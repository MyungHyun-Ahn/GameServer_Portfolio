#include "NetLibPch.h"

#include "Packet/Buffer/FPacketBuffer.h"
#include "Packet/Buffer/FSendBuffer.h"
#include "Packet/Framing/PacketTypes.h"

namespace NetworkLib::Packet::Buffer
{
	void FSendBuffer::ConfigurePageReuse(
		const bool enabled,
		const std::size_t pageSize) noexcept
	{
		s_pageReuseEnabled.store(enabled, std::memory_order_relaxed);
		s_pageSize.store(pageSize == 0 ? kDefaultPageSize : pageSize, std::memory_order_relaxed);
	}

	bool FSendBuffer::InitializeSegmentPool(
		const bool registerForRio,
		const RIO_EXTENSION_FUNCTION_TABLE* rioFunctionTable,
		const std::uint32_t maxSessionCount) noexcept
	{
		const std::uint32_t regionsPerBucket = std::clamp<std::uint32_t>((maxSessionCount + 31u) / 32u, 4u, 16u);
		FSendSegmentPool::Configure(FSendSegmentPool::kDefaultRegionSizeBytes, regionsPerBucket);
		return FSendSegmentPool::Initialize(registerForRio, rioFunctionTable);
	}

	void FSendBuffer::ShutdownSegmentPool(
		const RIO_EXTENSION_FUNCTION_TABLE* rioFunctionTable) noexcept
	{
		FSendSegmentPool::Shutdown(rioFunctionTable);
	}

	FSendBuffer* FSendBuffer::Create(
		std::vector<char>&& buffer) noexcept
	{
		FSendBuffer* sendBuffer = s_sendBufferPool.Alloc();
		sendBuffer->Reset();
		sendBuffer->InitializeFromBuffer(std::move(buffer));
		return sendBuffer;
	}

	FSendBuffer* FSendBuffer::Create(
		NetworkLib::Packet::Buffer::FPacketBuffer* packetBuffer) noexcept
	{
		if (packetBuffer == nullptr)
		{
			return nullptr;
		}

		FSendBuffer* sendBuffer = s_sendBufferPool.Alloc();
		sendBuffer->Reset();
		sendBuffer->InitializeFromBuffer(std::move(packetBuffer->GetBuffer()));
		NetworkLib::Packet::Buffer::FPacketBuffer::Release(packetBuffer);
		return sendBuffer;
	}

	FSendBuffer* FSendBuffer::Create(
		const NetworkLib::Packet::Framing::SFramedPacketBufferParts& packetParts,
		std::vector<char>&& payloadBuffer) noexcept
	{
		FSendBuffer* sendBuffer = s_sendBufferPool.Alloc();
		sendBuffer->Reset();
		sendBuffer->InitializeFromParts(packetParts, payloadBuffer.data(), payloadBuffer.size());
		return sendBuffer;
	}

	FSendBuffer* FSendBuffer::Create(
		const NetworkLib::Packet::Framing::SFramedPacketBufferParts& packetParts,
		NetworkLib::Packet::Buffer::FPacketBuffer* packetBuffer) noexcept
	{
		if (packetBuffer == nullptr)
		{
			return nullptr;
		}

		FSendBuffer* sendBuffer = s_sendBufferPool.Alloc();
		sendBuffer->Reset();
		const std::vector<char>& payloadBuffer = packetBuffer->GetBuffer();
		sendBuffer->InitializeFromParts(packetParts, payloadBuffer.data(), payloadBuffer.size());
		NetworkLib::Packet::Buffer::FPacketBuffer::Release(packetBuffer);
		return sendBuffer;
	}

	void FSendBuffer::Release(
		FSendBuffer* sendBuffer) noexcept
	{
		if (sendBuffer == nullptr)
		{
			return;
		}

		sendBuffer->Reset();
		s_sendBufferPool.Free(sendBuffer);
	}

	LONG FSendBuffer::GetPoolCapacity() noexcept
	{
		return s_sendBufferPool.GetCapacity();
	}

	LONG FSendBuffer::GetPoolUsage() noexcept
	{
		return s_sendBufferPool.GetUseCount();
	}

	const char* FSendBuffer::GetData() const noexcept
	{
		if (m_usesSegmentPool)
		{
			return m_segmentAllocation.data;
		}

		return m_buffer.empty() ? nullptr : m_buffer.data();
	}

	char* FSendBuffer::GetData() noexcept
	{
		if (m_usesSegmentPool)
		{
			return m_segmentAllocation.data;
		}

		return m_buffer.empty() ? nullptr : m_buffer.data();
	}

	std::size_t FSendBuffer::GetSize() const noexcept
	{
		return m_usesSegmentPool ? m_length : m_buffer.size();
	}

	void FSendBuffer::AppendWsabufs(
		std::vector<WSABUF>& outWsabufs) noexcept
	{
		WSABUF wsabuf{};
		wsabuf.buf = GetData();
		wsabuf.len = static_cast<ULONG>(GetSize());
		outWsabufs.push_back(wsabuf);
	}

	bool FSendBuffer::TryBuildRioBuf(
		RIO_BUF& outRioBuffer) const noexcept
	{
		if (!m_usesSegmentPool || m_segmentAllocation.rioBufferId == RIO_INVALID_BUFFERID || m_length == 0)
		{
			return false;
		}

		outRioBuffer.BufferId = m_segmentAllocation.rioBufferId;
		outRioBuffer.Offset = m_segmentAllocation.offset;
		outRioBuffer.Length = static_cast<ULONG>(m_length);
		return true;
	}

	bool FSendBuffer::InitializeFromBuffer(
		std::vector<char>&& buffer) noexcept
	{
		m_length = 0;
		m_usesSegmentPool = false;
		if (!buffer.empty() && FSendSegmentPool::TryAllocate(buffer.size(), m_segmentAllocation))
		{
			std::memcpy(m_segmentAllocation.data, buffer.data(), buffer.size());
			m_length = buffer.size();
			m_usesSegmentPool = true;
			return true;
		}

		m_buffer = std::move(buffer);
		return true;
	}

	bool FSendBuffer::InitializeFromParts(
		const NetworkLib::Packet::Framing::SFramedPacketBufferParts& packetParts,
		const char* payloadData,
		const std::size_t payloadLength) noexcept
	{
		const std::size_t totalLength = static_cast<std::size_t>(packetParts.headerLength) + payloadLength;
		m_length = 0;
		m_usesSegmentPool = false;
		if (totalLength > 0 && FSendSegmentPool::TryAllocate(totalLength, m_segmentAllocation))
		{
			char* destination = m_segmentAllocation.data;
			if (packetParts.headerLength > 0)
			{
				std::memcpy(destination, packetParts.headerBytes.data(), packetParts.headerLength);
			}

			if (payloadLength > 0 && payloadData != nullptr)
			{
				std::memcpy(destination + packetParts.headerLength, payloadData, payloadLength);
			}

			m_length = totalLength;
			m_usesSegmentPool = true;
			return true;
		}

		m_buffer.resize(totalLength);
		if (packetParts.headerLength > 0)
		{
			std::memcpy(m_buffer.data(), packetParts.headerBytes.data(), packetParts.headerLength);
		}

		if (payloadLength > 0 && payloadData != nullptr)
		{
			std::memcpy(m_buffer.data() + packetParts.headerLength, payloadData, payloadLength);
		}

		return true;
	}

	void FSendBuffer::Reset() noexcept
	{
		if (m_usesSegmentPool)
		{
			FSendSegmentPool::Release(m_segmentAllocation);
			m_usesSegmentPool = false;
		}
		m_length = 0;

		if (!s_pageReuseEnabled.load(std::memory_order_relaxed))
		{
			std::vector<char>().swap(m_buffer);
			return;
		}

		const std::size_t pageSize = s_pageSize.load(std::memory_order_relaxed);
		if (m_buffer.capacity() < pageSize)
		{
			m_buffer.reserve(pageSize == 0 ? kDefaultPageSize : pageSize);
		}

		m_buffer.clear();
	}
}
