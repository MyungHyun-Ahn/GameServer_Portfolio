#include "WorldCorePch.h"

#include "WorldCore/Map/Sector/FSectorDeferredCommandBuffer.h"
#include "WorldCore/Map/Sector/FSectorGrid.h"

namespace WorldCore
{
	struct FSectorDeferredCommandBuffer::SImpl final
	{
		std::vector<SSectorTransferCommand> transfers;
	};

	FSectorDeferredCommandBuffer::FSectorDeferredCommandBuffer()
		: m_impl(std::make_unique<SImpl>())
	{
	}

	FSectorDeferredCommandBuffer::~FSectorDeferredCommandBuffer() = default;

	void FSectorDeferredCommandBuffer::Build(
		const std::span<const SSectorTaskOutput> taskOutputs)
	{
		m_impl->transfers.clear();
		for (const SSectorTaskOutput& output : taskOutputs)
		{
			m_impl->transfers.insert(m_impl->transfers.end(), output.sectorTransfers.begin(), output.sectorTransfers.end());
		}
		std::sort(m_impl->transfers.begin(),
			m_impl->transfers.end(),
			[](const auto& lhs, const auto& rhs)
			{
				return lhs.entityId < rhs.entityId || (lhs.entityId == rhs.entityId && lhs.targetSectorId < rhs.targetSectorId);
			});
	}

	bool FSectorDeferredCommandBuffer::Validate(
		const FSectorGrid& sectorGrid,
		std::string& outError) const
	{
		outError.clear();
		FEntityId previousEntityId = kInvalidEntityId;
		for (const SSectorTransferCommand& transfer : m_impl->transfers)
		{
			if (previousEntityId == transfer.entityId)
			{
				outError = "An Entity has more than one deferred Sector transfer in the same Tick.";
				return false;
			}
			previousEntityId = transfer.entityId;

			const auto currentSectorId = sectorGrid.GetEntitySectorId(transfer.entityId);
			if (!currentSectorId.has_value() || *currentSectorId != transfer.sourceSectorId ||
				!sectorGrid.IsValidSectorId(transfer.targetSectorId) || transfer.sourceSectorId == transfer.targetSectorId)
			{
				outError = "Deferred Sector transfer no longer matches the current Grid state.";
				return false;
			}
		}
		return true;
	}

	bool FSectorDeferredCommandBuffer::Commit(
		FSectorGrid& sectorGrid,
		std::string& outError)
	{
		if (!Validate(sectorGrid, outError))
		{
			return false;
		}

		std::size_t committedTransferCount = 0;
		for (const SSectorTransferCommand& transfer : m_impl->transfers)
		{
			if (!sectorGrid.TransferEntity(transfer.entityId, transfer.sourceSectorId, transfer.targetSectorId))
			{
				bool rollbackSucceeded = true;
				while (committedTransferCount > 0)
				{
					const SSectorTransferCommand& committedTransfer = m_impl->transfers[--committedTransferCount];
					if (!sectorGrid.TransferEntity(
							committedTransfer.entityId, committedTransfer.targetSectorId, committedTransfer.sourceSectorId))
					{
						rollbackSucceeded = false;
					}
				}
				outError = rollbackSucceeded ? "Deferred Sector transfer failed during Commit."
											 : "Deferred Sector transfer failed and rollback could not restore the Grid.";
				m_impl->transfers.clear();
				return false;
			}
			++committedTransferCount;
		}
		m_impl->transfers.clear();
		return true;
	}

	void FSectorDeferredCommandBuffer::Clear()
	{
		m_impl->transfers.clear();
	}

	std::size_t FSectorDeferredCommandBuffer::GetTransferCount() const noexcept
	{
		return m_impl->transfers.size();
	}
}
