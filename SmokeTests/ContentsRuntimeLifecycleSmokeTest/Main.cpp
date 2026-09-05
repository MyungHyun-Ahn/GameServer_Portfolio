#include "ContentsRuntimeLifecycleSmokeTestPch.h"

namespace
{
	using ContentsRuntime::Core::FContentId;
	using ContentsRuntime::Core::FContentInstanceId;

	inline constexpr FContentId kSourceContentId = 6000;
	inline constexpr FContentId kTargetContentId = 6001;
	inline constexpr FContentInstanceId kSourceContentInstanceId = ContentsRuntime::Core::MakeContentInstanceId(kSourceContentId, 0, 1);
	inline constexpr FContentInstanceId kTargetContentInstanceId = ContentsRuntime::Core::MakeContentInstanceId(kTargetContentId, 0, 1);
	inline constexpr std::uint16_t kMoveOpcode = 1;
	inline constexpr std::uint64_t kSessionId = (std::uint64_t{1} << 32) | 7;
	inline constexpr auto kWaitTimeout = std::chrono::seconds(5);
	inline constexpr std::uint32_t kInvalidWorkerIndex = std::numeric_limits<std::uint32_t>::max();

	enum class EProbeEvent : std::uint8_t
	{
		SourceEnter,
		SourcePacketBegin,
		MoveIssued,
		TargetFence,
		SourceFenceAfterDisconnect,
		SourcePacketEnd,
		SourceLeaveBegin,
		SourceLeaveEnd,
		TargetEnter,
		MoveCompleted
	};

	std::string_view ToString(
		const EProbeEvent event) noexcept
	{
		switch (event)
		{
			case EProbeEvent::SourceEnter:
				return "SourceEnter";
			case EProbeEvent::SourcePacketBegin:
				return "SourcePacketBegin";
			case EProbeEvent::MoveIssued:
				return "MoveIssued";
			case EProbeEvent::TargetFence:
				return "TargetFence";
			case EProbeEvent::SourceFenceAfterDisconnect:
				return "SourceFenceAfterDisconnect";
			case EProbeEvent::SourcePacketEnd:
				return "SourcePacketEnd";
			case EProbeEvent::SourceLeaveBegin:
				return "SourceLeaveBegin";
			case EProbeEvent::SourceLeaveEnd:
				return "SourceLeaveEnd";
			case EProbeEvent::TargetEnter:
				return "TargetEnter";
			case EProbeEvent::MoveCompleted:
				return "MoveCompleted";
			default:
				return "Unknown";
		}
	}

	class FTransitionProbe final
	{
	public:
		void Record(
			const EProbeEvent event)
		{
			{
				std::lock_guard<std::mutex> lock(m_lock);
				m_events.push_back(event);
			}
			m_changed.notify_all();
		}

		void RecordSourceEnter()
		{
			{
				std::lock_guard<std::mutex> lock(m_lock);
				m_sourceWorkerIndex = ContentsRuntime::Threading::FContentThread::GetCurrentWorkerIndex();
				m_events.push_back(EProbeEvent::SourceEnter);
			}
			m_changed.notify_all();
		}

		void RecordTargetEnter()
		{
			{
				std::lock_guard<std::mutex> lock(m_lock);
				m_targetWorkerIndex = ContentsRuntime::Threading::FContentThread::GetCurrentWorkerIndex();
				m_events.push_back(EProbeEvent::TargetEnter);
			}
			m_changed.notify_all();
		}

		void SetMoveAccepted(
			const bool accepted)
		{
			std::lock_guard<std::mutex> lock(m_lock);
			m_moveAccepted = accepted;
		}

		bool WasMoveAccepted() const
		{
			std::lock_guard<std::mutex> lock(m_lock);
			return m_moveAccepted;
		}

		bool WaitFor(
			const EProbeEvent event,
			const std::chrono::steady_clock::duration timeout)
		{
			std::unique_lock<std::mutex> lock(m_lock);
			return m_changed.wait_for(lock,
				timeout,
				[this, event]()
				{
					return std::find(m_events.begin(), m_events.end(), event) != m_events.end();
				});
		}

		bool HasEvent(
			const EProbeEvent event) const
		{
			std::lock_guard<std::mutex> lock(m_lock);
			return std::find(m_events.begin(), m_events.end(), event) != m_events.end();
		}

		std::size_t CountEvent(
			const EProbeEvent event) const
		{
			std::lock_guard<std::mutex> lock(m_lock);
			return static_cast<std::size_t>(std::count(m_events.begin(), m_events.end(), event));
		}

		void WaitUntilSourceHandlerReleased()
		{
			std::unique_lock<std::mutex> lock(m_lock);
			m_changed.wait(lock,
				[this]()
				{
					return m_sourceHandlerReleased;
				});
		}

		void ReleaseSourceHandler()
		{
			{
				std::lock_guard<std::mutex> lock(m_lock);
				m_sourceHandlerReleased = true;
			}
			m_changed.notify_all();
		}

		std::optional<std::size_t> FindEventIndex(
			const EProbeEvent event) const
		{
			std::lock_guard<std::mutex> lock(m_lock);
			const auto found = std::find(m_events.begin(), m_events.end(), event);
			if (found == m_events.end())
			{
				return std::nullopt;
			}

			return static_cast<std::size_t>(std::distance(m_events.begin(), found));
		}

		std::pair<std::uint32_t, std::uint32_t> GetWorkerIndices() const
		{
			std::lock_guard<std::mutex> lock(m_lock);
			return {m_sourceWorkerIndex, m_targetWorkerIndex};
		}

		std::vector<EProbeEvent> GetEvents() const
		{
			std::lock_guard<std::mutex> lock(m_lock);
			return m_events;
		}

	private:
		mutable std::mutex m_lock;
		std::condition_variable m_changed;
		std::vector<EProbeEvent> m_events;
		std::uint32_t m_sourceWorkerIndex = kInvalidWorkerIndex;
		std::uint32_t m_targetWorkerIndex = kInvalidWorkerIndex;
		bool m_moveAccepted = false;
		bool m_sourceHandlerReleased = false;
	};

	class FSourceContent final : public ContentsRuntime::Core::IContent
	{
	public:
		explicit FSourceContent(
			std::shared_ptr<FTransitionProbe> probe)
			: m_probe(std::move(probe))
		{
		}

		FContentId GetContentId() const noexcept override
		{
			return kSourceContentId;
		}

		FContentInstanceId GetContentInstanceId() const noexcept override
		{
			return kSourceContentInstanceId;
		}

		void OnEnter(
			std::uint64_t,
			std::uint64_t,
			ContentsRuntime::Bridge::IContentBridge&) override
		{
			m_probe->RecordSourceEnter();
		}

		void OnLeave(
			std::uint64_t,
			std::uint64_t,
			ContentsRuntime::Bridge::IContentBridge&) override
		{
			m_probe->Record(EProbeEvent::SourceLeaveBegin);
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
			m_probe->Record(EProbeEvent::SourceLeaveEnd);
		}

		void OnPacket(
			const std::uint64_t sessionId,
			std::uint64_t,
			const std::uint16_t opcode,
			std::span<const char>,
			ContentsRuntime::Bridge::IContentBridge& bridge) override
		{
			if (opcode != kMoveOpcode)
			{
				return;
			}

			m_probe->Record(EProbeEvent::SourcePacketBegin);
			const bool accepted = bridge.MoveSessionToInstanceWithCompletion(sessionId,
				kTargetContentInstanceId,
				[probe = m_probe]()
				{
					probe->Record(EProbeEvent::MoveCompleted);
				});
			m_probe->SetMoveAccepted(accepted);
			m_probe->Record(EProbeEvent::MoveIssued);
			m_probe->WaitUntilSourceHandlerReleased();
			m_probe->Record(EProbeEvent::SourcePacketEnd);
		}

	private:
		std::shared_ptr<FTransitionProbe> m_probe;
	};

	class FTargetContent final : public ContentsRuntime::Core::IContent
	{
	public:
		explicit FTargetContent(
			std::shared_ptr<FTransitionProbe> probe)
			: m_probe(std::move(probe))
		{
		}

		FContentId GetContentId() const noexcept override
		{
			return kTargetContentId;
		}

		FContentInstanceId GetContentInstanceId() const noexcept override
		{
			return kTargetContentInstanceId;
		}

		void OnEnter(
			std::uint64_t,
			std::uint64_t,
			ContentsRuntime::Bridge::IContentBridge&) override
		{
			m_probe->RecordTargetEnter();
		}

		void OnLeave(
			std::uint64_t,
			std::uint64_t,
			ContentsRuntime::Bridge::IContentBridge&) override
		{
		}

		void OnPacket(
			std::uint64_t,
			std::uint64_t,
			std::uint16_t,
			std::span<const char>,
			ContentsRuntime::Bridge::IContentBridge&) override
		{
		}

	private:
		std::shared_ptr<FTransitionProbe> m_probe;
	};

	void PrintEvents(
		const std::vector<EProbeEvent>& events)
	{
		std::cout << "[TRACE] ";
		for (std::size_t index = 0; index < events.size(); ++index)
		{
			if (index > 0)
			{
				std::cout << " -> ";
			}
			std::cout << ToString(events[index]);
		}
		std::cout << '\n';
	}
}

int main()
{
	auto probe = std::make_shared<FTransitionProbe>();
	ContentsRuntime::Routing::FContentRuntime runtime;
	ContentsRuntime::Core::SContentRuntimeConfig config{};
	config.workerThreadCount = 2;
	runtime.SetConfig(config);

	if (!runtime.RegisterContent(std::make_unique<FSourceContent>(probe)) ||
		!runtime.RegisterContent(std::make_unique<FTargetContent>(probe)))
	{
		std::cerr << "[FAIL] Content registration failed.\n";
		return 1;
	}

	NetworkLib::Core::FStubServer server(NetworkLib::Core::EBackendKind::Iocp);
	runtime.Start(server);

	auto fail = [&](const std::string_view message)
	{
		probe->ReleaseSourceHandler();
		runtime.Stop();
		PrintEvents(probe->GetEvents());
		std::cerr << "[FAIL] " << message << '\n';
		return 1;
	};

	if (!runtime.EnterSessionToInstance(kSessionId, kSourceContentInstanceId) || !probe->WaitFor(EProbeEvent::SourceEnter, kWaitTimeout))
	{
		return fail("Initial source enter did not complete.");
	}

	if (!runtime.EnqueuePacket(kSessionId, kMoveOpcode, nullptr, 0) || !probe->WaitFor(EProbeEvent::MoveIssued, kWaitTimeout))
	{
		return fail("Move request did not reach the source Content.");
	}

	if (!probe->WasMoveAccepted())
	{
		return fail("MoveSessionToInstanceWithCompletion rejected the move.");
	}

	if (!runtime.EnqueueCompletionToInstance(kTargetContentInstanceId,
			[probe]()
			{
				probe->Record(EProbeEvent::TargetFence);
			}) ||
		!probe->WaitFor(EProbeEvent::TargetFence, kWaitTimeout))
	{
		return fail("Target worker fence did not complete.");
	}

	if (probe->HasEvent(EProbeEvent::TargetEnter))
	{
		return fail("Target OnEnter ran before the source handler and OnLeave completed.");
	}
	if (probe->HasEvent(EProbeEvent::SourceLeaveBegin) || probe->HasEvent(EProbeEvent::SourceLeaveEnd))
	{
		return fail("Source OnLeave ran reentrantly before the source packet handler returned.");
	}

	probe->ReleaseSourceHandler();
	if (!probe->WaitFor(EProbeEvent::MoveCompleted, kWaitTimeout))
	{
		return fail("Move completion callback did not run.");
	}

	const auto sourcePacketEndIndex = probe->FindEventIndex(EProbeEvent::SourcePacketEnd);
	const auto sourceLeaveBeginIndex = probe->FindEventIndex(EProbeEvent::SourceLeaveBegin);
	const auto sourceLeaveEndIndex = probe->FindEventIndex(EProbeEvent::SourceLeaveEnd);
	const auto targetEnterIndex = probe->FindEventIndex(EProbeEvent::TargetEnter);
	const auto moveCompletedIndex = probe->FindEventIndex(EProbeEvent::MoveCompleted);
	if (!sourcePacketEndIndex.has_value() || !sourceLeaveBeginIndex.has_value() || !sourceLeaveEndIndex.has_value() ||
		!targetEnterIndex.has_value() || !moveCompletedIndex.has_value() ||
		!(*sourcePacketEndIndex < *sourceLeaveBeginIndex && *sourceLeaveBeginIndex < *sourceLeaveEndIndex &&
			*sourceLeaveEndIndex < *targetEnterIndex && *targetEnterIndex < *moveCompletedIndex))
	{
		return fail("Expected source packet return -> Source OnLeave -> Target OnEnter -> completion callback ordering was not observed.");
	}

	const auto [sourceWorkerIndex, targetWorkerIndex] = probe->GetWorkerIndices();
	if (sourceWorkerIndex == kInvalidWorkerIndex || targetWorkerIndex == kInvalidWorkerIndex || sourceWorkerIndex == targetWorkerIndex)
	{
		return fail("The test Contents were not assigned to different workers.");
	}

	const auto currentContentInstanceId = runtime.GetCurrentContentInstanceId(kSessionId);
	if (!currentContentInstanceId.has_value() || *currentContentInstanceId != kTargetContentInstanceId)
	{
		return fail("The final session route does not point to the target Content.");
	}

	runtime.Stop();
	PrintEvents(probe->GetEvents());
	std::cout << "[PASS] Cross-worker lifecycle order verified. sourceWorker=" << sourceWorkerIndex << " targetWorker=" << targetWorkerIndex
			  << '\n';

	auto disconnectProbe = std::make_shared<FTransitionProbe>();
	ContentsRuntime::Routing::FContentRuntime disconnectRuntime;
	disconnectRuntime.SetConfig(config);
	if (!disconnectRuntime.RegisterContent(std::make_unique<FSourceContent>(disconnectProbe)) ||
		!disconnectRuntime.RegisterContent(std::make_unique<FTargetContent>(disconnectProbe)))
	{
		std::cerr << "[FAIL] Disconnect-race Content registration failed.\n";
		return 1;
	}

	NetworkLib::Core::FStubServer disconnectServer(NetworkLib::Core::EBackendKind::Iocp);
	disconnectRuntime.Start(disconnectServer);
	auto failDisconnectRace = [&](const std::string_view message)
	{
		disconnectProbe->ReleaseSourceHandler();
		disconnectRuntime.Stop();
		PrintEvents(disconnectProbe->GetEvents());
		std::cerr << "[FAIL] " << message << '\n';
		return 1;
	};

	if (!disconnectRuntime.EnterSessionToInstance(kSessionId, kSourceContentInstanceId) ||
		!disconnectProbe->WaitFor(EProbeEvent::SourceEnter, kWaitTimeout) ||
		!disconnectRuntime.EnqueuePacket(kSessionId, kMoveOpcode, nullptr, 0) ||
		!disconnectProbe->WaitFor(EProbeEvent::MoveIssued, kWaitTimeout) || !disconnectProbe->WasMoveAccepted())
	{
		return failDisconnectRace("Disconnect-race move setup failed.");
	}

	disconnectRuntime.LeaveSession(kSessionId);
	if (!disconnectRuntime.EnqueueCompletionToInstance(kSourceContentInstanceId,
			[disconnectProbe]()
			{
				disconnectProbe->Record(EProbeEvent::SourceFenceAfterDisconnect);
			}))
	{
		return failDisconnectRace("Source fence enqueue failed after disconnect.");
	}

	disconnectProbe->ReleaseSourceHandler();
	if (!disconnectProbe->WaitFor(EProbeEvent::SourceFenceAfterDisconnect, kWaitTimeout))
	{
		return failDisconnectRace("Source fence did not complete after disconnect.");
	}

	if (disconnectProbe->CountEvent(EProbeEvent::SourceLeaveBegin) != 1 || disconnectProbe->CountEvent(EProbeEvent::SourceLeaveEnd) != 1 ||
		disconnectProbe->HasEvent(EProbeEvent::TargetEnter) || disconnectProbe->HasEvent(EProbeEvent::MoveCompleted) ||
		disconnectRuntime.GetCurrentContentInstanceId(kSessionId).has_value())
	{
		return failDisconnectRace("Disconnect during a pending move did not produce exactly one Source OnLeave and no Target OnEnter.");
	}

	disconnectRuntime.Stop();
	PrintEvents(disconnectProbe->GetEvents());
	std::cout << "[PASS] Pending-move disconnect produced exactly one Source OnLeave and no Target OnEnter.\n";

	auto sameWorkerProbe = std::make_shared<FTransitionProbe>();
	ContentsRuntime::Routing::FContentRuntime sameWorkerRuntime;
	ContentsRuntime::Core::SContentRuntimeConfig sameWorkerConfig{};
	sameWorkerConfig.workerThreadCount = 1;
	sameWorkerRuntime.SetConfig(sameWorkerConfig);
	if (!sameWorkerRuntime.RegisterContent(std::make_unique<FSourceContent>(sameWorkerProbe)) ||
		!sameWorkerRuntime.RegisterContent(std::make_unique<FTargetContent>(sameWorkerProbe)))
	{
		std::cerr << "[FAIL] Same-worker Content registration failed.\n";
		return 1;
	}

	NetworkLib::Core::FStubServer sameWorkerServer(NetworkLib::Core::EBackendKind::Iocp);
	sameWorkerRuntime.Start(sameWorkerServer);
	auto failSameWorker = [&](const std::string_view message)
	{
		sameWorkerProbe->ReleaseSourceHandler();
		sameWorkerRuntime.Stop();
		PrintEvents(sameWorkerProbe->GetEvents());
		std::cerr << "[FAIL] " << message << '\n';
		return 1;
	};

	if (!sameWorkerRuntime.EnterSessionToInstance(kSessionId, kSourceContentInstanceId) ||
		!sameWorkerProbe->WaitFor(EProbeEvent::SourceEnter, kWaitTimeout) ||
		!sameWorkerRuntime.EnqueuePacket(kSessionId, kMoveOpcode, nullptr, 0) ||
		!sameWorkerProbe->WaitFor(EProbeEvent::MoveIssued, kWaitTimeout) || !sameWorkerProbe->WasMoveAccepted())
	{
		return failSameWorker("Same-worker move setup failed.");
	}

	if (sameWorkerProbe->HasEvent(EProbeEvent::SourceLeaveBegin) || sameWorkerProbe->HasEvent(EProbeEvent::SourceLeaveEnd) ||
		sameWorkerProbe->HasEvent(EProbeEvent::TargetEnter))
	{
		return failSameWorker("Same-worker lifecycle callback ran before the source packet handler returned.");
	}

	sameWorkerProbe->ReleaseSourceHandler();
	if (!sameWorkerProbe->WaitFor(EProbeEvent::MoveCompleted, kWaitTimeout))
	{
		return failSameWorker("Same-worker move completion callback did not run.");
	}

	const auto sameWorkerPacketEndIndex = sameWorkerProbe->FindEventIndex(EProbeEvent::SourcePacketEnd);
	const auto sameWorkerLeaveBeginIndex = sameWorkerProbe->FindEventIndex(EProbeEvent::SourceLeaveBegin);
	const auto sameWorkerLeaveEndIndex = sameWorkerProbe->FindEventIndex(EProbeEvent::SourceLeaveEnd);
	const auto sameWorkerEnterIndex = sameWorkerProbe->FindEventIndex(EProbeEvent::TargetEnter);
	const auto sameWorkerCompletedIndex = sameWorkerProbe->FindEventIndex(EProbeEvent::MoveCompleted);
	const auto [sameSourceWorkerIndex, sameTargetWorkerIndex] = sameWorkerProbe->GetWorkerIndices();
	const auto sameWorkerRoute = sameWorkerRuntime.GetCurrentContentInstanceId(kSessionId);
	if (!sameWorkerPacketEndIndex.has_value() || !sameWorkerLeaveBeginIndex.has_value() || !sameWorkerLeaveEndIndex.has_value() ||
		!sameWorkerEnterIndex.has_value() || !sameWorkerCompletedIndex.has_value() ||
		!(*sameWorkerPacketEndIndex < *sameWorkerLeaveBeginIndex && *sameWorkerLeaveBeginIndex < *sameWorkerLeaveEndIndex &&
			*sameWorkerLeaveEndIndex < *sameWorkerEnterIndex && *sameWorkerEnterIndex < *sameWorkerCompletedIndex) ||
		sameSourceWorkerIndex == kInvalidWorkerIndex || sameSourceWorkerIndex != sameTargetWorkerIndex || !sameWorkerRoute.has_value() ||
		*sameWorkerRoute != kTargetContentInstanceId)
	{
		return failSameWorker("Same-worker lifecycle order or final route was invalid.");
	}

	sameWorkerRuntime.Stop();
	PrintEvents(sameWorkerProbe->GetEvents());
	std::cout << "[PASS] Same-worker lifecycle order verified. worker=" << sameSourceWorkerIndex << '\n';
	return 0;
}
