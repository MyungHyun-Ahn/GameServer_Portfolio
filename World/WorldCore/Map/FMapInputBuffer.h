#pragma once

namespace WorldCore
{
	struct SMoveCommand;
	struct SMoveRequestIdentity;

	class FMapInputBuffer final
	{
	public:
		FMapInputBuffer();
		~FMapInputBuffer();

		FMapInputBuffer(const FMapInputBuffer&) = delete;
		FMapInputBuffer& operator=(const FMapInputBuffer&) = delete;

		[[nodiscard]] bool EnqueueMove(const SMoveCommand& command);
		[[nodiscard]] bool EnqueuePlayerAttack(const SPlayerAttackCommand& command);
		[[nodiscard]] std::vector<SMoveCommand> BeginTick();
		[[nodiscard]] std::vector<SPlayerAttackCommand> BeginAttackTick();
		[[nodiscard]] std::optional<SMoveRequestIdentity> DiscardPendingMove(FEntityId entityId);
		void RemoveEntity(FEntityId entityId);
		void Clear();
		[[nodiscard]] std::size_t GetPendingMoveCount() const noexcept;
		[[nodiscard]] std::size_t GetPendingAttackCount() const noexcept;

	private:
		struct SImpl;
		std::unique_ptr<SImpl> m_impl;
	};
}
