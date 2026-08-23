#pragma once

#define DECLARE_ALLOC_FREE(Type, PoolName)                                                                                                 \
public:                                                                                                                                    \
	inline static Type* Alloc() noexcept                                                                                                   \
	{                                                                                                                                      \
		return PoolName.Alloc();                                                                                                           \
	}                                                                                                                                      \
	inline static void Free(Type* object) noexcept                                                                                         \
	{                                                                                                                                      \
		PoolName.Free(object);                                                                                                             \
	}                                                                                                                                      \
                                                                                                                                           \
private:

#define DECLARE_ALLOC_FREE_WITH_INIT(Type, PoolName, InitFunc)                                                                             \
public:                                                                                                                                    \
	inline static Type* Alloc() noexcept                                                                                                   \
	{                                                                                                                                      \
		Type* object = PoolName.Alloc();                                                                                                   \
		object->InitFunc();                                                                                                                \
		return object;                                                                                                                     \
	}                                                                                                                                      \
	inline static void Free(Type* object) noexcept                                                                                         \
	{                                                                                                                                      \
		PoolName.Free(object);                                                                                                             \
	}                                                                                                                                      \
                                                                                                                                           \
private:

#define DECLARE_GET_POOL_INFO(Type, PoolName)                                                                                              \
public:                                                                                                                                    \
	inline static LONG GetPoolCapacity() noexcept                                                                                          \
	{                                                                                                                                      \
		return PoolName.GetCapacity();                                                                                                     \
	}                                                                                                                                      \
	inline static LONG GetPoolUsage() noexcept                                                                                             \
	{                                                                                                                                      \
		return PoolName.GetUseCount();                                                                                                     \
	}                                                                                                                                      \
                                                                                                                                           \
private:

#define USE_TLS_POOL(Type, PoolName)                                                                                                       \
	DECLARE_ALLOC_FREE(Type, PoolName)                                                                                                     \
	DECLARE_GET_POOL_INFO(Type, PoolName)                                                                                                  \
	inline static NetworkLib::Memory::FTlsMemoryPoolManager<Type> PoolName;

#define USE_TLS_POOL_WITH_INIT(Type, PoolName, InitFunc)                                                                                   \
	DECLARE_ALLOC_FREE_WITH_INIT(Type, PoolName, InitFunc)                                                                                 \
	DECLARE_GET_POOL_INFO(Type, PoolName)                                                                                                  \
	inline static NetworkLib::Memory::FTlsMemoryPoolManager<Type> PoolName;

namespace NetworkLib::Memory
{
	template <typename T, int BucketSize = 64, int BucketCount = 2, bool UseQueue = true> class FTlsMemoryPoolManager
	{
	private:
		struct SPoolNode
		{
			SPoolNode* nextLocal = nullptr;
			T value{};
		};

		struct SLocalCache
		{
			FTlsMemoryPoolManager* owner = nullptr;
			SPoolNode* freeList = nullptr;
			int cachedCount = 0;
		};

		static constexpr int kLocalCacheTarget = BucketSize;
		static constexpr int kLocalCacheLimit = BucketSize * BucketCount;

	public:
		explicit FTlsMemoryPoolManager(
			int initialCapacity = 0) noexcept
			: m_sharedPool(initialCapacity)
		{
			m_flsIndex = FlsAlloc(&FTlsMemoryPoolManager::ReleaseLocalCache);
			if (m_flsIndex == FLS_OUT_OF_INDEXES)
			{
				__debugbreak();
			}
		}

		~FTlsMemoryPoolManager() noexcept
		{
			if (m_flsIndex != FLS_OUT_OF_INDEXES)
			{
				if (SLocalCache* localCache = reinterpret_cast<SLocalCache*>(FlsGetValue(m_flsIndex)); localCache != nullptr)
				{
					FlsSetValue(m_flsIndex, nullptr);
					ReleaseLocalCache(localCache);
				}

				FlsFree(m_flsIndex);
			}
		}

		T* Alloc() noexcept
		{
			SLocalCache& localCache = GetOrCreateLocalCache();
			if (localCache.freeList == nullptr)
			{
				RefillLocalCache(localCache);
			}

			SPoolNode* node = localCache.freeList;
			localCache.freeList = node->nextLocal;
			node->nextLocal = nullptr;
			--localCache.cachedCount;

			InterlockedIncrement(&m_useCount);
			return &node->value;
		}

		void Free(
			T* object) noexcept
		{
			if (object == nullptr)
			{
				return;
			}

			SLocalCache& localCache = GetOrCreateLocalCache();
			SPoolNode* node = NodeFromObject(object);
			node->nextLocal = localCache.freeList;
			localCache.freeList = node;
			++localCache.cachedCount;
			InterlockedDecrement(&m_useCount);

			if (localCache.cachedCount >= kLocalCacheLimit)
			{
				TrimLocalCache(localCache);
			}
		}

		LONG GetCapacity() const noexcept
		{
			return m_sharedPool.GetCapacity();
		}

		LONG GetUseCount() const noexcept
		{
			return m_useCount;
		}

	private:
		static SPoolNode* NodeFromObject(
			T* object) noexcept
		{
			return reinterpret_cast<SPoolNode*>(reinterpret_cast<unsigned char*>(object) - offsetof(SPoolNode, value));
		}

		static void CALLBACK ReleaseLocalCache(
			void* value) noexcept
		{
			SLocalCache* localCache = reinterpret_cast<SLocalCache*>(value);
			if (localCache == nullptr)
			{
				return;
			}

			localCache->owner->FlushLocalCache(*localCache);
			delete localCache;
		}

		SLocalCache& GetOrCreateLocalCache() noexcept
		{
			SLocalCache* localCache = reinterpret_cast<SLocalCache*>(FlsGetValue(m_flsIndex));
			if (localCache == nullptr)
			{
				localCache = new (std::nothrow) SLocalCache();
				if (localCache == nullptr)
				{
					__debugbreak();
				}

				localCache->owner = this;
				FlsSetValue(m_flsIndex, localCache);
			}

			return *localCache;
		}

		void RefillLocalCache(
			SLocalCache& localCache) noexcept
		{
			for (int index = 0; index < kLocalCacheTarget; ++index)
			{
				SPoolNode* node = m_sharedPool.Alloc();
				node->nextLocal = localCache.freeList;
				localCache.freeList = node;
				++localCache.cachedCount;
			}
		}

		void TrimLocalCache(
			SLocalCache& localCache) noexcept
		{
			while (localCache.cachedCount > kLocalCacheTarget)
			{
				SPoolNode* node = localCache.freeList;
				localCache.freeList = node->nextLocal;
				node->nextLocal = nullptr;
				--localCache.cachedCount;
				m_sharedPool.Free(node);
			}
		}

		void FlushLocalCache(
			SLocalCache& localCache) noexcept
		{
			while (localCache.freeList != nullptr)
			{
				SPoolNode* node = localCache.freeList;
				localCache.freeList = node->nextLocal;
				node->nextLocal = nullptr;
				--localCache.cachedCount;
				m_sharedPool.Free(node);
			}
		}

	private:
		FLockFreeMemoryPool<SPoolNode> m_sharedPool;
		DWORD m_flsIndex = FLS_OUT_OF_INDEXES;
		volatile LONG m_useCount = 0;
	};

	template <typename T, int BucketSize = 64, int BucketCount = 2, bool UseQueue = true>
	using FTlsMemoryPool = FTlsMemoryPoolManager<T, BucketSize, BucketCount, UseQueue>;
}
