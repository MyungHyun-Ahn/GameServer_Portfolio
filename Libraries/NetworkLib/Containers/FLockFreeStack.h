#pragma once

namespace NetworkLib::Containers
{
	template <typename T> struct SLockFreeStackNode
	{
		T data{};
		volatile LONG64 next = 0;
	};

	template <FundamentalOrPointer T, bool UseMemoryPool = true> class FLockFreeStack
	{
	private:
		using SNode = SLockFreeStackNode<T>;
		using SPool = std::conditional_t<UseMemoryPool, Memory::FTlsMemoryPoolManager<SNode>, Memory::FLockFreeMemoryPool<SNode>>;

	public:
		void Push(
			T value) noexcept
		{
			SNode* node = s_nodePool.Alloc();
			node->data = value;

			while (true)
			{
				const std::uint64_t observedTop = AtomicLoad64(&m_top);
				node->next = static_cast<LONG64>(observedTop);
				const std::uint64_t newTop = MakeTaggedPointer(GetTag(observedTop) + 1, node);
				if (AtomicCompareExchange64(&m_top, newTop, observedTop) == observedTop)
				{
					return;
				}
			}
		}

		bool Pop(
			T& outValue) noexcept
		{
			while (true)
			{
				const std::uint64_t observedTop = AtomicLoad64(&m_top);
				SNode* topNode = GetPointer<SNode>(observedTop);
				if (topNode == nullptr)
				{
					return false;
				}

				const std::uint64_t next = AtomicLoad64(&topNode->next);
				const std::uint64_t newTop = MakeTaggedPointer(GetTag(observedTop) + 1, GetPointer<void>(next));
				if (AtomicCompareExchange64(&m_top, newTop, observedTop) == observedTop)
				{
					outValue = topNode->data;
					s_nodePool.Free(topNode);
					return true;
				}
			}
		}

		bool Pop(
			T* outValue) noexcept
		{
			if (outValue == nullptr)
			{
				return false;
			}

			return Pop(*outValue);
		}

	private:
		volatile LONG64 m_top = 0;
		inline static SPool s_nodePool{};
	};
}
