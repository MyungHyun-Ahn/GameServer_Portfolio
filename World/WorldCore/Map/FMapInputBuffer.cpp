#include "WorldCorePch.h"

#include "WorldCore/Map/FMapInputBuffer.h"

namespace WorldCore
{
	struct FMapInputBuffer::SImpl final
	{
		std::unordered_map<FEntityId, SMoveCommand> pendingMoves;
		std::unordered_map<FEntityId, FMoveSequence> latestSequences;
		std::vector<SPlayerAttackCommand> pendingAttacks;
		std::unordered_map<FEntityId, FAttackSequence> latestAttackSequences;
	};

	FMapInputBuffer::FMapInputBuffer()
		: m_impl(std::make_unique<SImpl>())
	{
	}

	bool FMapInputBuffer::EnqueuePlayerAttack(
		const SPlayerAttackCommand& command)
	{
		if (command.attackerEntityId == kInvalidEntityId || command.attackSequence == 0 || command.targetEntityId == kInvalidEntityId)
		{
			return false;
		}

		const auto latest = m_impl->latestAttackSequences.find(command.attackerEntityId);
		if (latest != m_impl->latestAttackSequences.end() && command.attackSequence <= latest->second)
		{
			return false;
		}

		m_impl->latestAttackSequences[command.attackerEntityId] = command.attackSequence;
		m_impl->pendingAttacks.push_back(command);
		return true;
	}

	FMapInputBuffer::~FMapInputBuffer() = default;

	bool FMapInputBuffer::EnqueueMove(
		const SMoveCommand& command)
	{
		if (command.entityId == kInvalidEntityId || command.sequence == 0)
		{
			return false;
		}

		const auto latest = m_impl->latestSequences.find(command.entityId);
		if (latest != m_impl->latestSequences.end() && command.sequence <= latest->second)
		{
			return false;
		}

		m_impl->latestSequences[command.entityId] = command.sequence;
		m_impl->pendingMoves[command.entityId] = command;
		return true;
	}

	std::vector<SMoveCommand> FMapInputBuffer::BeginTick()
	{
		std::vector<SMoveCommand> commands;
		commands.reserve(m_impl->pendingMoves.size());
		for (auto& [entityId, command] : m_impl->pendingMoves)
		{
			(void)entityId;
			commands.push_back(std::move(command));
		}
		m_impl->pendingMoves.clear();

		std::sort(commands.begin(),
			commands.end(),
			[](const SMoveCommand& lhs, const SMoveCommand& rhs)
			{
				return lhs.entityId < rhs.entityId || (lhs.entityId == rhs.entityId && lhs.sequence < rhs.sequence);
			});
		return commands;
	}

	std::vector<SPlayerAttackCommand> FMapInputBuffer::BeginAttackTick()
	{
		std::vector<SPlayerAttackCommand> commands = std::move(m_impl->pendingAttacks);
		m_impl->pendingAttacks.clear();
		std::sort(commands.begin(),
			commands.end(),
			[](const SPlayerAttackCommand& lhs, const SPlayerAttackCommand& rhs)
			{
				return lhs.attackerEntityId < rhs.attackerEntityId ||
					   (lhs.attackerEntityId == rhs.attackerEntityId && lhs.attackSequence < rhs.attackSequence);
			});
		return commands;
	}

	std::optional<SMoveRequestIdentity> FMapInputBuffer::DiscardPendingMove(
		const FEntityId entityId)
	{
		const auto pending = m_impl->pendingMoves.find(entityId);
		if (pending == m_impl->pendingMoves.end())
		{
			return std::nullopt;
		}

		const SMoveRequestIdentity discarded{entityId, pending->second.sequence};
		m_impl->pendingMoves.erase(pending);
		return discarded;
	}

	void FMapInputBuffer::RemoveEntity(
		const FEntityId entityId)
	{
		m_impl->pendingMoves.erase(entityId);
		m_impl->latestSequences.erase(entityId);
		m_impl->latestAttackSequences.erase(entityId);
		std::erase_if(m_impl->pendingAttacks,
			[entityId](const SPlayerAttackCommand& command)
			{
				return command.attackerEntityId == entityId;
			});
	}

	void FMapInputBuffer::Clear()
	{
		m_impl->pendingMoves.clear();
		m_impl->latestSequences.clear();
		m_impl->pendingAttacks.clear();
		m_impl->latestAttackSequences.clear();
	}

	std::size_t FMapInputBuffer::GetPendingMoveCount() const noexcept
	{
		return m_impl->pendingMoves.size();
	}

	std::size_t FMapInputBuffer::GetPendingAttackCount() const noexcept
	{
		return m_impl->pendingAttacks.size();
	}
}
