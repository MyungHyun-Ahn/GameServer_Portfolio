#pragma once

namespace NetworkLib
{
	class IServer;
}

namespace ContentsRuntime::Core
{
	class IContent;
	struct SContentExecutionState;
}

namespace ContentsRuntime::Routing
{
	class FContentRuntime final : public Bridge::IContentBridge
	{
	public:
		FContentRuntime();
		~FContentRuntime() override;

		bool RegisterContent(std::unique_ptr<Core::IContent> content);
		void SetConfig(const Core::SContentRuntimeConfig& config);
		void Start(NetworkLib::IServer& server);
		void Stop();
		Core::SContentRuntimeStats GetStatsSnapshot();

		bool EnterSession(std::uint64_t sessionId, Core::FContentId initialContentId);
		bool EnterSessionToInstance(std::uint64_t sessionId, Core::FContentInstanceId initialContentInstanceId);
		void LeaveSession(std::uint64_t sessionId);
		bool EnqueuePacket(std::uint64_t sessionId, std::uint16_t opcode, const char* payload, std::int32_t payloadLength);

	public:
		bool SendPacket(std::uint64_t sessionId, NetworkLib::Packet::Serialization::FOutgoingContentPacket&& packet) override;
		Core::EPacketEnqueueResult EnqueuePacketToInstance(std::uint64_t sessionId,
			std::uint64_t routeGeneration,
			Core::FContentInstanceId targetContentInstanceId,
			std::uint16_t opcode,
			std::span<const char> payload) override;
		bool EnqueueCompletionToInstance(Core::FContentInstanceId contentInstanceId, std::function<void()> completion) override;
		bool MoveSession(std::uint64_t sessionId, Core::FContentId targetContentId) override;
		bool MoveSessionToInstance(std::uint64_t sessionId, Core::FContentInstanceId targetContentInstanceId) override;
		bool MoveSessionWithCompletion(std::uint64_t sessionId,
			Core::FContentId targetContentId,
			Core::FTransitionCompletionCallback onCompleted) override;
		bool MoveSessionToInstanceWithCompletion(std::uint64_t sessionId,
			Core::FContentInstanceId targetContentInstanceId,
			Core::FTransitionCompletionCallback onCompleted) override;
		bool RequestContentInstanceTransfer(Core::FContentInstanceId contentInstanceId, std::uint32_t targetWorkerIndex);
		bool TryScheduleDelegateTransfer(Core::FContentInstanceId contentInstanceId, std::uint32_t sourceWorkerIndex);
		bool TryScheduleWorkSteal(std::uint32_t idleWorkerIndex);
		bool CommitRequestedTransferAtWorkBoundary(Core::SContentExecutionState& executionState, std::uint32_t sourceWorkerIndex);
		bool DisconnectSession(std::uint64_t sessionId) override;
		bool IsSessionAlive(std::uint64_t sessionId) const override;
		bool HasContentInstance(Core::FContentInstanceId contentInstanceId) const override;
		std::optional<Core::FContentId> GetCurrentContentId(std::uint64_t sessionId) const override;
		std::optional<Core::FContentInstanceId> GetCurrentContentInstanceId(std::uint64_t sessionId) const override;

	private:
		bool HasPendingMoveForContentLocked(Core::FContentInstanceId contentInstanceId) const;

	private:
		struct SImpl;
		std::unique_ptr<SImpl> m_impl;
	};
}
