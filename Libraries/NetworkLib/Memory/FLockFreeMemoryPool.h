#pragma once

namespace NetworkLib::Memory
{
	template <typename T> class FLockFreeMemoryPool
	{
	private:
		struct SNode
		{
			T data{};
			volatile LONG64 next = 0;
		};

	public:
		explicit FLockFreeMemoryPool(
			int initialCapacity = 0) noexcept
		{
			for (int index = 0; index < initialCapacity; ++index)
			{
				SNode* node = AllocateNode();
				PushNode(node);
			}
		}

		~FLockFreeMemoryPool() noexcept
		{
			while (true)
			{
				SNode* node = PopNode();
				if (node == nullptr)
				{
					break;
				}

				delete node;
			}
		}

		T* Alloc() noexcept
		{
			SNode* node = PopNode();
			if (node == nullptr)
			{
				node = AllocateNode();
			}

			InterlockedIncrement(&m_useCount);
			return &node->data;
		}

		void Free(
			T* value) noexcept
		{
			if (value == nullptr)
			{
				return;
			}

			SNode* node = NodeFromValue(value);
			PushNode(node);
			InterlockedDecrement(&m_useCount);
		}

		LONG GetCapacity() const noexcept
		{
			return m_capacity;
		}

		LONG GetUseCount() const noexcept
		{
			return m_useCount;
		}

	private:
		static SNode* NodeFromValue(
			T* value) noexcept
		{
			return reinterpret_cast<SNode*>(reinterpret_cast<unsigned char*>(value) - offsetof(SNode, data));
		}

		SNode* AllocateNode() noexcept
		{
			SNode* node = new (std::nothrow) SNode();
			if (node != nullptr)
			{
				InterlockedIncrement(&m_capacity);
			}

			return node;
		}

		SNode* PopNode() noexcept
		{
			while (true)
			{
				const std::uint64_t observedTop = Containers::AtomicLoad64(&m_top);
				SNode* topNode = Containers::GetPointer<SNode>(observedTop);
				if (topNode == nullptr)
				{
					return nullptr;
				}

				const std::uint64_t next = Containers::AtomicLoad64(&topNode->next);
				const std::uint64_t newTop =
					Containers::MakeTaggedPointer(Containers::GetTag(observedTop) + 1, Containers::GetPointer<void>(next));
				if (Containers::AtomicCompareExchange64(&m_top, newTop, observedTop) == observedTop)
				{
					topNode->next = 0;
					return topNode;
				}
			}
		}

		void PushNode(
			SNode* node) noexcept
		{
			while (true)
			{
				const std::uint64_t observedTop = Containers::AtomicLoad64(&m_top);
				node->next = static_cast<LONG64>(observedTop);
				const std::uint64_t newTop = Containers::MakeTaggedPointer(Containers::GetTag(observedTop) + 1, node);
				if (Containers::AtomicCompareExchange64(&m_top, newTop, observedTop) == observedTop)
				{
					return;
				}
			}
		}

	private:
		volatile LONG m_useCount = 0;
		volatile LONG m_capacity = 0;
		volatile LONG64 m_top = 0;
	};
}
