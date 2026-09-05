#pragma once

namespace WorldCore
{
	class FSectorGrid;
	class FSectorTickPlan;

	class FMapTickExecution final
	{
	public:
		FMapTickExecution();
		~FMapTickExecution();

		FMapTickExecution(const FMapTickExecution&) = delete;
		FMapTickExecution& operator=(const FMapTickExecution&) = delete;

		[[nodiscard]] bool Begin(const SMapTickTicket& ticket,
			const FSectorGrid& sectorGrid,
			std::vector<SSectorTask> tasks,
			std::vector<SMoveRequestIdentity> consumedMoveRequests,
			std::vector<SPlayerAttackRequestIdentity> consumedAttackRequests);
		[[nodiscard]] EMapTickCompletionResult Complete(SMapTickExecutionCompletion completion) noexcept;
		void Reset();

		[[nodiscard]] EMapTickExecutionState GetState() const noexcept;
		[[nodiscard]] const SMapTickTicket& GetTicket() const noexcept;
		[[nodiscard]] const FSectorTickPlan& GetTickPlan() const noexcept;
		[[nodiscard]] std::span<const SSectorTaskOutput> GetTaskOutputs() const noexcept;
		[[nodiscard]] std::span<const SMoveRequestIdentity> GetConsumedMoveRequests() const noexcept;
		[[nodiscard]] std::span<const SPlayerAttackRequestIdentity> GetConsumedAttackRequests() const noexcept;
		[[nodiscard]] std::string_view GetFailureReason() const noexcept;

	private:
		struct SImpl;
		std::unique_ptr<SImpl> m_impl;
	};
}
