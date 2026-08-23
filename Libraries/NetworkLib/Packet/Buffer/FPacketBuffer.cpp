#include "NetLibPch.h"

#include "Packet/Buffer/FPacketBuffer.h"

namespace NetworkLib::Packet::Buffer
{
	std::atomic<bool> FPacketBuffer::s_pageReuseEnabled{true};
	std::atomic<std::size_t> FPacketBuffer::s_pageSize{FPacketBuffer::kDefaultPageSize};
	NetworkLib::Memory::FTlsMemoryPoolManager<FPacketBuffer, 256, 2> FPacketBuffer::s_packetBufferPool{};
}
