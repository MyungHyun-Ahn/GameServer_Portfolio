#include "FoundationPch.h"

#include "Foundation/Ids/FDefaultIncrementIdAllocator.h"

namespace Foundation::Ids
{
	FDefaultIncrementIdAllocator::FDefaultIncrementIdAllocator(
		const FAllocatedId initialValue) noexcept
		: m_nextId(initialValue)
	{
	}

	FAllocatedId FDefaultIncrementIdAllocator::Allocate()
	{
		const FAllocatedId allocatedId = m_nextId.fetch_add(1, std::memory_order_relaxed) + 1;
		if (allocatedId == kInvalidAllocatedId)
		{
			return kInvalidAllocatedId;
		}

		return allocatedId;
	}

	void FDefaultIncrementIdAllocator::Release(
		const FAllocatedId allocatedId)
	{
		(void)allocatedId;
	}
}
