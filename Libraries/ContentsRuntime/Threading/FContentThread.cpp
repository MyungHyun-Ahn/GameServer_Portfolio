#include "ContentsRuntimePch.h"

#include "ContentsRuntime/Threading/FContentThread.h"

#include "ContentsRuntime/Bridge/IContentBridge.h"
#include "ContentsRuntime/Core/ContentExecutionState.h"
#include "ContentsRuntime/Core/IContent.h"
#include "ContentsRuntime/Routing/FContentRuntime.h"

#include <Windows.h>

namespace ContentsRuntime::Threading
{
	namespace
	{
		inline constexpr std::size_t kMailboxBatchSize = 64;
		thread_local std::uint32_t g_currentWorkerIndex = Core::SContentExecutionState::kInvalidWorkerIndex;

		class FCurrentWorkerIndexScope final
		{
		public:
			explicit FCurrentWorkerIndexScope(
				const std::uint32_t workerIndex) noexcept
			{
				g_currentWorkerIndex = workerIndex;
			}

			~FCurrentWorkerIndexScope()
			{
				g_currentWorkerIndex = Core::SContentExecutionState::kInvalidWorkerIndex;
			}
		};

		template <typename TValue>
		void UpdateMaxAtomic(
			std::atomic<TValue>& target,
			const TValue candidate)
		{
			TValue current = target.load(std::memory_order_relaxed);
			while (current < candidate && !target.compare_exchange_weak(current, candidate, std::memory_order_relaxed))
			{
			}
		}

		std::uint64_t ToNanoseconds(
			const std::chrono::steady_clock::duration duration)
		{
			return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count());
		}

		std::uint32_t MakePacketTimingKey(
			const Core::FContentId contentId,
			const std::uint16_t opcode) noexcept
		{
			return (static_cast<std::uint32_t>(contentId) << 16) | opcode;
		}

		std::int64_t CurrentSteadyMilliseconds() noexcept
		{
			return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
		}

		void RunRaceInjection(
			const Core::SContentRuntimeConfig& config,
			std::atomic<std::uint64_t>& counter) noexcept
		{
			if (!config.enableRaceInjection || config.raceInjectionPeriod == 0 ||
				config.raceInjectionMode == Core::ERaceInjectionMode::None)
			{
				return;
			}

			const std::uint64_t current = counter.fetch_add(1, std::memory_order_relaxed) + 1;
			if ((current % config.raceInjectionPeriod) != 0)
			{
				return;
			}

			switch (config.raceInjectionMode)
			{
				case Core::ERaceInjectionMode::SwitchToThread:
					::SwitchToThread();
					break;
				case Core::ERaceInjectionMode::Sleep0:
					::Sleep(0);
					break;
				case Core::ERaceInjectionMode::Yield:
					std::this_thread::yield();
					break;
				default:
					break;
			}
		}

		bool ShouldTraceSession(
			const Core::SContentRuntimeConfig& config,
			const std::uint64_t sessionId)
		{
			if (!config.enableTraceLogging || !config.traceLogger)
			{
				return false;
			}

			if (config.tracedSessionId == nullptr)
			{
				return true;
			}

			return config.tracedSessionId->load(std::memory_order_relaxed) == sessionId;
		}

		void TraceThread(
			const Core::SContentRuntimeConfig& config,
			const std::uint64_t sessionId,
			const std::string& message)
		{
			if (!ShouldTraceSession(config, sessionId))
			{
				return;
			}

			config.traceLogger(message);
		}
	}

	struct FContentThread::SImpl
	{
		Bridge::IContentBridge* bridge = nullptr;
		Core::SContentRuntimeConfig config{};
		std::uint32_t workerIndex = 0;
		std::thread workerThread;
		std::mutex lock;
		mutable std::shared_mutex contentsLock;
		std::mutex readyLock;
		std::condition_variable wakeCondition;
		std::deque<Core::FContentInstanceId> readyContentIds;
		std::unordered_map<Core::FContentInstanceId, Core::SContentExecutionState*> contents;
		std::unordered_map<std::uint32_t, Core::SPacketTimingMetricConfig> packetTimingMetrics;
		std::atomic<std::uint64_t> pendingWorkCount = 0;
		std::atomic<std::uint64_t> raceInjectionCounter = 0;
		std::atomic<std::int64_t> lastTransferPolicyActionMs = 0;
		std::atomic<std::int64_t> lastStealAttemptMs = 0;
		std::atomic<bool> running = false;
	};

	FContentThread::FContentThread(
		Bridge::IContentBridge& bridge,
		const Core::SContentRuntimeConfig& config,
		const std::uint32_t workerIndex)
		: m_impl(std::make_unique<SImpl>())
	{
		m_impl->bridge = &bridge;
		m_impl->config = config;
		m_impl->workerIndex = workerIndex;
		for (const Core::SPacketTimingMetricConfig& timingMetric : config.packetTimingMetrics)
		{
			m_impl->packetTimingMetrics.insert_or_assign(MakePacketTimingKey(timingMetric.contentId, timingMetric.opcode), timingMetric);
		}
	}

	FContentThread::~FContentThread()
	{
		Stop();
	}

	bool FContentThread::RegisterContent(
		Core::SContentExecutionState& executionState)
	{
		const Core::FContentInstanceId contentInstanceId = executionState.contentInstanceId;
		if (executionState.content == nullptr || executionState.contentId == Core::kInvalidContentId ||
			contentInstanceId == Core::kInvalidContentInstanceId)
		{
			return false;
		}

		std::unique_lock<std::shared_mutex> contentsLock(m_impl->contentsLock);
		if (m_impl->contents.contains(contentInstanceId))
		{
			return false;
		}

		auto [stateIt, inserted] = m_impl->contents.try_emplace(contentInstanceId, &executionState);
		if (!inserted)
		{
			return false;
		}

		executionState.ownerWorkerIndex.store(m_impl->workerIndex, std::memory_order_relaxed);

		bool shouldQueueReady = false;
		std::size_t mailboxItemCount = 0;
		{
			std::lock_guard<std::mutex> mailboxLock(executionState.mailbox.lock);
			mailboxItemCount = executionState.mailbox.items.size();
			if (!executionState.mailbox.items.empty() && !executionState.mailbox.readyQueued)
			{
				executionState.mailbox.readyQueued = true;
				shouldQueueReady = true;
			}
		}

		if (shouldQueueReady)
		{
			m_impl->pendingWorkCount.fetch_add(static_cast<std::uint64_t>(mailboxItemCount), std::memory_order_relaxed);
			std::lock_guard<std::mutex> readyLock(m_impl->readyLock);
			m_impl->readyContentIds.push_back(contentInstanceId);
		}

		if (m_impl->running.load(std::memory_order_relaxed))
		{
			m_impl->wakeCondition.notify_one();
		}
		return true;
	}

	bool FContentThread::DetachContent(
		const Core::FContentInstanceId contentInstanceId)
	{
		Core::SContentExecutionState* executionState = nullptr;
		{
			std::shared_lock<std::shared_mutex> contentsLock(m_impl->contentsLock);
			const auto stateIt = m_impl->contents.find(contentInstanceId);
			if (stateIt == m_impl->contents.end() || stateIt->second == nullptr)
			{
				return false;
			}

			executionState = stateIt->second;
		}

		std::unique_lock<std::mutex> consumerLock(executionState->consumerLock);
		return DetachContentForTransfer(*executionState);
	}

	bool FContentThread::DetachContentForTransfer(
		Core::SContentExecutionState& executionState)
	{
		const Core::FContentInstanceId contentInstanceId = executionState.contentInstanceId;
		std::unique_lock<std::shared_mutex> contentsLock(m_impl->contentsLock);
		const auto stateIt = m_impl->contents.find(contentInstanceId);
		if (stateIt == m_impl->contents.end() || stateIt->second != &executionState)
		{
			return false;
		}

		m_impl->contents.erase(stateIt);
		contentsLock.unlock();

		std::size_t mailboxItemCount = 0;
		{
			std::lock_guard<std::mutex> mailboxLock(executionState.mailbox.lock);
			mailboxItemCount = executionState.mailbox.items.size();
			executionState.mailbox.readyQueued = false;
		}
		if (mailboxItemCount > 0)
		{
			m_impl->pendingWorkCount.fetch_sub(static_cast<std::uint64_t>(mailboxItemCount), std::memory_order_relaxed);
		}
		executionState.ownerWorkerIndex.store(Core::SContentExecutionState::kInvalidWorkerIndex, std::memory_order_relaxed);
		return true;
	}

	void FContentThread::Start()
	{
		if (m_impl->running.exchange(true, std::memory_order_relaxed))
		{
			return;
		}

		{
			std::unique_lock<std::shared_mutex> contentsLock(m_impl->contentsLock);
			const auto now = std::chrono::steady_clock::now();
			for (auto& [contentInstanceId, state] : m_impl->contents)
			{
				(void)contentInstanceId;
				if (state != nullptr)
				{
					state->nextFrameTime = now + state->frameDuration;
				}
			}
		}

		m_impl->workerThread = std::thread(
			[this]()
			{
				SImpl& impl = *m_impl;
				const FCurrentWorkerIndexScope workerIndexScope(impl.workerIndex);
				Foundation::Diagnostics::FTimingThreadLocalCollector timingCollector(impl.config.timingMetricsRuntime);

				auto pushReadyContent = [&](const Core::FContentInstanceId contentInstanceId)
				{
					std::lock_guard<std::mutex> readyLock(impl.readyLock);
					impl.readyContentIds.push_back(contentInstanceId);
					impl.wakeCondition.notify_one();
				};

				auto processQueuedWork = [&](Core::SContentExecutionState& state, Core::SQueuedWorkItem& workItem)
				{
					if (state.content == nullptr)
					{
						return;
					}

					const auto dequeuedAt = std::chrono::steady_clock::now();
					const auto queueWait = dequeuedAt - workItem.enqueuedAt;
					const auto queueWaitMs = std::chrono::duration_cast<std::chrono::milliseconds>(queueWait).count();
					const auto pendingWorkCount = impl.pendingWorkCount.fetch_sub(1, std::memory_order_relaxed) - 1;

					state.inFlightCallbackCount.fetch_add(1, std::memory_order_relaxed);
					switch (workItem.kind)
					{
						case Core::EQueuedWorkKind::Enter:
							if (impl.config.enableTraceLogging)
							{
								std::ostringstream oss;
								oss << "worker execute enter. workerIndex=" << impl.workerIndex
									<< " sessionId=" << workItem.lifecycleEvent.sessionId
									<< " routeGeneration=" << workItem.lifecycleEvent.routeGeneration
									<< " contentInstanceId=" << workItem.lifecycleEvent.contentInstanceId << " queueWaitMs=" << queueWaitMs
									<< " pendingWorkCount=" << pendingWorkCount
									<< " enterQueueDepth=" << state.enterQueueDepth.load(std::memory_order_relaxed)
									<< " leaveQueueDepth=" << state.leaveQueueDepth.load(std::memory_order_relaxed)
									<< " packetQueueDepth=" << state.packetQueueDepth.load(std::memory_order_relaxed);
								TraceThread(impl.config, workItem.lifecycleEvent.sessionId, oss.str());
							}

							state.content->OnEnter(
								workItem.lifecycleEvent.sessionId, workItem.lifecycleEvent.routeGeneration, *impl.bridge);
							if (workItem.lifecycleEvent.completionFlag != nullptr)
							{
								workItem.lifecycleEvent.completionFlag->store(true, std::memory_order_release);
							}
							if (workItem.lifecycleEvent.completionCallback)
							{
								if (impl.config.enableTraceLogging)
								{
									std::ostringstream oss;
									oss << "worker invoke enter completion callback. workerIndex=" << impl.workerIndex
										<< " sessionId=" << workItem.lifecycleEvent.sessionId
										<< " routeGeneration=" << workItem.lifecycleEvent.routeGeneration
										<< " contentInstanceId=" << workItem.lifecycleEvent.contentInstanceId;
									TraceThread(impl.config, workItem.lifecycleEvent.sessionId, oss.str());
								}
								workItem.lifecycleEvent.completionCallback();
							}
							state.enterCount.fetch_add(1, std::memory_order_relaxed);
							state.enterQueueDepth.fetch_sub(1, std::memory_order_relaxed);
							break;

						case Core::EQueuedWorkKind::Leave:
							if (impl.config.enableTraceLogging)
							{
								std::ostringstream oss;
								oss << "worker execute leave. workerIndex=" << impl.workerIndex
									<< " sessionId=" << workItem.lifecycleEvent.sessionId
									<< " routeGeneration=" << workItem.lifecycleEvent.routeGeneration
									<< " contentInstanceId=" << workItem.lifecycleEvent.contentInstanceId << " queueWaitMs=" << queueWaitMs
									<< " pendingWorkCount=" << pendingWorkCount
									<< " enterQueueDepth=" << state.enterQueueDepth.load(std::memory_order_relaxed)
									<< " leaveQueueDepth=" << state.leaveQueueDepth.load(std::memory_order_relaxed)
									<< " packetQueueDepth=" << state.packetQueueDepth.load(std::memory_order_relaxed);
								TraceThread(impl.config, workItem.lifecycleEvent.sessionId, oss.str());
							}

							state.content->OnLeave(
								workItem.lifecycleEvent.sessionId, workItem.lifecycleEvent.routeGeneration, *impl.bridge);
							if (workItem.lifecycleEvent.completionFlag != nullptr)
							{
								workItem.lifecycleEvent.completionFlag->store(true, std::memory_order_release);
							}
							if (workItem.lifecycleEvent.completionCallback)
							{
								workItem.lifecycleEvent.completionCallback();
							}
							state.leaveCount.fetch_add(1, std::memory_order_relaxed);
							state.leaveQueueDepth.fetch_sub(1, std::memory_order_relaxed);
							break;

						case Core::EQueuedWorkKind::Packet:
						{
							const auto timingMetricIt =
								impl.packetTimingMetrics.find(MakePacketTimingKey(state.contentId, workItem.packet.opcode));
							const Core::SPacketTimingMetricConfig* const timingMetric =
								timingMetricIt != impl.packetTimingMetrics.end() ? &timingMetricIt->second : nullptr;
							if (timingMetric != nullptr)
							{
								timingCollector.RecordDuration(
									static_cast<Foundation::Diagnostics::FTimingMetricIndex>(timingMetric->queueWaitMetricIndex),
									std::chrono::duration_cast<std::chrono::nanoseconds>(queueWait),
									workItem.packet.sessionId);
							}

							if (impl.config.enableTraceLogging)
							{
								std::ostringstream oss;
								oss << "worker dequeue packet. workerIndex=" << impl.workerIndex
									<< " sessionId=" << workItem.packet.sessionId << " opcode=" << workItem.packet.opcode
									<< " routeGeneration=" << workItem.packet.routeGeneration << " contentId=" << state.contentId
									<< " contentInstanceId=" << state.contentInstanceId
									<< " payloadBytes=" << workItem.packet.payload.size() << " queueWaitMs=" << queueWaitMs
									<< " pendingWorkCount=" << pendingWorkCount
									<< " enterQueueDepth=" << state.enterQueueDepth.load(std::memory_order_relaxed)
									<< " leaveQueueDepth=" << state.leaveQueueDepth.load(std::memory_order_relaxed)
									<< " packetQueueDepth=" << state.packetQueueDepth.load(std::memory_order_relaxed);
								TraceThread(impl.config, workItem.packet.sessionId, oss.str());
							}

							auto invokePacketHandler = [&]()
							{
								state.content->OnPacket(workItem.packet.sessionId,
									workItem.packet.routeGeneration,
									workItem.packet.opcode,
									std::span<const char>(workItem.packet.payload.data(), workItem.packet.payload.size()),
									*impl.bridge);
							};
							if (timingMetric != nullptr)
							{
								Foundation::Diagnostics::FTimingScope handlerTimingScope(timingCollector,
									static_cast<Foundation::Diagnostics::FTimingMetricIndex>(timingMetric->handlerMetricIndex),
									workItem.packet.sessionId);
								invokePacketHandler();
							}
							else
							{
								invokePacketHandler();
							}
							state.packetCount.fetch_add(1, std::memory_order_relaxed);
							state.packetQueueDepth.fetch_sub(1, std::memory_order_relaxed);
							break;
						}
					}

					state.inFlightCallbackCount.fetch_sub(1, std::memory_order_relaxed);
				};

				auto tryCommitRequestedTransferAtBoundary = [&](Core::SContentExecutionState& state) -> bool
				{
					const std::uint32_t targetWorkerIndex = state.requestedTransferTargetWorkerIndex.load(std::memory_order_relaxed);
					if (targetWorkerIndex == Core::SContentExecutionState::kInvalidWorkerIndex)
					{
						return false;
					}

					auto* runtime = dynamic_cast<Routing::FContentRuntime*>(impl.bridge);
					if (runtime == nullptr)
					{
						return false;
					}

					return runtime->CommitRequestedTransferAtWorkBoundary(state, impl.workerIndex);
				};

				auto tryScheduleDelegateTransferAtBoundary = [&](Core::SContentExecutionState& state) -> bool
				{
					auto* runtime = dynamic_cast<Routing::FContentRuntime*>(impl.bridge);
					if (runtime == nullptr || !impl.config.enableOwnershipTransferPolicy ||
						state.requestedTransferTargetWorkerIndex.load(std::memory_order_relaxed) !=
							Core::SContentExecutionState::kInvalidWorkerIndex)
					{
						return false;
					}

					const std::uint64_t totalQueueDepth = state.enterQueueDepth.load(std::memory_order_relaxed) +
														  state.leaveQueueDepth.load(std::memory_order_relaxed) +
														  state.packetQueueDepth.load(std::memory_order_relaxed);
					const std::int32_t lastDelayFrame = state.lastDelayFrame.load(std::memory_order_relaxed);
					const std::uint64_t workerPendingWorkCount = impl.pendingWorkCount.load(std::memory_order_relaxed);
					const bool overloaded = workerPendingWorkCount >= impl.config.ownershipTransferSourcePendingWorkThreshold &&
											(lastDelayFrame >= impl.config.ownershipTransferDelayFrameThreshold ||
												totalQueueDepth >= impl.config.ownershipTransferContentQueueDepthThreshold);
					if (!overloaded)
					{
						state.overloadSinceMs.store(0, std::memory_order_relaxed);
						return false;
					}

					const std::int64_t nowMs = CurrentSteadyMilliseconds();
					const std::int64_t lastPolicyActionMs = impl.lastTransferPolicyActionMs.load(std::memory_order_relaxed);
					if (lastPolicyActionMs > 0 &&
						(nowMs - lastPolicyActionMs) < impl.config.ownershipTransferWorkerStabilizationDuration.count())
					{
						return false;
					}

					std::int64_t overloadSinceMs = state.overloadSinceMs.load(std::memory_order_relaxed);
					if (overloadSinceMs <= 0)
					{
						state.overloadSinceMs.store(nowMs, std::memory_order_relaxed);
						return false;
					}

					if ((nowMs - overloadSinceMs) < impl.config.ownershipTransferSustainDuration.count())
					{
						return false;
					}

					if (!runtime->TryScheduleDelegateTransfer(state.contentInstanceId, impl.workerIndex))
					{
						return false;
					}

					impl.lastTransferPolicyActionMs.store(nowMs, std::memory_order_relaxed);
					return true;
				};

				auto tryScheduleWorkStealWhileIdle = [&]() -> bool
				{
					auto* runtime = dynamic_cast<Routing::FContentRuntime*>(impl.bridge);
					if (runtime == nullptr || !impl.config.enableOwnershipTransferPolicy)
					{
						return false;
					}

					if (impl.pendingWorkCount.load(std::memory_order_relaxed) != 0)
					{
						return false;
					}

					const std::int64_t nowMs = CurrentSteadyMilliseconds();
					const std::int64_t lastAttemptMs = impl.lastStealAttemptMs.load(std::memory_order_relaxed);
					if (lastAttemptMs > 0 && (nowMs - lastAttemptMs) < impl.config.workStealAttemptCooldown.count())
					{
						return false;
					}

					impl.lastStealAttemptMs.store(nowMs, std::memory_order_relaxed);
					return runtime->TryScheduleWorkSteal(impl.workerIndex);
				};

				auto processContentMailbox = [&](const Core::FContentInstanceId contentInstanceId)
				{
					Core::SContentExecutionState* state = nullptr;
					{
						std::shared_lock<std::shared_mutex> contentsLock(impl.contentsLock);
						const auto stateIt = impl.contents.find(contentInstanceId);
						if (stateIt == impl.contents.end() || stateIt->second == nullptr || stateIt->second->content == nullptr)
						{
							return;
						}

						state = stateIt->second;
					}

					std::unique_lock<std::mutex> consumerLock(state->consumerLock);
					{
						std::shared_lock<std::shared_mutex> contentsLock(impl.contentsLock);
						const auto stateIt = impl.contents.find(contentInstanceId);
						if (stateIt == impl.contents.end() || stateIt->second != state || state->content == nullptr)
						{
							return;
						}
					}

					std::vector<Core::SQueuedWorkItem> workItems;
					workItems.reserve(kMailboxBatchSize);
					{
						std::lock_guard<std::mutex> mailboxLock(state->mailbox.lock);
						while (!state->mailbox.items.empty() && workItems.size() < kMailboxBatchSize)
						{
							workItems.push_back(std::move(state->mailbox.items.front()));
							state->mailbox.items.pop_front();
						}
					}

					for (Core::SQueuedWorkItem& workItem : workItems)
					{
						processQueuedWork(*state, workItem);
					}

					tryScheduleDelegateTransferAtBoundary(*state);
					if (tryCommitRequestedTransferAtBoundary(*state))
					{
						return;
					}

					bool requeue = false;
					{
						std::lock_guard<std::mutex> mailboxLock(state->mailbox.lock);
						if (state->mailbox.items.empty())
						{
							state->mailbox.readyQueued = false;
						}
						else
						{
							requeue = true;
						}
					}

					if (requeue)
					{
						pushReadyContent(contentInstanceId);
					}
				};

				auto processDueFrames = [&]() -> bool
				{
					const auto now = std::chrono::steady_clock::now();
					std::vector<Core::FContentInstanceId> dueFrameContentInstanceIds;
					{
						std::shared_lock<std::shared_mutex> contentsLock(impl.contentsLock);
						dueFrameContentInstanceIds.reserve(impl.contents.size());
						for (const auto& [contentInstanceId, state] : impl.contents)
						{
							if (state == nullptr || state->content == nullptr)
							{
								continue;
							}

							if (now >= state->nextFrameTime)
							{
								dueFrameContentInstanceIds.push_back(contentInstanceId);
							}
						}
					}

					bool processed = false;
					for (const Core::FContentInstanceId contentInstanceId : dueFrameContentInstanceIds)
					{
						Core::IContent* content = nullptr;
						Core::SContentExecutionState* state = nullptr;
						int delayFrame = 0;
						{
							std::shared_lock<std::shared_mutex> contentsLock(impl.contentsLock);
							const auto stateIt = impl.contents.find(contentInstanceId);
							if (stateIt == impl.contents.end() || stateIt->second == nullptr || stateIt->second->content == nullptr)
							{
								continue;
							}

							state = stateIt->second;
						}

						std::unique_lock<std::mutex> consumerLock(state->consumerLock);
						{
							std::shared_lock<std::shared_mutex> contentsLock(impl.contentsLock);
							const auto stateIt = impl.contents.find(contentInstanceId);
							if (stateIt == impl.contents.end() || stateIt->second != state || state->content == nullptr)
							{
								continue;
							}
						}

						content = state->content;
						const auto frameNow = std::chrono::steady_clock::now();
						if (frameNow < state->nextFrameTime)
						{
							continue;
						}

						const auto frameDuration = std::max(state->frameDuration, std::chrono::milliseconds(1));
						const auto overdue = frameNow - state->nextFrameTime;
						delayFrame = 1 + static_cast<int>(overdue / frameDuration);
						state->nextFrameTime += frameDuration * delayFrame;
						state->lastDelayFrame.store(delayFrame, std::memory_order_relaxed);
						UpdateMaxAtomic(state->maxDelayFrame, delayFrame);
						state->inFlightCallbackCount.fetch_add(1, std::memory_order_relaxed);

						const auto frameBegin = std::chrono::steady_clock::now();
						content->OnFrame(delayFrame, *impl.bridge);
						const auto frameElapsedMs =
							std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - frameBegin).count();
						state->frameCount.fetch_add(1, std::memory_order_relaxed);
						state->inFlightCallbackCount.fetch_sub(1, std::memory_order_relaxed);
						if (impl.config.enableTraceLogging && frameElapsedMs >= 10)
						{
							std::ostringstream oss;
							oss << "worker slow frame. workerIndex=" << impl.workerIndex << " contentInstanceId=" << contentInstanceId
								<< " contentId=" << state->contentId << " delayFrame=" << delayFrame << " frameElapsedMs=" << frameElapsedMs
								<< " pendingWorkCount=" << impl.pendingWorkCount.load(std::memory_order_relaxed)
								<< " enterQueueDepth=" << state->enterQueueDepth.load(std::memory_order_relaxed)
								<< " leaveQueueDepth=" << state->leaveQueueDepth.load(std::memory_order_relaxed)
								<< " packetQueueDepth=" << state->packetQueueDepth.load(std::memory_order_relaxed);
							TraceThread(impl.config, 0, oss.str());
						}

						tryScheduleDelegateTransferAtBoundary(*state);
						if (tryCommitRequestedTransferAtBoundary(*state))
						{
							processed = true;
							continue;
						}
						processed = true;
					}

					return processed;
				};

				auto processIdleTransferCommits = [&]() -> bool
				{
					std::vector<Core::SContentExecutionState*> requestedStates;
					{
						std::shared_lock<std::shared_mutex> contentsLock(impl.contentsLock);
						requestedStates.reserve(impl.contents.size());
						for (const auto& [contentInstanceId, state] : impl.contents)
						{
							(void)contentInstanceId;
							if (state == nullptr || state->content == nullptr)
							{
								continue;
							}

							if (state->requestedTransferTargetWorkerIndex.load(std::memory_order_relaxed) !=
								Core::SContentExecutionState::kInvalidWorkerIndex)
							{
								requestedStates.push_back(state);
							}
						}
					}

					bool committed = false;
					for (Core::SContentExecutionState* state : requestedStates)
					{
						if (state == nullptr)
						{
							continue;
						}

						std::unique_lock<std::mutex> consumerLock(state->consumerLock);
						{
							std::shared_lock<std::shared_mutex> contentsLock(impl.contentsLock);
							const auto stateIt = impl.contents.find(state->contentInstanceId);
							if (stateIt == impl.contents.end() || stateIt->second != state || state->content == nullptr)
							{
								continue;
							}
						}

						if (tryCommitRequestedTransferAtBoundary(*state))
						{
							committed = true;
						}
					}

					return committed;
				};

				auto computeNextFrameTime = [&]() -> std::chrono::steady_clock::time_point
				{
					std::shared_lock<std::shared_mutex> contentsLock(impl.contentsLock);
					std::chrono::steady_clock::time_point nextWakeTime = std::chrono::steady_clock::time_point::max();
					for (const auto& [contentInstanceId, state] : impl.contents)
					{
						(void)contentInstanceId;
						if (state == nullptr || state->content == nullptr)
						{
							continue;
						}

						nextWakeTime = std::min(nextWakeTime, state->nextFrameTime);
					}

					return nextWakeTime;
				};

				while (impl.running.load(std::memory_order_relaxed))
				{
					Core::FContentInstanceId readyContentInstanceId = Core::kInvalidContentInstanceId;
					{
						std::lock_guard<std::mutex> readyLock(impl.readyLock);
						if (!impl.readyContentIds.empty())
						{
							readyContentInstanceId = impl.readyContentIds.front();
							impl.readyContentIds.pop_front();
						}
					}

					if (readyContentInstanceId != Core::kInvalidContentInstanceId)
					{
						processContentMailbox(readyContentInstanceId);
						processDueFrames();
						continue;
					}

					if (processDueFrames())
					{
						continue;
					}

					if (processIdleTransferCommits())
					{
						continue;
					}

					if (tryScheduleWorkStealWhileIdle())
					{
						continue;
					}

					const auto nextWakeTime = computeNextFrameTime();
					std::unique_lock<std::mutex> readyLock(impl.readyLock);
					if (!impl.running.load(std::memory_order_relaxed))
					{
						break;
					}

					if (!impl.readyContentIds.empty())
					{
						continue;
					}

					if (nextWakeTime == std::chrono::steady_clock::time_point::max())
					{
						impl.wakeCondition.wait(readyLock,
							[&impl]()
							{
								return !impl.running.load(std::memory_order_relaxed) || !impl.readyContentIds.empty();
							});
					}
					else
					{
						impl.wakeCondition.wait_until(readyLock,
							nextWakeTime,
							[&impl]()
							{
								return !impl.running.load(std::memory_order_relaxed) || !impl.readyContentIds.empty();
							});
					}
				}
			});
	}

	void FContentThread::Stop()
	{
		if (!m_impl->running.exchange(false, std::memory_order_relaxed))
		{
			return;
		}

		m_impl->wakeCondition.notify_all();
		if (m_impl->workerThread.joinable())
		{
			m_impl->workerThread.join();
		}

		std::vector<Core::SContentExecutionState*> executionStates;
		{
			std::unique_lock<std::shared_mutex> contentsLock(m_impl->contentsLock);
			for (auto& [contentInstanceId, state] : m_impl->contents)
			{
				(void)contentInstanceId;
				if (state != nullptr)
				{
					executionStates.push_back(state);
				}
			}
		}

		for (Core::SContentExecutionState* state : executionStates)
		{
			if (state == nullptr)
			{
				continue;
			}

			std::lock_guard<std::mutex> mailboxLock(state->mailbox.lock);
			state->mailbox.items.clear();
			state->mailbox.readyQueued = false;
		}

		{
			std::lock_guard<std::mutex> readyLock(m_impl->readyLock);
			m_impl->readyContentIds.clear();
		}
		m_impl->pendingWorkCount.store(0, std::memory_order_relaxed);
	}

	Core::SContentThreadStats FContentThread::GetStatsSnapshot(
		const Core::FContentInstanceId contentInstanceId)
	{
		Core::SContentThreadStats stats{};
		stats.contentInstanceId = contentInstanceId;

		std::shared_lock<std::shared_mutex> contentsLock(m_impl->contentsLock);
		const auto stateIt = m_impl->contents.find(contentInstanceId);
		if (stateIt == m_impl->contents.end())
		{
			return stats;
		}

		const Core::SContentExecutionState* state = stateIt->second;
		if (state == nullptr)
		{
			return stats;
		}

		stats.contentId = state->contentId;
		stats.contentInstanceId = state->contentInstanceId;
		stats.running = m_impl->running.load(std::memory_order_relaxed);
		stats.enqueueEnterCallCount = state->enqueueEnterCallCount.load(std::memory_order_relaxed);
		stats.enqueueLeaveCallCount = state->enqueueLeaveCallCount.load(std::memory_order_relaxed);
		stats.enqueuePacketCallCount = state->enqueuePacketCallCount.load(std::memory_order_relaxed);
		stats.enterCount = state->enterCount.load(std::memory_order_relaxed);
		stats.leaveCount = state->leaveCount.load(std::memory_order_relaxed);
		stats.packetCount = state->packetCount.load(std::memory_order_relaxed);
		stats.frameCount = state->frameCount.load(std::memory_order_relaxed);
		stats.enterQueueDepth = state->enterQueueDepth.load(std::memory_order_relaxed);
		stats.leaveQueueDepth = state->leaveQueueDepth.load(std::memory_order_relaxed);
		stats.packetQueueDepth = state->packetQueueDepth.load(std::memory_order_relaxed);
		stats.maxEnterQueueDepth = state->maxEnterQueueDepth.load(std::memory_order_relaxed);
		stats.maxLeaveQueueDepth = state->maxLeaveQueueDepth.load(std::memory_order_relaxed);
		stats.maxPacketQueueDepth = state->maxPacketQueueDepth.load(std::memory_order_relaxed);
		stats.enqueueEnterLockWaitNs = state->enqueueEnterLockWaitNs.load(std::memory_order_relaxed);
		stats.enqueueLeaveLockWaitNs = state->enqueueLeaveLockWaitNs.load(std::memory_order_relaxed);
		stats.enqueuePacketLockWaitNs = state->enqueuePacketLockWaitNs.load(std::memory_order_relaxed);
		stats.maxEnqueueEnterLockWaitNs = state->maxEnqueueEnterLockWaitNs.load(std::memory_order_relaxed);
		stats.maxEnqueueLeaveLockWaitNs = state->maxEnqueueLeaveLockWaitNs.load(std::memory_order_relaxed);
		stats.maxEnqueuePacketLockWaitNs = state->maxEnqueuePacketLockWaitNs.load(std::memory_order_relaxed);
		stats.rejectedPacketCount = state->rejectedPacketCount.load(std::memory_order_relaxed);
		stats.lastDelayFrame = state->lastDelayFrame.load(std::memory_order_relaxed);
		stats.maxDelayFrame = state->maxDelayFrame.load(std::memory_order_relaxed);
		return stats;
	}

	std::uint32_t FContentThread::GetWorkerIndex() const noexcept
	{
		return m_impl->workerIndex;
	}

	std::uint32_t FContentThread::GetCurrentWorkerIndex() noexcept
	{
		return g_currentWorkerIndex;
	}

	std::uint64_t FContentThread::GetApproxPendingWorkCount() const noexcept
	{
		return m_impl->pendingWorkCount.load(std::memory_order_relaxed);
	}

	bool FContentThread::EnqueueEnter(
		Core::SContentLifecycleEvent event)
	{
		std::shared_lock<std::shared_mutex> contentsLock(m_impl->contentsLock);
		auto stateIt = m_impl->contents.find(event.contentInstanceId);
		if (stateIt == m_impl->contents.end() || stateIt->second == nullptr || stateIt->second->content == nullptr)
		{
			return false;
		}

		Core::SContentExecutionState& state = *stateIt->second;
		state.enqueueEnterCallCount.fetch_add(1, std::memory_order_relaxed);
		const std::uint64_t queueDepth = state.enterQueueDepth.fetch_add(1, std::memory_order_relaxed) + 1;
		UpdateMaxAtomic(state.maxEnterQueueDepth, queueDepth);

		Core::SQueuedWorkItem workItem{};
		workItem.kind = Core::EQueuedWorkKind::Enter;
		workItem.enqueuedAt = std::chrono::steady_clock::now();
		workItem.lifecycleEvent = std::move(event);

		bool shouldWake = false;
		const auto mailboxLockWaitStart = std::chrono::steady_clock::now();
		{
			std::lock_guard<std::mutex> mailboxLock(state.mailbox.lock);
			const auto waitNs = ToNanoseconds(std::chrono::steady_clock::now() - mailboxLockWaitStart);
			state.enqueueEnterLockWaitNs.fetch_add(waitNs, std::memory_order_relaxed);
			UpdateMaxAtomic(state.maxEnqueueEnterLockWaitNs, waitNs);
			state.mailbox.items.push_back(std::move(workItem));
			if (!state.mailbox.readyQueued)
			{
				state.mailbox.readyQueued = true;
				shouldWake = true;
			}
		}

		m_impl->pendingWorkCount.fetch_add(1, std::memory_order_relaxed);
		if (shouldWake)
		{
			std::lock_guard<std::mutex> readyLock(m_impl->readyLock);
			m_impl->readyContentIds.push_back(state.contentInstanceId);
		}
		if (shouldWake)
		{
			m_impl->wakeCondition.notify_one();
		}
		return true;
	}

	bool FContentThread::EnqueueLeave(
		Core::SContentLifecycleEvent event)
	{
		std::shared_lock<std::shared_mutex> contentsLock(m_impl->contentsLock);
		auto stateIt = m_impl->contents.find(event.contentInstanceId);
		if (stateIt == m_impl->contents.end() || stateIt->second == nullptr || stateIt->second->content == nullptr)
		{
			return false;
		}

		Core::SContentExecutionState& state = *stateIt->second;
		state.enqueueLeaveCallCount.fetch_add(1, std::memory_order_relaxed);
		const std::uint64_t queueDepth = state.leaveQueueDepth.fetch_add(1, std::memory_order_relaxed) + 1;
		UpdateMaxAtomic(state.maxLeaveQueueDepth, queueDepth);

		Core::SQueuedWorkItem workItem{};
		workItem.kind = Core::EQueuedWorkKind::Leave;
		workItem.enqueuedAt = std::chrono::steady_clock::now();
		workItem.lifecycleEvent = std::move(event);

		bool shouldWake = false;
		const auto mailboxLockWaitStart = std::chrono::steady_clock::now();
		{
			std::lock_guard<std::mutex> mailboxLock(state.mailbox.lock);
			const auto waitNs = ToNanoseconds(std::chrono::steady_clock::now() - mailboxLockWaitStart);
			state.enqueueLeaveLockWaitNs.fetch_add(waitNs, std::memory_order_relaxed);
			UpdateMaxAtomic(state.maxEnqueueLeaveLockWaitNs, waitNs);
			state.mailbox.items.push_back(std::move(workItem));
			if (!state.mailbox.readyQueued)
			{
				state.mailbox.readyQueued = true;
				shouldWake = true;
			}
		}

		m_impl->pendingWorkCount.fetch_add(1, std::memory_order_relaxed);
		if (shouldWake)
		{
			std::lock_guard<std::mutex> readyLock(m_impl->readyLock);
			m_impl->readyContentIds.push_back(state.contentInstanceId);
		}
		if (shouldWake)
		{
			m_impl->wakeCondition.notify_one();
		}
		return true;
	}

	bool FContentThread::EnqueuePacket(
		Core::FOwnedPacketEnvelope packet)
	{
		return EnqueuePacketDetailed(std::move(packet)) == Core::EPacketEnqueueResult::Accepted;
	}

	Core::EPacketEnqueueResult FContentThread::EnqueuePacketDetailed(
		Core::FOwnedPacketEnvelope packet)
	{
		std::shared_lock<std::shared_mutex> contentsLock(m_impl->contentsLock);
		auto stateIt = m_impl->contents.find(packet.contentInstanceId);
		if (stateIt == m_impl->contents.end() || stateIt->second == nullptr || stateIt->second->content == nullptr)
		{
			return Core::EPacketEnqueueResult::InvalidTarget;
		}

		Core::SContentExecutionState& state = *stateIt->second;
		state.enqueuePacketCallCount.fetch_add(1, std::memory_order_relaxed);
		RunRaceInjection(m_impl->config, m_impl->raceInjectionCounter);
		const std::uint64_t traceSessionId = packet.sessionId;

		Core::SQueuedWorkItem workItem{};
		workItem.kind = Core::EQueuedWorkKind::Packet;
		workItem.enqueuedAt = std::chrono::steady_clock::now();
		workItem.packet = std::move(packet);
		bool shouldWake = false;
		std::uint64_t queueDepth = 0;
		const auto mailboxLockWaitStart = std::chrono::steady_clock::now();
		{
			std::lock_guard<std::mutex> mailboxLock(state.mailbox.lock);
			const auto waitNs = ToNanoseconds(std::chrono::steady_clock::now() - mailboxLockWaitStart);
			state.enqueuePacketLockWaitNs.fetch_add(waitNs, std::memory_order_relaxed);
			UpdateMaxAtomic(state.maxEnqueuePacketLockWaitNs, waitNs);
			const std::uint64_t currentPacketQueueDepth = state.packetQueueDepth.load(std::memory_order_relaxed);
			if (state.maxPacketQueueDepthLimit > 0 && currentPacketQueueDepth >= state.maxPacketQueueDepthLimit)
			{
				state.rejectedPacketCount.fetch_add(1, std::memory_order_relaxed);
				return Core::EPacketEnqueueResult::QueueFull;
			}

			queueDepth = state.packetQueueDepth.fetch_add(1, std::memory_order_relaxed) + 1;
			UpdateMaxAtomic(state.maxPacketQueueDepth, queueDepth);
			state.mailbox.items.push_back(std::move(workItem));
			if (!state.mailbox.readyQueued)
			{
				state.mailbox.readyQueued = true;
				shouldWake = true;
			}
		}
		if (m_impl->config.enableTraceLogging)
		{
			std::ostringstream oss;
			oss << "worker enqueue packet. workerIndex=" << m_impl->workerIndex << " contentInstanceId=" << state.contentInstanceId
				<< " queueDepth=" << queueDepth;
			TraceThread(m_impl->config, traceSessionId, oss.str());
		}

		m_impl->pendingWorkCount.fetch_add(1, std::memory_order_relaxed);
		RunRaceInjection(m_impl->config, m_impl->raceInjectionCounter);
		if (shouldWake)
		{
			std::lock_guard<std::mutex> readyLock(m_impl->readyLock);
			m_impl->readyContentIds.push_back(state.contentInstanceId);
		}
		if (shouldWake)
		{
			m_impl->wakeCondition.notify_one();
		}
		return Core::EPacketEnqueueResult::Accepted;
	}

	bool FContentThread::EnqueueMoveTransition(
		Core::SContentLifecycleEvent sourceLeaveEvent,
		Core::SContentLifecycleEvent targetEnterEvent)
	{
		std::shared_lock<std::shared_mutex> contentsLock(m_impl->contentsLock);
		const auto sourceIt = m_impl->contents.find(sourceLeaveEvent.contentInstanceId);
		const auto targetIt = m_impl->contents.find(targetEnterEvent.contentInstanceId);
		if (sourceIt == m_impl->contents.end() || targetIt == m_impl->contents.end() || sourceIt->second == nullptr ||
			targetIt->second == nullptr || sourceIt->second->content == nullptr || targetIt->second->content == nullptr)
		{
			return false;
		}

		auto sourceCompletion = std::move(sourceLeaveEvent.completionCallback);
		sourceLeaveEvent.completionCallback =
			[this, targetEnterEvent = std::move(targetEnterEvent), sourceCompletion = std::move(sourceCompletion)]() mutable
		{
			if (sourceCompletion)
			{
				sourceCompletion();
			}

			auto fallbackCompletion = targetEnterEvent.completionCallback;
			if (!this->EnqueueEnter(std::move(targetEnterEvent)) && fallbackCompletion)
			{
				fallbackCompletion();
			}
		};

		return EnqueueLeave(std::move(sourceLeaveEvent));
	}
}
