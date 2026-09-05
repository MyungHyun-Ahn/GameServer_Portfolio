#include "NetLibPch.h"

#include "Servers/Session/FIocpSession.h"

namespace NetworkLib::Session
{
	FIocpSession* FIocpSession::Create() noexcept
	{
		FIocpSession* session = s_sessionPool.Alloc();
		session->Reset();
		session->m_refCount.store(1, std::memory_order_release);
		return session;
	}

	void FIocpSession::Destroy(
		FIocpSession* session) noexcept
	{
		if (session == nullptr)
		{
			return;
		}

		session->Reset();
		s_sessionPool.Free(session);
	}

	LONG FIocpSession::GetPoolCapacity() noexcept
	{
		return s_sessionPool.GetCapacity();
	}

	LONG FIocpSession::GetPoolUsage() noexcept
	{
		return s_sessionPool.GetUseCount();
	}

	FIocpSession::~FIocpSession()
	{
		Reset();
	}

	void FIocpSession::SIoContext::Prepare(
		EIoType newIoType,
		FIocpSession* newOwnerSession)
	{
		ZeroMemory(&overlapped, sizeof(overlapped));
		ioType = newIoType;
		ownerSession = newOwnerSession;
	}

	void FIocpSession::Initialize(
		SOCKET socket,
		std::uint64_t sessionId,
		std::uint32_t slotIndex,
		std::uint32_t generation,
		std::size_t recvBufferCapacity)
	{
		m_socket = socket;
		m_sessionId.store(sessionId, std::memory_order_release);
		m_slotIndex = slotIndex;
		m_generation = generation;
		m_closing.store(false);
		m_recvBuffer.Initialize(recvBufferCapacity);
		m_activeSendBuffers.clear();
		m_sendWsabufs.clear();
		m_sendState.store(0);
		m_liveSendIoCount.store(0);
		m_maxObservedConcurrentSendIoCount.store(0);
		m_queuedSendBufferCount.store(0);
		m_maxObservedQueuedSendBufferCount.store(0);
		m_recvContext = {};
		m_sendContext = {};
	}

	void FIocpSession::Reset() noexcept
	{
		ReleaseActiveSendBuffers();
		ReleaseQueuedSendBuffers();
		m_socket = INVALID_SOCKET;
		m_sessionId.store(0, std::memory_order_release);
		m_slotIndex = 0;
		m_generation = 0;
		m_closing.store(false);
		m_recvContext = {};
		m_sendContext = {};
		m_recvBuffer.Clear();
		m_sendState.store(0);
		m_liveSendIoCount.store(0);
		m_maxObservedConcurrentSendIoCount.store(0);
		m_queuedSendBufferCount.store(0);
		m_maxObservedQueuedSendBufferCount.store(0);
	}

	SOCKET FIocpSession::GetSocket() const noexcept
	{
		return m_socket;
	}

	void FIocpSession::SetSocket(
		SOCKET socket) noexcept
	{
		m_socket = socket;
	}

	std::uint64_t FIocpSession::GetSessionId() const noexcept
	{
		return m_sessionId.load(std::memory_order_acquire);
	}

	std::uint32_t FIocpSession::GetSlotIndex() const noexcept
	{
		return m_slotIndex;
	}

	std::uint32_t FIocpSession::GetGeneration() const noexcept
	{
		return m_generation;
	}

	bool FIocpSession::IsClosing() const noexcept
	{
		return m_closing.load();
	}

	bool FIocpSession::TryMarkClosing() noexcept
	{
		bool expected = false;
		return m_closing.compare_exchange_strong(expected, true);
	}

	long FIocpSession::AcquireRef() noexcept
	{
		return m_refCount.fetch_add(1) + 1;
	}

	bool FIocpSession::TryAcquireRef() noexcept
	{
		long refCount = m_refCount.load(std::memory_order_acquire);
		while (refCount > 0)
		{
			if (m_refCount.compare_exchange_weak(refCount, refCount + 1, std::memory_order_acq_rel, std::memory_order_acquire))
			{
				return true;
			}
		}

		return false;
	}

	long FIocpSession::ReleaseRef() noexcept
	{
		return m_refCount.fetch_sub(1) - 1;
	}

	void FIocpSession::BuildRecvWsabufs(
		WSABUF (&outBuffers)[2],
		DWORD& outBufferCount) noexcept
	{
		m_recvBuffer.BuildRecvWsabufs(outBuffers, outBufferCount);
	}

	bool FIocpSession::CommitRecvBytes(
		std::size_t length) noexcept
	{
		return m_recvBuffer.CommitWrite(length);
	}

	Packet::Buffer::FRecvBuffer& FIocpSession::GetRecvBuffer() noexcept
	{
		return m_recvBuffer;
	}

	const Packet::Buffer::FRecvBuffer& FIocpSession::GetRecvBuffer() const noexcept
	{
		return m_recvBuffer;
	}

	FIocpSession::SIoContext& FIocpSession::GetRecvContext() noexcept
	{
		return m_recvContext;
	}

	FIocpSession::SIoContext& FIocpSession::GetSendContext() noexcept
	{
		return m_sendContext;
	}

	void FIocpSession::EnqueueSendBuffer(
		NetworkLib::Packet::Buffer::FSendBuffer* sendBuffer) noexcept
	{
		m_sendQueue.Enqueue(sendBuffer);
		m_sendState.fetch_or(kSendPendingFlag, std::memory_order_release);
		const std::uint32_t queuedCount = m_queuedSendBufferCount.fetch_add(1) + 1;
		std::uint32_t currentMax = m_maxObservedQueuedSendBufferCount.load();
		while (queuedCount > currentMax && !m_maxObservedQueuedSendBufferCount.compare_exchange_weak(currentMax, queuedCount))
		{
		}
	}

	bool FIocpSession::TryBeginSend() noexcept
	{
		std::uint32_t expected = m_sendState.load(std::memory_order_acquire);
		while (true)
		{
			if ((expected & kSendInFlightFlag) != 0)
			{
				return false;
			}

			const std::uint32_t desired = (expected | kSendInFlightFlag) & ~kSendPendingFlag;
			if (m_sendState.compare_exchange_weak(expected, desired, std::memory_order_acq_rel, std::memory_order_acquire))
			{
				return true;
			}
		}
	}

	bool FIocpSession::EndSend() noexcept
	{
		const std::uint32_t previousState = m_sendState.fetch_and(~kSendInFlightFlag, std::memory_order_acq_rel);
		return (previousState & kSendPendingFlag) != 0;
	}

	int FIocpSession::BeginSendIo() noexcept
	{
		const int liveSendIoCount = m_liveSendIoCount.fetch_add(1) + 1;
		int currentMax = m_maxObservedConcurrentSendIoCount.load();
		while (liveSendIoCount > currentMax && !m_maxObservedConcurrentSendIoCount.compare_exchange_weak(currentMax, liveSendIoCount))
		{
		}

		return liveSendIoCount;
	}

	int FIocpSession::FinishSendIo() noexcept
	{
		return m_liveSendIoCount.fetch_sub(1) - 1;
	}

	int FIocpSession::GetMaxObservedConcurrentSendIoCount() const noexcept
	{
		return m_maxObservedConcurrentSendIoCount.load();
	}

	std::uint32_t FIocpSession::GetQueuedSendBufferCount() const noexcept
	{
		return m_queuedSendBufferCount.load();
	}

	std::uint32_t FIocpSession::GetMaxObservedQueuedSendBufferCount() const noexcept
	{
		return m_maxObservedQueuedSendBufferCount.load();
	}

	bool FIocpSession::FillSendBatch(
		std::size_t maxSendCount) noexcept
	{
		m_activeSendBuffers.clear();
		m_sendWsabufs.clear();

		m_activeSendBuffers.reserve(maxSendCount);
		m_sendWsabufs.reserve(maxSendCount * 2);

		while (m_activeSendBuffers.size() < maxSendCount)
		{
			NetworkLib::Packet::Buffer::FSendBuffer* sendBuffer = nullptr;
			if (!m_sendQueue.Dequeue(&sendBuffer))
			{
				break;
			}

			m_queuedSendBufferCount.fetch_sub(1);
			m_activeSendBuffers.push_back(sendBuffer);
			sendBuffer->AppendWsabufs(m_sendWsabufs);
		}

		return !m_activeSendBuffers.empty();
	}

	const std::vector<WSABUF>& FIocpSession::GetSendWsabufs() const noexcept
	{
		return m_sendWsabufs;
	}

	void FIocpSession::ReleaseActiveSendBuffers() noexcept
	{
		for (NetworkLib::Packet::Buffer::FSendBuffer* sendBuffer : m_activeSendBuffers)
		{
			NetworkLib::Packet::Buffer::FSendBuffer::Release(sendBuffer);
		}

		m_activeSendBuffers.clear();
		m_sendWsabufs.clear();
	}

	void FIocpSession::ReleaseQueuedSendBuffers() noexcept
	{
		NetworkLib::Packet::Buffer::FSendBuffer* sendBuffer = nullptr;
		while (m_sendQueue.Dequeue(&sendBuffer))
		{
			m_queuedSendBufferCount.fetch_sub(1);
			NetworkLib::Packet::Buffer::FSendBuffer::Release(sendBuffer);
			sendBuffer = nullptr;
		}
	}
}
