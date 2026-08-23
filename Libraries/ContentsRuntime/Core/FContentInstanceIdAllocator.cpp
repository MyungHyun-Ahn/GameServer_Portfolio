#include "ContentsRuntimePch.h"

#include "ContentsRuntime/Core/FContentInstanceIdAllocator.h"

#include "Foundation/Ids/FDefaultIncrementIdAllocator.h"

namespace ContentsRuntime::Core
{
	FContentInstanceIdAllocator::FContentInstanceIdAllocator(
		FAllocatorFactory allocatorFactory)
		: m_allocatorFactory(std::move(allocatorFactory))
	{
		if (!m_allocatorFactory)
		{
			m_allocatorFactory = []()
			{
				return std::make_shared<Foundation::Ids::FDefaultIncrementIdAllocator>();
			};
		}
	}

	FContentInstanceId FContentInstanceIdAllocator::Allocate(
		const FContentId contentId,
		const std::uint8_t reserveBits)
	{
		if (contentId == kInvalidContentId)
		{
			return kInvalidContentInstanceId;
		}

		const std::shared_ptr<Foundation::Ids::IIdAllocator> sequenceAllocator = FindOrCreateSequenceAllocator(contentId);
		if (sequenceAllocator == nullptr)
		{
			return kInvalidContentInstanceId;
		}

		const Foundation::Ids::FAllocatedId sequence = sequenceAllocator->Allocate();
		if (sequence == Foundation::Ids::kInvalidAllocatedId)
		{
			return kInvalidContentInstanceId;
		}

		return MakeContentInstanceId(contentId, reserveBits, sequence);
	}

	void FContentInstanceIdAllocator::Release(
		const FContentInstanceId contentInstanceId)
	{
		if (!IsValidContentInstanceId(contentInstanceId))
		{
			return;
		}

		const FContentId contentId = ExtractContentId(contentInstanceId);
		const std::uint64_t sequence = ExtractContentInstanceSequence(contentInstanceId);
		std::shared_ptr<Foundation::Ids::IIdAllocator> sequenceAllocator;
		{
			std::lock_guard<std::mutex> lock(m_lock);
			const auto allocatorIt = m_sequenceAllocators.find(contentId);
			if (allocatorIt == m_sequenceAllocators.end())
			{
				return;
			}

			sequenceAllocator = allocatorIt->second;
		}

		if (sequenceAllocator != nullptr)
		{
			sequenceAllocator->Release(sequence);
		}
	}

	std::shared_ptr<Foundation::Ids::IIdAllocator> FContentInstanceIdAllocator::FindOrCreateSequenceAllocator(
		const FContentId contentId)
	{
		std::lock_guard<std::mutex> lock(m_lock);
		const auto allocatorIt = m_sequenceAllocators.find(contentId);
		if (allocatorIt != m_sequenceAllocators.end())
		{
			return allocatorIt->second;
		}

		std::shared_ptr<Foundation::Ids::IIdAllocator> sequenceAllocator = m_allocatorFactory();
		if (sequenceAllocator == nullptr)
		{
			return nullptr;
		}

		m_sequenceAllocators.emplace(contentId, sequenceAllocator);
		return sequenceAllocator;
	}
}
