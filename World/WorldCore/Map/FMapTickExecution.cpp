#include "WorldCorePch.h"

#include "WorldCore/Map/FMapTickExecution.h"
#include "WorldCore/Map/Sector/FSectorGrid.h"
#include "WorldCore/Map/Sector/FSectorTickPlan.h"

namespace WorldCore
{
	namespace
	{
		bool IsValidTicket(
			const SMapTickTicket& ticket) noexcept
		{
			return ticket.mapInstanceId != kInvalidMapInstanceId && ticket.mapIncarnation != kInvalidMapIncarnation &&
				   ticket.tickIndex != 0 && ticket.generation != kInvalidMapTickGeneration;
		}

		bool ValidateOutputs(
			const FSectorTickPlan& tickPlan,
			std::vector<SSectorTaskOutput>& outputs,
			std::string& outError)
		{
			if (outputs.size() != tickPlan.GetTaskCount())
			{
				outError = "Sector execution output count does not match the active Tick Plan.";
				return false;
			}

			std::sort(outputs.begin(),
				outputs.end(),
				[](const SSectorTaskOutput& lhs, const SSectorTaskOutput& rhs)
				{
					return lhs.stableOrder < rhs.stableOrder;
				});

			for (std::size_t index = 0; index < outputs.size(); ++index)
			{
				const std::uint32_t expectedStableOrder = static_cast<std::uint32_t>(index);
				const FSectorId expectedSectorId = static_cast<FSectorId>(index);
				if (outputs[index].stableOrder != expectedStableOrder || outputs[index].sectorId != expectedSectorId)
				{
					outError = "Sector execution output identity does not match the active Tick Plan.";
					return false;
				}
			}
			return true;
		}
	}

	struct FMapTickExecution::SImpl final
	{
		EMapTickExecutionState state = EMapTickExecutionState::Idle;
		SMapTickTicket ticket{};
		FSectorTickPlan tickPlan;
		std::vector<SSectorTaskOutput> taskOutputs;
		std::vector<SMoveRequestIdentity> consumedMoveRequests;
		std::vector<SPlayerAttackRequestIdentity> consumedAttackRequests;
		std::string failureReason;
	};

	FMapTickExecution::FMapTickExecution()
		: m_impl(std::make_unique<SImpl>())
	{
	}

	FMapTickExecution::~FMapTickExecution() = default;

	bool FMapTickExecution::Begin(
		const SMapTickTicket& ticket,
		const FSectorGrid& sectorGrid,
		std::vector<SSectorTask> tasks,
		std::vector<SMoveRequestIdentity> consumedMoveRequests,
		std::vector<SPlayerAttackRequestIdentity> consumedAttackRequests)
	{
		if (m_impl->state != EMapTickExecutionState::Idle)
		{
			return false;
		}

		m_impl->ticket = ticket;
		m_impl->consumedMoveRequests = std::move(consumedMoveRequests);
		m_impl->consumedAttackRequests = std::move(consumedAttackRequests);
		m_impl->failureReason.clear();
		m_impl->taskOutputs.clear();
		if (!IsValidTicket(ticket))
		{
			m_impl->failureReason = "Map Tick ticket is invalid.";
			m_impl->state = EMapTickExecutionState::Failed;
			return true;
		}
		if (!m_impl->tickPlan.Build(sectorGrid, std::move(tasks), m_impl->failureReason))
		{
			m_impl->state = EMapTickExecutionState::Failed;
			return true;
		}

		m_impl->state = EMapTickExecutionState::Executing;
		return true;
	}

	EMapTickCompletionResult FMapTickExecution::Complete(
		SMapTickExecutionCompletion completion) noexcept
	{
		if (m_impl->state == EMapTickExecutionState::Idle)
		{
			return EMapTickCompletionResult::NotExecuting;
		}
		if (!(completion.ticket == m_impl->ticket))
		{
			return EMapTickCompletionResult::StaleTicket;
		}
		if (m_impl->state != EMapTickExecutionState::Executing)
		{
			return EMapTickCompletionResult::DuplicateCompletion;
		}

		if (completion.status == EMapTickCompletionStatus::Failed)
		{
			m_impl->state = EMapTickExecutionState::Failed;
			try
			{
				m_impl->failureReason =
					completion.failureReason.empty() ? "Sector execution failed without a reason." : completion.failureReason;
			}
			catch (...)
			{
				m_impl->failureReason.clear();
			}
			return EMapTickCompletionResult::Accepted;
		}

		try
		{
			if (!ValidateOutputs(m_impl->tickPlan, completion.taskOutputs, m_impl->failureReason))
			{
				m_impl->state = EMapTickExecutionState::Failed;
				return EMapTickCompletionResult::Accepted;
			}

			m_impl->taskOutputs = std::move(completion.taskOutputs);
			m_impl->state = EMapTickExecutionState::ReadyToCommit;
		}
		catch (...)
		{
			m_impl->state = EMapTickExecutionState::Failed;
			try
			{
				m_impl->failureReason = "Sector execution completion validation raised an exception.";
			}
			catch (...)
			{
				m_impl->failureReason.clear();
			}
		}
		return EMapTickCompletionResult::Accepted;
	}

	void FMapTickExecution::Reset()
	{
		m_impl->state = EMapTickExecutionState::Idle;
		m_impl->ticket = {};
		m_impl->tickPlan.Clear();
		m_impl->taskOutputs.clear();
		m_impl->consumedMoveRequests.clear();
		m_impl->consumedAttackRequests.clear();
		m_impl->failureReason.clear();
	}

	EMapTickExecutionState FMapTickExecution::GetState() const noexcept
	{
		return m_impl->state;
	}

	const SMapTickTicket& FMapTickExecution::GetTicket() const noexcept
	{
		return m_impl->ticket;
	}

	const FSectorTickPlan& FMapTickExecution::GetTickPlan() const noexcept
	{
		return m_impl->tickPlan;
	}

	std::span<const SSectorTaskOutput> FMapTickExecution::GetTaskOutputs() const noexcept
	{
		return m_impl->taskOutputs;
	}

	std::span<const SMoveRequestIdentity> FMapTickExecution::GetConsumedMoveRequests() const noexcept
	{
		return m_impl->consumedMoveRequests;
	}

	std::span<const SPlayerAttackRequestIdentity> FMapTickExecution::GetConsumedAttackRequests() const noexcept
	{
		return m_impl->consumedAttackRequests;
	}

	std::string_view FMapTickExecution::GetFailureReason() const noexcept
	{
		return m_impl->failureReason;
	}
}
