#include "NetLibPch.h"

#include "Packet/Buffer/FSendSegmentPool.h"
#include "Containers/LockFreeCommon.h"

namespace NetworkLib::Packet::Buffer
{
	namespace
	{
		struct SFreeNode
		{
			std::uint32_t regionIndex = FSendSegmentPool::kInvalidIndex;
			std::uint32_t slotIndex = FSendSegmentPool::kInvalidIndex;
			SFreeNode* nextLocal = nullptr;
			volatile LONG64 nextShared = 0;
		};

		struct SRegion
		{
			std::unique_ptr<char[]> storage;
			std::unique_ptr<SFreeNode[]> freeNodes;
			std::uint32_t bucketIndex = FSendSegmentPool::kInvalidIndex;
			std::uint32_t slotSize = 0;
			std::uint32_t slotCount = 0;
			RIO_BUFFERID rioBufferId = RIO_INVALID_BUFFERID;
		};

		struct SBucket
		{
			std::size_t slotSize = 0;
			volatile LONG64 sharedTop = 0;
			std::vector<std::uint32_t> regionIndices;
			std::atomic<std::uint64_t> inUseSlotCount = 0;
		};

		struct SBucketLocalCache
		{
			SFreeNode* freeList = nullptr;
			int cachedCount = 0;
		};

		struct SThreadLocalCache
		{
			std::uint32_t generation = 0;
			std::array<SBucketLocalCache, 6> buckets{};
		};

		inline constexpr std::array<std::size_t, 6> kBucketSizes{256, 512, 1024, 2048, 4096, 8192};

		inline constexpr int kLocalCacheTarget = 16;
		inline constexpr int kLocalCacheLimit = 32;

		std::mutex g_poolMutex;
		std::array<SBucket, kBucketSizes.size()> g_buckets{};
		std::vector<SRegion> g_regions{};
		std::size_t g_regionSizeBytes = FSendSegmentPool::kDefaultRegionSizeBytes;
		std::uint32_t g_regionsPerBucket = 8;
		std::atomic<bool> g_initialized = false;
		std::atomic<bool> g_registerForRio = false;
		std::atomic<std::uint64_t> g_allocationFailureCount = 0;
		std::atomic<std::uint32_t> g_activeGeneration = 0;
		DWORD g_flsIndex = FLS_OUT_OF_INDEXES;

		void ResetThreadLocalCacheWithoutFlush(
			SThreadLocalCache& localCache) noexcept
		{
			for (SBucketLocalCache& localBucket : localCache.buckets)
			{
				localBucket.freeList = nullptr;
				localBucket.cachedCount = 0;
			}
		}

		void PushSharedNode(
			SBucket& bucket,
			SFreeNode* node) noexcept
		{
			while (true)
			{
				const std::uint64_t observedTop = NetworkLib::Containers::AtomicLoad64(&bucket.sharedTop);
				node->nextLocal = nullptr;
				node->nextShared = static_cast<LONG64>(observedTop);
				const std::uint64_t newTop =
					NetworkLib::Containers::MakeTaggedPointer(NetworkLib::Containers::GetTag(observedTop) + 1, node);
				if (NetworkLib::Containers::AtomicCompareExchange64(&bucket.sharedTop, newTop, observedTop) == observedTop)
				{
					return;
				}
			}
		}

		SFreeNode* PopSharedNode(
			SBucket& bucket) noexcept
		{
			while (true)
			{
				const std::uint64_t observedTop = NetworkLib::Containers::AtomicLoad64(&bucket.sharedTop);
				SFreeNode* topNode = NetworkLib::Containers::GetPointer<SFreeNode>(observedTop);
				if (topNode == nullptr)
				{
					return nullptr;
				}

				const std::uint64_t next = NetworkLib::Containers::AtomicLoad64(&topNode->nextShared);
				const std::uint64_t newTop = NetworkLib::Containers::MakeTaggedPointer(
					NetworkLib::Containers::GetTag(observedTop) + 1, NetworkLib::Containers::GetPointer<void>(next));
				if (NetworkLib::Containers::AtomicCompareExchange64(&bucket.sharedTop, newTop, observedTop) == observedTop)
				{
					topNode->nextShared = 0;
					return topNode;
				}
			}
		}

		void FlushLocalBucketToShared(
			SBucket& bucket,
			SBucketLocalCache& localBucket) noexcept
		{
			while (localBucket.freeList != nullptr)
			{
				SFreeNode* node = localBucket.freeList;
				localBucket.freeList = node->nextLocal;
				node->nextLocal = nullptr;
				--localBucket.cachedCount;
				PushSharedNode(bucket, node);
			}
		}

		void TrimLocalBucketToTarget(
			SBucket& bucket,
			SBucketLocalCache& localBucket) noexcept
		{
			while (localBucket.cachedCount > kLocalCacheTarget)
			{
				SFreeNode* node = localBucket.freeList;
				localBucket.freeList = node->nextLocal;
				node->nextLocal = nullptr;
				--localBucket.cachedCount;
				PushSharedNode(bucket, node);
			}
		}

		void RefillLocalBucketFromShared(
			SBucket& bucket,
			SBucketLocalCache& localBucket) noexcept
		{
			while (localBucket.cachedCount < kLocalCacheTarget)
			{
				SFreeNode* node = PopSharedNode(bucket);
				if (node == nullptr)
				{
					break;
				}

				node->nextLocal = localBucket.freeList;
				localBucket.freeList = node;
				++localBucket.cachedCount;
			}
		}

		void CALLBACK ReleaseThreadLocalCache(
			void* value) noexcept
		{
			SThreadLocalCache* localCache = reinterpret_cast<SThreadLocalCache*>(value);
			if (localCache == nullptr)
			{
				return;
			}

			const std::uint32_t activeGeneration = g_activeGeneration.load(std::memory_order_acquire);
			if (g_initialized.load(std::memory_order_acquire) && activeGeneration != 0 && localCache->generation == activeGeneration)
			{
				for (std::size_t bucketIndex = 0; bucketIndex < g_buckets.size(); ++bucketIndex)
				{
					FlushLocalBucketToShared(g_buckets[bucketIndex], localCache->buckets[bucketIndex]);
				}
			}

			delete localCache;
		}

		SThreadLocalCache& GetOrCreateThreadLocalCache() noexcept
		{
			SThreadLocalCache* localCache = reinterpret_cast<SThreadLocalCache*>(FlsGetValue(g_flsIndex));
			if (localCache == nullptr)
			{
				localCache = new (std::nothrow) SThreadLocalCache();
				if (localCache == nullptr)
				{
					__debugbreak();
				}

				FlsSetValue(g_flsIndex, localCache);
			}

			const std::uint32_t activeGeneration = g_activeGeneration.load(std::memory_order_acquire);
			if (localCache->generation != activeGeneration)
			{
				ResetThreadLocalCacheWithoutFlush(*localCache);
				localCache->generation = activeGeneration;
			}

			return *localCache;
		}

		void ResetBucketsUnlocked() noexcept
		{
			for (std::size_t bucketIndex = 0; bucketIndex < g_buckets.size(); ++bucketIndex)
			{
				g_buckets[bucketIndex].slotSize = kBucketSizes[bucketIndex];
				g_buckets[bucketIndex].sharedTop = 0;
				g_buckets[bucketIndex].regionIndices.clear();
				g_buckets[bucketIndex].inUseSlotCount.store(0, std::memory_order_relaxed);
			}
		}

		void ClearRegionsUnlocked(
			const RIO_EXTENSION_FUNCTION_TABLE* rioFunctionTable) noexcept
		{
			if (g_registerForRio.load(std::memory_order_relaxed) && rioFunctionTable != nullptr)
			{
				for (SRegion& region : g_regions)
				{
					if (region.rioBufferId != RIO_INVALID_BUFFERID)
					{
						rioFunctionTable->RIODeregisterBuffer(region.rioBufferId);
						region.rioBufferId = RIO_INVALID_BUFFERID;
					}
				}
			}

			g_regions.clear();
		}

		std::uint32_t FindBucketIndex(
			const std::size_t requiredLength) noexcept
		{
			for (std::uint32_t bucketIndex = 0; bucketIndex < static_cast<std::uint32_t>(kBucketSizes.size()); ++bucketIndex)
			{
				if (requiredLength <= kBucketSizes[bucketIndex])
				{
					return bucketIndex;
				}
			}

			return FSendSegmentPool::kInvalidIndex;
		}
	}

	void FSendSegmentPool::Configure(
		const std::size_t regionSizeBytes,
		const std::uint32_t regionsPerBucket) noexcept
	{
		std::scoped_lock poolLock(g_poolMutex);
		g_regionSizeBytes = regionSizeBytes == 0 ? kDefaultRegionSizeBytes : regionSizeBytes;
		g_regionsPerBucket = std::max(1u, regionsPerBucket);
	}

	bool FSendSegmentPool::Initialize(
		const bool registerForRio,
		const RIO_EXTENSION_FUNCTION_TABLE* rioFunctionTable) noexcept
	{
		std::scoped_lock poolLock(g_poolMutex);
		if (g_initialized.load(std::memory_order_relaxed))
		{
			return true;
		}

		if (g_flsIndex == FLS_OUT_OF_INDEXES)
		{
			g_flsIndex = FlsAlloc(&ReleaseThreadLocalCache);
			if (g_flsIndex == FLS_OUT_OF_INDEXES)
			{
				return false;
			}
		}

		ResetBucketsUnlocked();
		g_regions.clear();
		g_allocationFailureCount.store(0, std::memory_order_relaxed);
		g_registerForRio.store(registerForRio, std::memory_order_relaxed);

		const std::uint32_t generation = g_activeGeneration.fetch_add(1, std::memory_order_acq_rel) + 1;

		for (std::uint32_t bucketIndex = 0; bucketIndex < static_cast<std::uint32_t>(g_buckets.size()); ++bucketIndex)
		{
			SBucket& bucket = g_buckets[bucketIndex];
			const std::uint32_t slotSize = static_cast<std::uint32_t>(bucket.slotSize);
			const std::uint32_t slotCount = static_cast<std::uint32_t>(g_regionSizeBytes / bucket.slotSize);
			if (slotCount == 0)
			{
				continue;
			}

			bucket.regionIndices.reserve(g_regionsPerBucket);

			for (std::uint32_t regionOrdinal = 0; regionOrdinal < g_regionsPerBucket; ++regionOrdinal)
			{
				SRegion region{};
				region.storage = std::make_unique<char[]>(g_regionSizeBytes);
				region.freeNodes = std::make_unique<SFreeNode[]>(slotCount);
				region.bucketIndex = bucketIndex;
				region.slotSize = slotSize;
				region.slotCount = slotCount;

				if (registerForRio)
				{
					if (rioFunctionTable == nullptr)
					{
						ClearRegionsUnlocked(rioFunctionTable);
						ResetBucketsUnlocked();
						g_registerForRio.store(false, std::memory_order_relaxed);
						g_activeGeneration.store(0, std::memory_order_release);
						return false;
					}

					region.rioBufferId = rioFunctionTable->RIORegisterBuffer(region.storage.get(), static_cast<DWORD>(g_regionSizeBytes));
					if (region.rioBufferId == RIO_INVALID_BUFFERID)
					{
						ClearRegionsUnlocked(rioFunctionTable);
						ResetBucketsUnlocked();
						g_registerForRio.store(false, std::memory_order_relaxed);
						g_activeGeneration.store(0, std::memory_order_release);
						return false;
					}
				}

				const std::uint32_t regionIndex = static_cast<std::uint32_t>(g_regions.size());
				g_regions.push_back(std::move(region));
				bucket.regionIndices.push_back(regionIndex);

				SRegion& storedRegion = g_regions.back();
				for (std::uint32_t slotIndex = 0; slotIndex < slotCount; ++slotIndex)
				{
					SFreeNode& freeNode = storedRegion.freeNodes[slotIndex];
					freeNode.regionIndex = regionIndex;
					freeNode.slotIndex = slotIndex;
					freeNode.nextLocal = nullptr;
					freeNode.nextShared = 0;
					PushSharedNode(bucket, &freeNode);
				}
			}
		}

		g_activeGeneration.store(generation, std::memory_order_release);
		g_initialized.store(true, std::memory_order_release);
		return true;
	}

	void FSendSegmentPool::Shutdown(
		const RIO_EXTENSION_FUNCTION_TABLE* rioFunctionTable) noexcept
	{
		std::scoped_lock poolLock(g_poolMutex);
		if (!g_initialized.exchange(false, std::memory_order_acq_rel))
		{
			return;
		}

		g_activeGeneration.store(0, std::memory_order_release);
		ClearRegionsUnlocked(rioFunctionTable);
		ResetBucketsUnlocked();
		g_registerForRio.store(false, std::memory_order_relaxed);
	}

	bool FSendSegmentPool::TryAllocate(
		const std::size_t requiredLength,
		SAllocation& outAllocation) noexcept
	{
		outAllocation = {};
		if (!g_initialized.load(std::memory_order_acquire) || requiredLength == 0)
		{
			g_allocationFailureCount.fetch_add(1, std::memory_order_relaxed);
			return false;
		}

		const std::uint32_t bucketIndex = FindBucketIndex(requiredLength);
		if (bucketIndex == kInvalidIndex)
		{
			g_allocationFailureCount.fetch_add(1, std::memory_order_relaxed);
			return false;
		}

		SThreadLocalCache& localCache = GetOrCreateThreadLocalCache();
		SBucketLocalCache& localBucket = localCache.buckets[bucketIndex];
		if (localBucket.freeList == nullptr)
		{
			RefillLocalBucketFromShared(g_buckets[bucketIndex], localBucket);
		}

		SFreeNode* freeNode = localBucket.freeList;
		if (freeNode == nullptr)
		{
			g_allocationFailureCount.fetch_add(1, std::memory_order_relaxed);
			return false;
		}

		localBucket.freeList = freeNode->nextLocal;
		freeNode->nextLocal = nullptr;
		--localBucket.cachedCount;

		SBucket& bucket = g_buckets[bucketIndex];
		bucket.inUseSlotCount.fetch_add(1, std::memory_order_relaxed);

		const SRegion& region = g_regions[freeNode->regionIndex];
		outAllocation.bucketIndex = bucketIndex;
		outAllocation.regionIndex = freeNode->regionIndex;
		outAllocation.slotIndex = freeNode->slotIndex;
		outAllocation.offset = freeNode->slotIndex * region.slotSize;
		outAllocation.capacity = region.slotSize;
		outAllocation.data = region.storage.get() + outAllocation.offset;
		outAllocation.rioBufferId = region.rioBufferId;
		return true;
	}

	void FSendSegmentPool::Release(
		SAllocation& allocation) noexcept
	{
		if (!allocation.IsValid())
		{
			return;
		}

		if (!g_initialized.load(std::memory_order_acquire))
		{
			allocation = {};
			return;
		}

		if (allocation.bucketIndex >= g_buckets.size())
		{
			allocation = {};
			return;
		}

		const std::uint32_t bucketIndex = allocation.bucketIndex;
		const std::uint32_t regionIndex = allocation.regionIndex;
		const std::uint32_t slotIndex = allocation.slotIndex;

		SThreadLocalCache& localCache = GetOrCreateThreadLocalCache();
		SBucketLocalCache& localBucket = localCache.buckets[bucketIndex];

		SRegion& region = g_regions[regionIndex];
		SFreeNode& freeNode = region.freeNodes[slotIndex];
		freeNode.nextLocal = localBucket.freeList;
		localBucket.freeList = &freeNode;
		++localBucket.cachedCount;

		SBucket& bucket = g_buckets[bucketIndex];
		if (localBucket.cachedCount > kLocalCacheLimit)
		{
			TrimLocalBucketToTarget(bucket, localBucket);
		}

		bucket.inUseSlotCount.fetch_sub(1, std::memory_order_relaxed);
		allocation = {};
	}

	bool FSendSegmentPool::IsInitialized() noexcept
	{
		return g_initialized.load(std::memory_order_acquire);
	}

	std::size_t FSendSegmentPool::GetRegionSizeBytes() noexcept
	{
		return g_regionSizeBytes;
	}

	std::uint32_t FSendSegmentPool::GetRegionCount() noexcept
	{
		return static_cast<std::uint32_t>(g_regions.size());
	}

	std::uint64_t FSendSegmentPool::GetTotalBytes() noexcept
	{
		return static_cast<std::uint64_t>(g_regions.size()) * static_cast<std::uint64_t>(g_regionSizeBytes);
	}

	std::uint64_t FSendSegmentPool::GetInUseBytes() noexcept
	{
		std::uint64_t totalBytes = 0;
		for (std::size_t bucketIndex = 0; bucketIndex < g_buckets.size(); ++bucketIndex)
		{
			totalBytes += g_buckets[bucketIndex].inUseSlotCount.load(std::memory_order_relaxed) *
						  static_cast<std::uint64_t>(g_buckets[bucketIndex].slotSize);
		}

		return totalBytes;
	}

	std::uint64_t FSendSegmentPool::GetAllocationFailureCount() noexcept
	{
		return g_allocationFailureCount.load(std::memory_order_relaxed);
	}
}
