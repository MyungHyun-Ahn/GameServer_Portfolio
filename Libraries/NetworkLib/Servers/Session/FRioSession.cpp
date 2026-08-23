#include "NetLibPch.h"

#include "Packet/Buffer/FPacketBuffer.h"
#include "Servers/Session/FRioSession.h"

namespace NetworkLib::Session
{
	namespace
	{
		std::mutex g_sessionRegistryMutex;
		std::vector<FRioSession*> g_allSessions;
	}

	FRioSession* FRioSession::Create() noexcept
	{
		FRioSession* session = s_sessionPool.Alloc();
		session->RegisterInSessionRegistry();
		session->Reset();
		return session;
	}

	void FRioSession::Destroy(
		FRioSession* session) noexcept
	{
		if (session == nullptr)
		{
			return;
		}

		session->Reset();
		s_sessionPool.Free(session);
	}

	void FRioSession::EnsurePoolCapacity(
		LONG targetCapacity) noexcept
	{
		if (targetCapacity <= 0)
		{
			return;
		}

		std::vector<FRioSession*> reservedSessions;
		reservedSessions.reserve(static_cast<std::size_t>(targetCapacity));
		while (GetPoolCapacity() < targetCapacity)
		{
			FRioSession* session = s_sessionPool.Alloc();
			if (session == nullptr)
			{
				break;
			}

			session->RegisterInSessionRegistry();
			session->Reset();
			reservedSessions.push_back(session);
		}

		for (FRioSession* session : reservedSessions)
		{
			s_sessionPool.Free(session);
		}
	}

	LONG FRioSession::GetPoolCapacity() noexcept
	{
		return s_sessionPool.GetCapacity();
	}

	LONG FRioSession::GetPoolUsage() noexcept
	{
		return s_sessionPool.GetUseCount();
	}

	FRioSession::~FRioSession()
	{
		Reset();
	}

	void FRioSession::Initialize(
		SOCKET socket,
		std::uint64_t sessionId,
		std::uint32_t slotIndex,
		std::uint32_t generation,
		std::uint32_t ownerWorkerIndex,
		std::size_t recvBufferCapacity,
		std::size_t recvStagingCapacity,
		std::size_t sendRingCapacityBytes) noexcept
	{
		m_socket = socket;
		m_sessionId = sessionId;
		m_slotIndex = slotIndex;
		m_generation = generation;
		m_ownerWorkerIndex = ownerWorkerIndex;
		m_requestQueue = RIO_INVALID_RQ;
		m_recvBufferId = RIO_INVALID_BUFFERID;
		m_refCount.store(1);
		m_closing.store(false);
		m_recvPending.store(false);
		m_ownerSendDrainScheduled.store(false);
		m_queuedSendBufferCount.store(0);
		m_maxObservedQueuedSendBufferCount.store(0);
		m_ownerQueuedSendBytes.store(0);
		m_maxObservedOwnerQueuedSendBytes.store(0);
		m_observedSendRingUsedBytes.store(0);
		m_observedSendRingInFlightBytes.store(0);
		m_maxObservedSendRingUsedBytes.store(0);
		m_lastObservedSendRingTouchThreadId.store(0);
		m_recvBuffer.Initialize(recvBufferCapacity);
		m_recvStagingBuffer.assign(recvStagingCapacity, 0);
		m_sendRingCapacityBytes = std::max<std::size_t>(kMaxSendPacketSizeBytes, sendRingCapacityBytes);
		m_recvRequestContext = {};
		m_recvRequestContext.requestKind = ERequestKind::Recv;
		m_recvRequestContext.ownerSession = this;
		m_sendRequestContext = {};
		m_sendRequestContext.requestKind = ERequestKind::Send;
		m_sendRequestContext.ownerSession = this;
		ResetSendRingState();
	}

	void FRioSession::Reset() noexcept
	{
		ReleaseQueuedSendPackets();
		m_socket = INVALID_SOCKET;
		m_sessionId = 0;
		m_slotIndex = 0;
		m_generation = 0;
		m_ownerWorkerIndex = 0;
		m_requestQueue = RIO_INVALID_RQ;
		m_recvBufferId = RIO_INVALID_BUFFERID;
		m_refCount.store(1);
		m_closing.store(false);
		m_recvPending.store(false);
		m_ownerSendDrainScheduled.store(false);
		m_queuedSendBufferCount.store(0);
		m_maxObservedQueuedSendBufferCount.store(0);
		m_ownerQueuedSendBytes.store(0);
		m_maxObservedOwnerQueuedSendBytes.store(0);
		m_observedSendRingUsedBytes.store(0);
		m_observedSendRingInFlightBytes.store(0);
		m_maxObservedSendRingUsedBytes.store(0);
		m_lastObservedSendRingTouchThreadId.store(0);
		m_recvBuffer.Clear();
		m_recvStagingBuffer.clear();
		m_sendRingCapacityBytes = kDefaultSendRingSizeBytes;
		m_recvRequestContext = {};
		m_recvRequestContext.requestKind = ERequestKind::Recv;
		m_recvRequestContext.ownerSession = this;
		m_sendRequestContext = {};
		m_sendRequestContext.requestKind = ERequestKind::Send;
		m_sendRequestContext.ownerSession = this;
		ResetSendRingState();
	}

	SOCKET FRioSession::GetSocket() const noexcept
	{
		return m_socket;
	}

	void FRioSession::SetSocket(
		SOCKET socket) noexcept
	{
		m_socket = socket;
	}

	std::uint64_t FRioSession::GetSessionId() const noexcept
	{
		return m_sessionId;
	}

	std::uint32_t FRioSession::GetSlotIndex() const noexcept
	{
		return m_slotIndex;
	}

	std::uint32_t FRioSession::GetGeneration() const noexcept
	{
		return m_generation;
	}

	std::uint32_t FRioSession::GetOwnerWorkerIndex() const noexcept
	{
		return m_ownerWorkerIndex;
	}

	RIO_RQ FRioSession::GetRequestQueue() const noexcept
	{
		return m_requestQueue;
	}

	void FRioSession::SetRequestQueue(
		RIO_RQ requestQueue) noexcept
	{
		m_requestQueue = requestQueue;
	}

	RIO_BUFFERID FRioSession::GetRecvBufferId() const noexcept
	{
		return m_recvBufferId;
	}

	void FRioSession::SetRecvBufferId(
		RIO_BUFFERID bufferId) noexcept
	{
		m_recvBufferId = bufferId;
	}

	std::mutex& FRioSession::GetRequestQueueMutex() noexcept
	{
		return m_requestQueueMutex;
	}

	Packet::Buffer::FRecvBuffer& FRioSession::GetRecvBuffer() noexcept
	{
		return m_recvBuffer;
	}

	const Packet::Buffer::FRecvBuffer& FRioSession::GetRecvBuffer() const noexcept
	{
		return m_recvBuffer;
	}

	char* FRioSession::GetRecvStagingData() noexcept
	{
		return m_recvStagingBuffer.empty() ? nullptr : m_recvStagingBuffer.data();
	}

	std::size_t FRioSession::GetRecvStagingCapacity() const noexcept
	{
		return m_recvStagingBuffer.size();
	}

	bool FRioSession::CopyReceivedDataFromStaging(
		std::size_t length) noexcept
	{
		if (length > m_recvStagingBuffer.size() || length > m_recvBuffer.GetFreeSize())
		{
			return false;
		}

		WSABUF recvBuffers[2]{};
		DWORD recvBufferCount = 0;
		m_recvBuffer.BuildRecvWsabufs(recvBuffers, recvBufferCount);
		std::size_t copiedLength = 0;
		for (DWORD bufferIndex = 0; bufferIndex < recvBufferCount && copiedLength < length; ++bufferIndex)
		{
			const std::size_t chunkLength = std::min<std::size_t>(recvBuffers[bufferIndex].len, length - copiedLength);
			std::memcpy(recvBuffers[bufferIndex].buf, m_recvStagingBuffer.data() + copiedLength, chunkLength);
			copiedLength += chunkLength;
		}

		return copiedLength == length && m_recvBuffer.CommitWrite(length);
	}

	FRioSession::SRecvRequestContext& FRioSession::GetRecvRequestContext() noexcept
	{
		return m_recvRequestContext;
	}

	const FRioSession::SRecvRequestContext& FRioSession::GetRecvRequestContext() const noexcept
	{
		return m_recvRequestContext;
	}

	bool FRioSession::IsClosing() const noexcept
	{
		return m_closing.load();
	}

	bool FRioSession::TryMarkClosing() noexcept
	{
		bool expected = false;
		return m_closing.compare_exchange_strong(expected, true);
	}

	bool FRioSession::TryBeginRecv() noexcept
	{
		bool expected = false;
		return m_recvPending.compare_exchange_strong(expected, true);
	}

	void FRioSession::EndRecv() noexcept
	{
		m_recvPending.store(false);
	}

	bool FRioSession::TryEnqueueOwnerSendPacket(
		Packet::Buffer::FPacketBuffer* packetBuffer) noexcept
	{
		if (packetBuffer == nullptr)
		{
			return false;
		}

		const std::uint64_t packetBytes = packetBuffer->GetBuffer().size();
		const std::uint64_t queueLimitBytes = m_sendRingCapacityBytes;
		std::uint64_t queuedBytes = m_ownerQueuedSendBytes.load(std::memory_order_relaxed);
		while (true)
		{
			if (packetBytes > queueLimitBytes || queuedBytes > queueLimitBytes - packetBytes)
			{
				return false;
			}

			if (m_ownerQueuedSendBytes.compare_exchange_weak(
					queuedBytes, queuedBytes + packetBytes, std::memory_order_acq_rel, std::memory_order_relaxed))
			{
				break;
			}
		}

		OnSendQueued();
		m_ownerSendPacketQueue.Enqueue(packetBuffer);

		const std::uint64_t currentQueuedBytes = queuedBytes + packetBytes;
		std::uint64_t observedMax = m_maxObservedOwnerQueuedSendBytes.load(std::memory_order_relaxed);
		while (currentQueuedBytes > observedMax &&
			   !m_maxObservedOwnerQueuedSendBytes.compare_exchange_weak(observedMax, currentQueuedBytes, std::memory_order_relaxed))
		{
		}

		return true;
	}

	bool FRioSession::TryDequeueOwnerSendPacket(
		Packet::Buffer::FPacketBuffer*& outPacketBuffer) noexcept
	{
		outPacketBuffer = nullptr;
		if (!m_ownerSendPacketQueue.Dequeue(&outPacketBuffer))
		{
			return false;
		}

		m_queuedSendBufferCount.fetch_sub(1, std::memory_order_relaxed);
		m_ownerQueuedSendBytes.fetch_sub(outPacketBuffer->GetBuffer().size(), std::memory_order_relaxed);
		return true;
	}

	bool FRioSession::TryScheduleOwnerSendDrain() noexcept
	{
		bool expected = false;
		return m_ownerSendDrainScheduled.compare_exchange_strong(expected, true);
	}

	void FRioSession::ClearOwnerSendDrainScheduled() noexcept
	{
		m_ownerSendDrainScheduled.store(false, std::memory_order_release);
	}

	std::mutex& FRioSession::GetSendRingMutex() noexcept
	{
		return m_sendRingMutex;
	}

	const std::mutex& FRioSession::GetSendRingMutex() const noexcept
	{
		return m_sendRingMutex;
	}

	bool FRioSession::ObserveSendRingTouch(
		const std::uint32_t threadId) noexcept
	{
		const std::uint32_t previousThreadId = m_lastObservedSendRingTouchThreadId.exchange(threadId, std::memory_order_relaxed);
		return previousThreadId != 0 && previousThreadId != threadId;
	}

	bool FRioSession::EnsureSendRingRegistered(
		const RIO_EXTENSION_FUNCTION_TABLE& rioFunctionTable) noexcept
	{
		if (m_sendRingBuffer.size() != m_sendRingCapacityBytes)
		{
			m_sendRingBuffer.assign(m_sendRingCapacityBytes, 0);
		}

		if (m_sendRingBufferId != RIO_INVALID_BUFFERID)
		{
			return true;
		}

		m_sendRingBufferId = rioFunctionTable.RIORegisterBuffer(m_sendRingBuffer.data(), static_cast<DWORD>(m_sendRingBuffer.size()));
		return m_sendRingBufferId != RIO_INVALID_BUFFERID;
	}

	void FRioSession::ReleaseSendRingRegistration(
		const RIO_EXTENSION_FUNCTION_TABLE& rioFunctionTable) noexcept
	{
		if (m_sendRingBufferId == RIO_INVALID_BUFFERID)
		{
			return;
		}

		rioFunctionTable.RIODeregisterBuffer(m_sendRingBufferId);
		m_sendRingBufferId = RIO_INVALID_BUFFERID;
		ResetSendRingState();
	}

	void FRioSession::ReleaseAllSendRingRegistrations(
		const RIO_EXTENSION_FUNCTION_TABLE& rioFunctionTable) noexcept
	{
		std::scoped_lock<std::mutex> registryLock(g_sessionRegistryMutex);
		for (FRioSession* session : g_allSessions)
		{
			if (session != nullptr)
			{
				session->ReleaseSendRingRegistration(rioFunctionTable);
			}
		}
	}

	bool FRioSession::TryAppendSendPacket(
		const char* headerData,
		const std::size_t headerLength,
		const char* payloadData,
		const std::size_t payloadLength) noexcept
	{
		const std::size_t totalLength = headerLength + payloadLength;
		if (totalLength == 0 || totalLength > m_sendRingCapacityBytes)
		{
			return false;
		}

		if ((m_sendRingCapacityBytes - m_sendRingUsedBytes) < totalLength)
		{
			return false;
		}

		if (headerLength > 0)
		{
			WriteSendBytes(headerData, headerLength);
		}

		if (payloadLength > 0)
		{
			WriteSendBytes(payloadData, payloadLength);
		}

		UpdateSendRingObservability();
		return true;
	}

	bool FRioSession::TryPrepareNextSend() noexcept
	{
		if (m_sendRingBufferId == RIO_INVALID_BUFFERID || m_sendRingInFlightBytes != 0 || m_sendRingUsedBytes == 0)
		{
			return false;
		}

		const std::size_t contiguousLength = std::min<std::size_t>(m_sendRingUsedBytes, m_sendRingCapacityBytes - m_sendRingReadOffset);
		if (contiguousLength == 0)
		{
			return false;
		}

		m_sendRingInFlightBytes = contiguousLength;
		m_sendRequestContext.buffer.BufferId = m_sendRingBufferId;
		m_sendRequestContext.buffer.Offset = static_cast<ULONG>(m_sendRingReadOffset);
		m_sendRequestContext.buffer.Length = static_cast<ULONG>(contiguousLength);
		UpdateSendRingObservability();
		return true;
	}

	void FRioSession::CompleteCurrentSend() noexcept
	{
		if (m_sendRingInFlightBytes == 0)
		{
			return;
		}

		m_sendRingReadOffset = (m_sendRingReadOffset + m_sendRingInFlightBytes) % m_sendRingCapacityBytes;
		m_sendRingUsedBytes -= m_sendRingInFlightBytes;
		m_sendRingInFlightBytes = 0;
		m_sendRequestContext.buffer = RIO_BUF{};
		UpdateSendRingObservability();
	}

	void FRioSession::CancelPreparedSend() noexcept
	{
		m_sendRingInFlightBytes = 0;
		m_sendRequestContext.buffer = RIO_BUF{};
		UpdateSendRingObservability();
	}

	bool FRioSession::HasPreparedSend() const noexcept
	{
		return m_sendRingInFlightBytes != 0;
	}

	bool FRioSession::HasQueuedSendData() const noexcept
	{
		return m_sendRingUsedBytes != 0;
	}

	FRioSession::SSendRequestContext& FRioSession::GetSendRequestContext() noexcept
	{
		return m_sendRequestContext;
	}

	const FRioSession::SSendRequestContext& FRioSession::GetSendRequestContext() const noexcept
	{
		return m_sendRequestContext;
	}

	long FRioSession::AcquireRef() noexcept
	{
		return m_refCount.fetch_add(1, std::memory_order_relaxed) + 1;
	}

	long FRioSession::ReleaseRef() noexcept
	{
		return m_refCount.fetch_sub(1, std::memory_order_acq_rel) - 1;
	}

	void FRioSession::OnSendQueued() noexcept
	{
		const std::uint32_t queuedCount = m_queuedSendBufferCount.fetch_add(1, std::memory_order_relaxed) + 1;
		std::uint32_t observedMax = m_maxObservedQueuedSendBufferCount.load(std::memory_order_relaxed);
		while (queuedCount > observedMax &&
			   !m_maxObservedQueuedSendBufferCount.compare_exchange_weak(observedMax, queuedCount, std::memory_order_relaxed))
		{
		}
	}

	void FRioSession::OnSendCompleted() noexcept
	{
		// Ring buffer mode does not keep per-send buffer completion counts.
	}

	std::uint32_t FRioSession::GetQueuedSendBufferCount() const noexcept
	{
		return m_queuedSendBufferCount.load(std::memory_order_relaxed);
	}

	std::uint32_t FRioSession::GetMaxObservedQueuedSendBufferCount() const noexcept
	{
		return m_maxObservedQueuedSendBufferCount.load(std::memory_order_relaxed);
	}

	std::uint64_t FRioSession::GetOwnerQueuedSendBytes() const noexcept
	{
		return m_ownerQueuedSendBytes.load(std::memory_order_relaxed);
	}

	std::uint64_t FRioSession::GetMaxObservedOwnerQueuedSendBytes() const noexcept
	{
		return m_maxObservedOwnerQueuedSendBytes.load(std::memory_order_relaxed);
	}

	std::uint32_t FRioSession::GetSendRingUsedBytes() const noexcept
	{
		return m_observedSendRingUsedBytes.load(std::memory_order_relaxed);
	}

	std::uint32_t FRioSession::GetSendRingInFlightBytes() const noexcept
	{
		return m_observedSendRingInFlightBytes.load(std::memory_order_relaxed);
	}

	std::uint32_t FRioSession::GetSendRingFreeBytes() const noexcept
	{
		const std::uint32_t usedBytes = GetSendRingUsedBytes();
		return usedBytes >= m_sendRingCapacityBytes ? 0u : static_cast<std::uint32_t>(m_sendRingCapacityBytes - usedBytes);
	}

	std::uint32_t FRioSession::GetSendRingCapacityBytes() const noexcept
	{
		return static_cast<std::uint32_t>(m_sendRingCapacityBytes);
	}

	std::uint32_t FRioSession::GetMaxObservedSendRingUsedBytes() const noexcept
	{
		return m_maxObservedSendRingUsedBytes.load(std::memory_order_relaxed);
	}

	void FRioSession::ReleaseRioResources(
		const RIO_EXTENSION_FUNCTION_TABLE& rioFunctionTable) noexcept
	{
		if (m_recvBufferId != RIO_INVALID_BUFFERID)
		{
			rioFunctionTable.RIODeregisterBuffer(m_recvBufferId);
			m_recvBufferId = RIO_INVALID_BUFFERID;
		}
	}

	void FRioSession::ReleaseQueuedSendPackets() noexcept
	{
		Packet::Buffer::FPacketBuffer* packetBuffer = nullptr;
		while (m_ownerSendPacketQueue.Dequeue(&packetBuffer))
		{
			Packet::Buffer::FPacketBuffer::Release(packetBuffer);
		}
		m_queuedSendBufferCount.store(0, std::memory_order_relaxed);
		m_ownerQueuedSendBytes.store(0, std::memory_order_relaxed);
	}

	void FRioSession::RegisterInSessionRegistry() noexcept
	{
		if (m_trackedInRegistry)
		{
			return;
		}

		std::scoped_lock<std::mutex> registryLock(g_sessionRegistryMutex);
		if (!m_trackedInRegistry)
		{
			g_allSessions.push_back(this);
			m_trackedInRegistry = true;
		}
	}

	void FRioSession::ResetSendRingState() noexcept
	{
		m_sendRingReadOffset = 0;
		m_sendRingWriteOffset = 0;
		m_sendRingUsedBytes = 0;
		m_sendRingInFlightBytes = 0;
		m_sendRequestContext.buffer = RIO_BUF{};
		UpdateSendRingObservability();
	}

	void FRioSession::UpdateSendRingObservability() noexcept
	{
		const std::uint32_t usedBytes = static_cast<std::uint32_t>(m_sendRingUsedBytes);
		const std::uint32_t inFlightBytes = static_cast<std::uint32_t>(m_sendRingInFlightBytes);
		m_observedSendRingUsedBytes.store(usedBytes, std::memory_order_relaxed);
		m_observedSendRingInFlightBytes.store(inFlightBytes, std::memory_order_relaxed);

		std::uint32_t observedMax = m_maxObservedSendRingUsedBytes.load(std::memory_order_relaxed);
		while (usedBytes > observedMax &&
			   !m_maxObservedSendRingUsedBytes.compare_exchange_weak(observedMax, usedBytes, std::memory_order_relaxed))
		{
		}
	}

	void FRioSession::WriteSendBytes(
		const char* data,
		std::size_t length) noexcept
	{
		if (length == 0)
		{
			return;
		}

		const std::size_t tailLength = std::min<std::size_t>(length, m_sendRingCapacityBytes - m_sendRingWriteOffset);
		std::memcpy(m_sendRingBuffer.data() + m_sendRingWriteOffset, data, tailLength);
		if (length > tailLength)
		{
			std::memcpy(m_sendRingBuffer.data(), data + tailLength, length - tailLength);
		}

		m_sendRingWriteOffset = (m_sendRingWriteOffset + length) % m_sendRingCapacityBytes;
		m_sendRingUsedBytes += length;
	}
}
