#pragma once

namespace NetworkLib::Packet::Buffer
{
	class FRecvBuffer
	{
	public:
		FRecvBuffer() = default;
		explicit FRecvBuffer(
			std::size_t capacity)
		{
			Initialize(capacity);
		}

		void Initialize(
			std::size_t capacity)
		{
			m_buffer.assign(capacity, 0);
			Clear();
		}

		void Clear() noexcept
		{
			m_readOffset = 0;
			m_writeOffset = 0;
			m_usedSize = 0;
		}

		std::size_t GetCapacity() const noexcept
		{
			return m_buffer.size();
		}

		std::size_t GetUsedSize() const noexcept
		{
			return m_usedSize;
		}

		std::size_t GetFreeSize() const noexcept
		{
			return m_buffer.size() - m_usedSize;
		}

		bool CommitWrite(
			std::size_t writtenSize) noexcept
		{
			if (writtenSize > GetFreeSize())
			{
				return false;
			}

			if (!m_buffer.empty())
			{
				m_writeOffset = (m_writeOffset + writtenSize) % m_buffer.size();
			}
			m_usedSize += writtenSize;
			return true;
		}

		void BuildRecvWsabufs(
			WSABUF (&outBuffers)[2],
			DWORD& outBufferCount) noexcept
		{
			outBufferCount = 0;
			if (m_buffer.empty() || GetFreeSize() == 0)
			{
				return;
			}

			const std::size_t firstWritableSize = std::min(GetFreeSize(), m_buffer.size() - m_writeOffset);
			outBuffers[0].buf = m_buffer.data() + m_writeOffset;
			outBuffers[0].len = static_cast<ULONG>(firstWritableSize);
			outBufferCount = 1;

			const std::size_t remainingWritableSize = GetFreeSize() - firstWritableSize;
			if (remainingWritableSize > 0)
			{
				outBuffers[1].buf = m_buffer.data();
				outBuffers[1].len = static_cast<ULONG>(remainingWritableSize);
				outBufferCount = 2;
			}
		}

		bool Peek(
			void* destination,
			std::size_t length,
			std::size_t offset = 0) const noexcept
		{
			if (destination == nullptr || length == 0)
			{
				return length == 0;
			}

			if (offset + length > m_usedSize || m_buffer.empty())
			{
				return false;
			}

			char* destinationBytes = static_cast<char*>(destination);
			std::size_t currentOffset = (m_readOffset + offset) % m_buffer.size();
			std::size_t remaining = length;
			while (remaining > 0)
			{
				const std::size_t chunkSize = std::min(remaining, m_buffer.size() - currentOffset);
				std::memcpy(destinationBytes, m_buffer.data() + currentOffset, chunkSize);
				destinationBytes += chunkSize;
				remaining -= chunkSize;
				currentOffset = (currentOffset + chunkSize) % m_buffer.size();
			}

			return true;
		}

		bool CopyOut(
			std::size_t offset,
			void* destination,
			std::size_t length) const noexcept
		{
			return Peek(destination, length, offset);
		}

		bool Discard(
			std::size_t length) noexcept
		{
			if (length > m_usedSize)
			{
				return false;
			}

			if (!m_buffer.empty())
			{
				m_readOffset = (m_readOffset + length) % m_buffer.size();
			}
			m_usedSize -= length;
			if (m_usedSize == 0)
			{
				m_readOffset = 0;
				m_writeOffset = 0;
			}

			return true;
		}

		bool Dequeue(
			void* destination,
			std::size_t length) noexcept
		{
			if (!Peek(destination, length))
			{
				return false;
			}

			return Discard(length);
		}

		const char* GetReadPointer() const noexcept
		{
			if (m_buffer.empty() || m_usedSize == 0)
			{
				return nullptr;
			}

			return m_buffer.data() + m_readOffset;
		}

		bool EnsureContiguous(
			std::size_t length) noexcept
		{
			if (length > m_usedSize || m_buffer.empty())
			{
				return false;
			}

			const std::size_t contiguousSize = std::min(m_usedSize, m_buffer.size() - m_readOffset);
			if (contiguousSize >= length)
			{
				return true;
			}

			std::vector<char> linearizedBuffer(m_usedSize);
			if (!Peek(linearizedBuffer.data(), m_usedSize))
			{
				return false;
			}

			std::memcpy(m_buffer.data(), linearizedBuffer.data(), m_usedSize);
			m_readOffset = 0;
			m_writeOffset = m_usedSize % m_buffer.size();
			return true;
		}

	private:
		std::vector<char> m_buffer;
		std::size_t m_readOffset = 0;
		std::size_t m_writeOffset = 0;
		std::size_t m_usedSize = 0;
	};
}
