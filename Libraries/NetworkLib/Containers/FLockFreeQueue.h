#pragma once

namespace NetworkLib::Containers
{
	template <typename T> struct SLockFreeQueueNode
	{
		T data{};
		volatile LONG64 next = 0;
	};

	template <FundamentalOrPointer T, bool CAS2First = false> class FLockFreeQueue
	{
	private:
		using SNode = SLockFreeQueueNode<T>;
		using SPool = Memory::FTlsMemoryPoolManager<SNode, 256, 4, !CAS2First>;

	public:
		FLockFreeQueue() noexcept
		{
			SNode* stubNode = s_nodePool.Alloc();
			stubNode->next = 0;

			const std::uint64_t taggedStub = MakeTaggedPointer(1, stubNode);
			m_head = static_cast<LONG64>(taggedStub);
			m_tail = static_cast<LONG64>(taggedStub);
		}

		~FLockFreeQueue() noexcept
		{
			T discardedValue{};
			while (Dequeue(discardedValue))
			{
			}

			SNode* stubNode = GetPointer<SNode>(AtomicLoad64(&m_head));
			if (stubNode != nullptr)
			{
				s_nodePool.Free(stubNode);
			}
		}

		void Enqueue(
			T value) noexcept
		{
			SNode* newNode = s_nodePool.Alloc();
			newNode->data = value;
			newNode->next = 0;

			while (true)
			{
				const std::uint64_t observedTail = AtomicLoad64(&m_tail);
				SNode* tailNode = GetPointer<SNode>(observedTail);
				const std::uint64_t observedNext = AtomicLoad64(&tailNode->next);

				if (observedTail != AtomicLoad64(&m_tail))
				{
					continue;
				}

				if (GetPointer<SNode>(observedNext) == nullptr)
				{
					const std::uint64_t newNext = MakeTaggedPointer(GetTag(observedNext) + 1, newNode);
					if (AtomicCompareExchange64(&tailNode->next, newNext, observedNext) == observedNext)
					{
						const std::uint64_t newTail = MakeTaggedPointer(GetTag(observedTail) + 1, newNode);
						AtomicCompareExchange64(&m_tail, newTail, observedTail);
						return;
					}
				}
				else
				{
					const std::uint64_t advancedTail = MakeTaggedPointer(GetTag(observedTail) + 1, GetPointer<void>(observedNext));
					AtomicCompareExchange64(&m_tail, advancedTail, observedTail);
				}
			}
		}

		bool Dequeue(
			T& outValue) noexcept
		{
			while (true)
			{
				const std::uint64_t observedHead = AtomicLoad64(&m_head);
				const std::uint64_t observedTail = AtomicLoad64(&m_tail);
				SNode* headNode = GetPointer<SNode>(observedHead);
				const std::uint64_t observedNext = AtomicLoad64(&headNode->next);
				SNode* nextNode = GetPointer<SNode>(observedNext);

				if (observedHead != AtomicLoad64(&m_head))
				{
					continue;
				}

				if (nextNode == nullptr)
				{
					return false;
				}

				if (headNode == GetPointer<SNode>(observedTail))
				{
					const std::uint64_t advancedTail = MakeTaggedPointer(GetTag(observedTail) + 1, nextNode);
					AtomicCompareExchange64(&m_tail, advancedTail, observedTail);
					continue;
				}

				outValue = nextNode->data;
				const std::uint64_t newHead = MakeTaggedPointer(GetTag(observedHead) + 1, nextNode);
				if (AtomicCompareExchange64(&m_head, newHead, observedHead) == observedHead)
				{
					s_nodePool.Free(headNode);
					return true;
				}
			}
		}

		bool Dequeue(
			T* outValue) noexcept
		{
			if (outValue == nullptr)
			{
				return false;
			}

			return Dequeue(*outValue);
		}

	private:
		volatile LONG64 m_head = 0;
		volatile LONG64 m_tail = 0;
		inline static SPool s_nodePool{};
	};
}
