#pragma once

namespace NetworkLib::Packet::Framing
{
	struct SFramedPacketBufferParts;
}

namespace NetworkLib::Packet::Buffer
{
	class FPacketBuffer;

	class FSendBuffer
	{
	public:
		inline static constexpr std::size_t kDefaultPageSize = 4096;

		FSendBuffer() = default;

		static void ConfigurePageReuse(bool enabled, std::size_t pageSize = kDefaultPageSize) noexcept;
		static bool InitializeSegmentPool(bool registerForRio,
			const RIO_EXTENSION_FUNCTION_TABLE* rioFunctionTable,
			std::uint32_t maxSessionCount) noexcept;
		static void ShutdownSegmentPool(const RIO_EXTENSION_FUNCTION_TABLE* rioFunctionTable) noexcept;

		static FSendBuffer* Create(std::vector<char>&& buffer) noexcept;
		static FSendBuffer* Create(NetworkLib::Packet::Buffer::FPacketBuffer* packetBuffer) noexcept;
		static FSendBuffer* Create(const NetworkLib::Packet::Framing::SFramedPacketBufferParts& packetParts,
			std::vector<char>&& payloadBuffer) noexcept;
		static FSendBuffer* Create(const NetworkLib::Packet::Framing::SFramedPacketBufferParts& packetParts,
			NetworkLib::Packet::Buffer::FPacketBuffer* packetBuffer) noexcept;
		static void Release(FSendBuffer* sendBuffer) noexcept;

		static LONG GetPoolCapacity() noexcept;
		static LONG GetPoolUsage() noexcept;

		const char* GetData() const noexcept;
		char* GetData() noexcept;
		std::size_t GetSize() const noexcept;
		void AppendWsabufs(std::vector<WSABUF>& outWsabufs) noexcept;
		bool TryBuildRioBuf(RIO_BUF& outRioBuffer) const noexcept;

	private:
		bool InitializeFromBuffer(std::vector<char>&& buffer) noexcept;
		bool InitializeFromParts(const NetworkLib::Packet::Framing::SFramedPacketBufferParts& packetParts,
			const char* payloadData,
			std::size_t payloadLength) noexcept;
		void Reset() noexcept;

	private:
		FSendSegmentPool::SAllocation m_segmentAllocation{};
		std::vector<char> m_buffer;
		std::size_t m_length = 0;
		bool m_usesSegmentPool = false;

	private:
		inline static std::atomic<bool> s_pageReuseEnabled{true};
		inline static std::atomic<std::size_t> s_pageSize{kDefaultPageSize};
		inline static NetworkLib::Memory::FTlsMemoryPoolManager<FSendBuffer, 256, 2> s_sendBufferPool{};
	};
}
