#pragma once

namespace NetworkLib::Packet::Buffer
{
	class FPacketBuffer
	{
	public:
		inline static constexpr std::size_t kDefaultPageSize = 4096;

		FPacketBuffer() = default;

		void Reset() noexcept
		{
			if (!IsPageReuseEnabled())
			{
				std::vector<char>().swap(m_buffer);
				return;
			}

			const std::size_t pageSize = GetConfiguredPageSize();
			if (m_buffer.capacity() < pageSize)
			{
				m_buffer.reserve(pageSize);
			}

			m_buffer.clear();
		}

		static FPacketBuffer* Create() noexcept
		{
			FPacketBuffer* packetBuffer = s_packetBufferPool.Alloc();
			packetBuffer->Reset();
			return packetBuffer;
		}

		static void Release(
			FPacketBuffer* packetBuffer) noexcept
		{
			if (packetBuffer == nullptr)
			{
				return;
			}

			packetBuffer->Reset();
			s_packetBufferPool.Free(packetBuffer);
		}

		std::vector<char>& GetBuffer() noexcept
		{
			return m_buffer;
		}

		const std::vector<char>& GetBuffer() const noexcept
		{
			return m_buffer;
		}

		static LONG GetPoolCapacity() noexcept
		{
			return s_packetBufferPool.GetCapacity();
		}

		static LONG GetPoolUsage() noexcept
		{
			return s_packetBufferPool.GetUseCount();
		}

		static void ConfigurePageReuse(
			bool enabled,
			std::size_t pageSize = kDefaultPageSize) noexcept
		{
			s_pageReuseEnabled.store(enabled, std::memory_order_relaxed);
			s_pageSize.store(pageSize == 0 ? kDefaultPageSize : pageSize, std::memory_order_relaxed);
		}

		static bool IsPageReuseEnabled() noexcept
		{
			return s_pageReuseEnabled.load(std::memory_order_relaxed);
		}

		static std::size_t GetConfiguredPageSize() noexcept
		{
			return s_pageSize.load(std::memory_order_relaxed);
		}

	private:
		std::vector<char> m_buffer;
		static std::atomic<bool> s_pageReuseEnabled;
		static std::atomic<std::size_t> s_pageSize;
		static NetworkLib::Memory::FTlsMemoryPoolManager<FPacketBuffer, 256, 2> s_packetBufferPool;
	};
}
