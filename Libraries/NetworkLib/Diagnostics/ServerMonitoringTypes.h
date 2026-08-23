#pragma once

namespace NetworkLib::Diagnostics
{
	struct SServerSessionObservationSnapshot final
	{
		std::uint64_t queuedSendBufferCount = 0;
		std::uint64_t maxObservedQueuedSendBufferCount = 0;
		std::uint64_t ownerQueuedSendBytes = 0;
		std::uint64_t maxObservedOwnerQueuedSendBytes = 0;
		std::uint64_t totalSendRingUsedBytes = 0;
		std::uint64_t totalSendRingInFlightBytes = 0;
		std::uint32_t maxCurrentSendRingUsedBytes = 0;
		std::uint32_t maxObservedSendRingUsedBytes = 0;
	};

	struct SServerPoolObservationSnapshot final
	{
		std::uint32_t sessionPoolCapacity = 0;
		std::uint32_t sessionPoolUsage = 0;
		std::uint32_t sendBufferPoolCapacity = 0;
		std::uint32_t sendBufferPoolUsage = 0;
		std::uint32_t packetBufferPoolCapacity = 0;
		std::uint32_t packetBufferPoolUsage = 0;
	};

	struct SServerMonitoringSnapshotInput final
	{
		SServerSessionObservationSnapshot session{};
		SServerPoolObservationSnapshot pools{};
	};
}
