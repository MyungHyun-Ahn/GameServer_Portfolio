#pragma once

namespace NetworkLib::Session
{
	class FIocpSession final : public ISession
	{
	public:
		static FIocpSession* Create() noexcept;
		static void Destroy(FIocpSession* session) noexcept;
		static LONG GetPoolCapacity() noexcept;
		static LONG GetPoolUsage() noexcept;

	public:
		enum class EIoType : std::uint32_t
		{
			Recv,
			Send
		};

		struct SIoContext
		{
			OVERLAPPED overlapped{};
			EIoType ioType = EIoType::Recv;
			FIocpSession* ownerSession = nullptr;

			void Prepare(EIoType newIoType, FIocpSession* newOwnerSession);
		};

	public:
		FIocpSession() = default;
		~FIocpSession() override;

		void Initialize(SOCKET socket,
			std::uint64_t sessionId,
			std::uint32_t slotIndex,
			std::uint32_t generation,
			std::size_t recvBufferCapacity);
		void Reset() noexcept;

		SOCKET GetSocket() const noexcept;
		void SetSocket(SOCKET socket) noexcept;

		std::uint64_t GetSessionId() const noexcept override;
		std::uint32_t GetSlotIndex() const noexcept override;
		std::uint32_t GetGeneration() const noexcept override;

		bool IsClosing() const noexcept override;
		bool TryMarkClosing() noexcept override;

		long AcquireRef() noexcept override;
		bool TryAcquireRef() noexcept;
		long ReleaseRef() noexcept override;

		void BuildRecvWsabufs(WSABUF (&outBuffers)[2], DWORD& outBufferCount) noexcept;
		bool CommitRecvBytes(std::size_t length) noexcept;
		NetworkLib::Packet::Buffer::FRecvBuffer& GetRecvBuffer() noexcept;
		const NetworkLib::Packet::Buffer::FRecvBuffer& GetRecvBuffer() const noexcept;

		SIoContext& GetRecvContext() noexcept;
		SIoContext& GetSendContext() noexcept;

		void EnqueueSendBuffer(NetworkLib::Packet::Buffer::FSendBuffer* sendBuffer) noexcept;
		bool TryBeginSend() noexcept;
		bool EndSend() noexcept;
		int BeginSendIo() noexcept;
		int FinishSendIo() noexcept;
		int GetMaxObservedConcurrentSendIoCount() const noexcept;
		std::uint32_t GetQueuedSendBufferCount() const noexcept override;
		std::uint32_t GetMaxObservedQueuedSendBufferCount() const noexcept override;
		bool FillSendBatch(std::size_t maxSendCount) noexcept;
		const std::vector<WSABUF>& GetSendWsabufs() const noexcept;
		void ReleaseActiveSendBuffers() noexcept;

	private:
		void ReleaseQueuedSendBuffers() noexcept;

	private:
		inline static constexpr std::size_t kDefaultSendBatchCapacity = 32;
		inline static constexpr std::uint32_t kSendInFlightFlag = 1u << 0;
		inline static constexpr std::uint32_t kSendPendingFlag = 1u << 1;
		SOCKET m_socket = INVALID_SOCKET;
		std::atomic<std::uint64_t> m_sessionId = 0;
		std::uint32_t m_slotIndex = 0;
		std::uint32_t m_generation = 0;
		std::atomic<long> m_refCount = 0;
		std::atomic<bool> m_closing = false;
		SIoContext m_recvContext{};
		SIoContext m_sendContext{};
		NetworkLib::Packet::Buffer::FRecvBuffer m_recvBuffer;
		NetworkLib::Containers::FLockFreeQueue<NetworkLib::Packet::Buffer::FSendBuffer*> m_sendQueue;
		std::vector<NetworkLib::Packet::Buffer::FSendBuffer*> m_activeSendBuffers;
		std::vector<WSABUF> m_sendWsabufs;
		std::atomic<std::uint32_t> m_sendState = 0;
		std::atomic<int> m_liveSendIoCount = 0;
		std::atomic<int> m_maxObservedConcurrentSendIoCount = 0;
		std::atomic<std::uint32_t> m_queuedSendBufferCount = 0;
		std::atomic<std::uint32_t> m_maxObservedQueuedSendBufferCount = 0;

	private:
		inline static NetworkLib::Memory::FTlsMemoryPoolManager<FIocpSession, 128, 2> s_sessionPool{};
	};
}
