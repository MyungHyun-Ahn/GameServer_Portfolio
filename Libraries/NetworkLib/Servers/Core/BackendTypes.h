#pragma once

namespace Foundation
{
	class ILogger;
}

namespace NetworkLib::Crypto
{
	class IPacketCipher;
}

namespace NetworkLib::Packet::Framing
{
	class IPacketFramer;
}

namespace NetworkLib::Core
{
	enum class EBackendKind : std::uint32_t
	{
		Iocp = 0,
		Rio = 1,
		BoostAsio = 2
	};

	enum class ERioSendDispatchMode : std::uint32_t
	{
		Direct = 0,
		OwnerThread = 1
	};

	struct SServerConfig
	{
		EBackendKind backendKind = EBackendKind::Iocp;
		ERioSendDispatchMode rioSendDispatchMode = ERioSendDispatchMode::Direct;
		std::string bindIp = "127.0.0.1";
		std::uint16_t port = 19000;
		std::uint32_t workerThreadCount = 2;
		std::uint32_t maxSessionCount = 64;
		std::uint32_t recvBufferSize = 1024;
		std::int32_t socketSendBufferBytes = -1;
		std::uint32_t rioSendRingSizeBytes = 64u * 1024u;
		bool enablePageBufferReuse = true;
		std::uint32_t pageBufferSize = 4096;
		Foundation::SLogConfig logConfig{};
		std::shared_ptr<Foundation::ILogger> logger;
		std::shared_ptr<NetworkLib::Crypto::IPacketCipher> packetCipher;
		std::shared_ptr<NetworkLib::Packet::Framing::IPacketFramer> packetFramer;
	};

	struct SServerStats
	{
		std::uint32_t activeSessionCount = 0;
		std::uint64_t acceptedSessionCount = 0;
		std::uint64_t receivedPacketCount = 0;
		std::uint64_t sentPacketCount = 0;
		std::uint64_t receivedByteCount = 0;
		std::uint64_t sentByteCount = 0;
		std::uint64_t wsaRecvCallCount = 0;
		std::uint64_t wsaSendCallCount = 0;
		std::uint64_t queuedSendBufferCount = 0;
		std::uint64_t maxObservedQueuedSendBufferCount = 0;
		std::uint64_t ownerQueuedSendBytes = 0;
		std::uint64_t maxObservedOwnerQueuedSendBytes = 0;
		std::uint64_t totalSendRingUsedBytes = 0;
		std::uint64_t totalSendRingInFlightBytes = 0;
		std::uint32_t maxCurrentSendRingUsedBytes = 0;
		std::uint32_t maxObservedSendRingUsedBytes = 0;
		std::uint64_t rioSendPrepareCount = 0;
		std::uint64_t rioSendPrepareTotalNs = 0;
		std::uint64_t rioSendPrepareMaxNs = 0;
		std::uint64_t rioSendRingTouchCount = 0;
		std::uint64_t rioSendRingCrossThreadTouchCount = 0;
		std::uint64_t rioDirectSendRingLockCount = 0;
		std::uint64_t rioDirectSendRingLockWaitTotalNs = 0;
		std::uint64_t rioDirectSendRingLockWaitMaxNs = 0;
		std::uint64_t rioDirectSendRingLockHoldTotalNs = 0;
		std::uint64_t rioDirectSendRingLockHoldMaxNs = 0;
		std::uint32_t sessionPoolCapacity = 0;
		std::uint32_t sessionPoolUsage = 0;
		std::uint32_t sendBufferPoolCapacity = 0;
		std::uint32_t sendBufferPoolUsage = 0;
		std::uint32_t packetBufferPoolCapacity = 0;
		std::uint32_t packetBufferPoolUsage = 0;
	};
}
