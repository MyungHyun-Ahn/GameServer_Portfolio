#pragma once

namespace Foundation::Ids
{
	class IIdAllocator;
}

namespace ContentsRuntime::Core
{
	class FContentInstanceIdAllocator
	{
	public:
		using FAllocatorFactory = std::function<std::shared_ptr<Foundation::Ids::IIdAllocator>()>;

		explicit FContentInstanceIdAllocator(FAllocatorFactory allocatorFactory = {});

		FContentInstanceId Allocate(FContentId contentId, std::uint8_t reserveBits = kDefaultContentInstanceReserveBits);
		void Release(FContentInstanceId contentInstanceId);

	private:
		std::shared_ptr<Foundation::Ids::IIdAllocator> FindOrCreateSequenceAllocator(FContentId contentId);

	private:
		FAllocatorFactory m_allocatorFactory;
		std::mutex m_lock;
		std::unordered_map<FContentId, std::shared_ptr<Foundation::Ids::IIdAllocator>> m_sequenceAllocators;
	};
}
