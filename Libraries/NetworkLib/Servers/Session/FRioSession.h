#pragma once

namespace NetworkLib::Packet::Buffer
{
	class FPacketBuffer;
}

namespace NetworkLib::Session
{
	class FRioSession final : public ISession
	{
	public:
		static FRioSession* Create() noexcept;
		static void Destroy(FRioSession* session) noexcept;
		static void EnsurePoolCapacity(LONG targetCapacity) noexcept;
		static LONG GetPoolCapacity() noexcept;
		static LONG GetPoolUsage() noexcept;

	public:
		enum class ERequestKind : std::uint8_t
		{
			Recv = 0,
			Send = 1
		};

		struct SRequestContext
		{
			ERequestKind requestKind = ERequestKind::Recv;
			FRioSession* ownerSession = nullptr;
		};

		struct SRecvRequestContext final : SRequestContext
		{
			RIO_BUF buffer{};
		};

		struct SSendRequestContext final : SRequestContext
		{
			RIO_BUF buffer{};
		};

	public:
		inline static constexpr std::size_t kDefaultSendRingSizeBytes = 64u * 1024u;
		inline static constexpr std::size_t kMaxSendPacketSizeBytes = NetworkLib::Packet::Framing::kMaxFramedPacketSizeBytes;

		FRioSession() = default;
		~FRioSession() override;

		void Initialize(SOCKET socket,
			std::uint64_t sessionId,
			std::uint32_t slotIndex,
			std::uint32_t generation,
			std::uint32_t ownerWorkerIndex,
			std::size_t recvBufferCapacity,
			std::size_t recvStagingCapacity,
			std::size_t sendRingCapacityBytes) noexcept;
		void Reset() noexcept;

		SOCKET GetSocket() const noexcept;
		void SetSocket(SOCKET socket) noexcept;

		std::uint64_t GetSessionId() const noexcept override;
		std::uint32_t GetSlotIndex() const noexcept override;
		std::uint32_t GetGeneration() const noexcept override;
		std::uint32_t GetOwnerWorkerIndex() const noexcept;

		RIO_RQ GetRequestQueue() const noexcept;
		void SetRequestQueue(RIO_RQ requestQueue) noexcept;
		RIO_BUFFERID GetRecvBufferId() const noexcept;
		void SetRecvBufferId(RIO_BUFFERID bufferId) noexcept;
		std::mutex& GetRequestQueueMutex() noexcept;

		NetworkLib::Packet::Buffer::FRecvBuffer& GetRecvBuffer() noexcept;
		const NetworkLib::Packet::Buffer::FRecvBuffer& GetRecvBuffer() const noexcept;
		char* GetRecvStagingData() noexcept;
		std::size_t GetRecvStagingCapacity() const noexcept;
		bool CopyReceivedDataFromStaging(std::size_t length) noexcept;

		SRecvRequestContext& GetRecvRequestContext() noexcept;
		const SRecvRequestContext& GetRecvRequestContext() const noexcept;

		bool IsClosing() const noexcept override;
		bool TryMarkClosing() noexcept override;
		bool TryBeginRecv() noexcept;
		void EndRecv() noexcept;
		bool TryEnqueueOwnerSendPacket(NetworkLib::Packet::Buffer::FPacketBuffer* packetBuffer) noexcept;
		bool TryDequeueOwnerSendPacket(NetworkLib::Packet::Buffer::FPacketBuffer*& outPacketBuffer) noexcept;
		bool TryScheduleOwnerSendDrain() noexcept;
		void ClearOwnerSendDrainScheduled() noexcept;
		std::mutex& GetSendRingMutex() noexcept;
		const std::mutex& GetSendRingMutex() const noexcept;
		bool ObserveSendRingTouch(std::uint32_t threadId) noexcept;
		bool EnsureSendRingRegistered(const RIO_EXTENSION_FUNCTION_TABLE& rioFunctionTable) noexcept;
		void ReleaseSendRingRegistration(const RIO_EXTENSION_FUNCTION_TABLE& rioFunctionTable) noexcept;
		static void ReleaseAllSendRingRegistrations(const RIO_EXTENSION_FUNCTION_TABLE& rioFunctionTable) noexcept;
		bool TryAppendSendPacket(const char* headerData,
			std::size_t headerLength,
			const char* payloadData,
			std::size_t payloadLength) noexcept;
		bool TryPrepareNextSend() noexcept;
		void CompleteCurrentSend() noexcept;
		void CancelPreparedSend() noexcept;
		bool HasPreparedSend() const noexcept;
		bool HasQueuedSendData() const noexcept;
		SSendRequestContext& GetSendRequestContext() noexcept;
		const SSendRequestContext& GetSendRequestContext() const noexcept;

		long AcquireRef() noexcept override;
		bool TryAcquireRef() noexcept;
		long ReleaseRef() noexcept override;

		void OnSendQueued() noexcept;
		void OnSendCompleted() noexcept;
		std::uint32_t GetQueuedSendBufferCount() const noexcept override;
		std::uint32_t GetMaxObservedQueuedSendBufferCount() const noexcept override;
		std::uint64_t GetOwnerQueuedSendBytes() const noexcept;
		std::uint64_t GetMaxObservedOwnerQueuedSendBytes() const noexcept;
		std::uint32_t GetSendRingUsedBytes() const noexcept;
		std::uint32_t GetSendRingInFlightBytes() const noexcept;
		std::uint32_t GetSendRingFreeBytes() const noexcept;
		std::uint32_t GetSendRingCapacityBytes() const noexcept;
		std::uint32_t GetMaxObservedSendRingUsedBytes() const noexcept;

		void ReleaseRioResources(const RIO_EXTENSION_FUNCTION_TABLE& rioFunctionTable) noexcept;

	private:
		SOCKET m_socket = INVALID_SOCKET;
		std::atomic<std::uint64_t> m_sessionId = 0;
		std::uint32_t m_slotIndex = 0;
		std::uint32_t m_generation = 0;
		std::uint32_t m_ownerWorkerIndex = 0;
		RIO_RQ m_requestQueue = RIO_INVALID_RQ;
		RIO_BUFFERID m_recvBufferId = RIO_INVALID_BUFFERID;
		RIO_BUFFERID m_sendRingBufferId = RIO_INVALID_BUFFERID;
		std::atomic<long> m_refCount = 0;
		std::atomic<bool> m_closing = false;
		std::atomic<bool> m_recvPending = false;
		std::atomic<bool> m_ownerSendDrainScheduled = false;
		std::atomic<std::uint32_t> m_queuedSendBufferCount = 0;
		std::atomic<std::uint32_t> m_maxObservedQueuedSendBufferCount = 0;
		std::atomic<std::uint64_t> m_ownerQueuedSendBytes = 0;
		std::atomic<std::uint64_t> m_maxObservedOwnerQueuedSendBytes = 0;
		std::atomic<std::uint32_t> m_observedSendRingUsedBytes = 0;
		std::atomic<std::uint32_t> m_observedSendRingInFlightBytes = 0;
		std::atomic<std::uint32_t> m_maxObservedSendRingUsedBytes = 0;
		std::atomic<std::uint32_t> m_lastObservedSendRingTouchThreadId = 0;
		NetworkLib::Packet::Buffer::FRecvBuffer m_recvBuffer;
		std::vector<char> m_recvStagingBuffer;
		std::vector<char> m_sendRingBuffer;
		std::size_t m_sendRingCapacityBytes = kDefaultSendRingSizeBytes;
		std::size_t m_sendRingReadOffset = 0;
		std::size_t m_sendRingWriteOffset = 0;
		std::size_t m_sendRingUsedBytes = 0;
		std::size_t m_sendRingInFlightBytes = 0;
		SRecvRequestContext m_recvRequestContext{};
		SSendRequestContext m_sendRequestContext{};
		NetworkLib::Containers::FLockFreeQueue<NetworkLib::Packet::Buffer::FPacketBuffer*> m_ownerSendPacketQueue;
		std::mutex m_requestQueueMutex;
		std::mutex m_sendRingMutex;
		bool m_trackedInRegistry = false;

	private:
		void ReleaseQueuedSendPackets() noexcept;
		void RegisterInSessionRegistry() noexcept;
		void ResetSendRingState() noexcept;
		void UpdateSendRingObservability() noexcept;
		void WriteSendBytes(const char* data, std::size_t length) noexcept;

	private:
		inline static NetworkLib::Memory::FTlsMemoryPoolManager<FRioSession, 128, 2> s_sessionPool{};
	};
}
