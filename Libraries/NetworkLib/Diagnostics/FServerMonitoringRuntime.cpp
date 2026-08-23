#include "NetLibPch.h"

#include "Diagnostics/FServerMonitoringRuntime.h"

namespace NetworkLib::Diagnostics
{
	void FServerMonitoringRuntime::Reset() noexcept
	{
		m_activeSessionCount.store(0, std::memory_order_relaxed);
		m_acceptedSessionCount.store(0, std::memory_order_relaxed);
		m_receivedPacketCount.store(0, std::memory_order_relaxed);
		m_sentPacketCount.store(0, std::memory_order_relaxed);
		m_receivedByteCount.store(0, std::memory_order_relaxed);
		m_sentByteCount.store(0, std::memory_order_relaxed);
		m_wsaRecvCallCount.store(0, std::memory_order_relaxed);
		m_wsaSendCallCount.store(0, std::memory_order_relaxed);
		m_rioSendMetrics.Reset();
	}

	void FServerMonitoringRuntime::OnSessionAccepted() noexcept
	{
		m_activeSessionCount.fetch_add(1, std::memory_order_relaxed);
		m_acceptedSessionCount.fetch_add(1, std::memory_order_relaxed);
	}

	void FServerMonitoringRuntime::OnSessionClosed() noexcept
	{
		m_activeSessionCount.fetch_sub(1, std::memory_order_relaxed);
	}

	void FServerMonitoringRuntime::OnReceiveBytes(
		const std::uint64_t byteCount) noexcept
	{
		m_receivedByteCount.fetch_add(byteCount, std::memory_order_relaxed);
	}

	void FServerMonitoringRuntime::OnReceivePacket() noexcept
	{
		m_receivedPacketCount.fetch_add(1, std::memory_order_relaxed);
	}

	void FServerMonitoringRuntime::OnSendPacket(
		const std::uint64_t byteCount) noexcept
	{
		m_sentPacketCount.fetch_add(1, std::memory_order_relaxed);
		m_sentByteCount.fetch_add(byteCount, std::memory_order_relaxed);
	}

	void FServerMonitoringRuntime::OnWsaRecvCall() noexcept
	{
		m_wsaRecvCallCount.fetch_add(1, std::memory_order_relaxed);
	}

	void FServerMonitoringRuntime::OnWsaSendCall() noexcept
	{
		m_wsaSendCallCount.fetch_add(1, std::memory_order_relaxed);
	}

	std::uint32_t FServerMonitoringRuntime::GetActiveSessionCount(
		const std::memory_order order) const noexcept
	{
		return m_activeSessionCount.load(order);
	}

	NetworkLib::Diagnostics::Rio::FRioSendMetricsRuntime& FServerMonitoringRuntime::GetRioSendMetrics() noexcept
	{
		return m_rioSendMetrics;
	}

	const NetworkLib::Diagnostics::Rio::FRioSendMetricsRuntime& FServerMonitoringRuntime::GetRioSendMetrics() const noexcept
	{
		return m_rioSendMetrics;
	}

	NetworkLib::Core::SServerStats FServerMonitoringRuntime::BuildSnapshot(
		const SServerMonitoringSnapshotInput& snapshotInput) const noexcept
	{
		NetworkLib::Core::SServerStats stats{};
		PopulateSnapshot(stats);
		m_rioSendMetrics.PopulateSnapshot(stats);

		stats.queuedSendBufferCount = snapshotInput.session.queuedSendBufferCount;
		stats.maxObservedQueuedSendBufferCount = snapshotInput.session.maxObservedQueuedSendBufferCount;
		stats.ownerQueuedSendBytes = snapshotInput.session.ownerQueuedSendBytes;
		stats.maxObservedOwnerQueuedSendBytes = snapshotInput.session.maxObservedOwnerQueuedSendBytes;
		stats.totalSendRingUsedBytes = snapshotInput.session.totalSendRingUsedBytes;
		stats.totalSendRingInFlightBytes = snapshotInput.session.totalSendRingInFlightBytes;
		stats.maxCurrentSendRingUsedBytes = snapshotInput.session.maxCurrentSendRingUsedBytes;
		stats.maxObservedSendRingUsedBytes = snapshotInput.session.maxObservedSendRingUsedBytes;

		stats.sessionPoolCapacity = snapshotInput.pools.sessionPoolCapacity;
		stats.sessionPoolUsage = snapshotInput.pools.sessionPoolUsage;
		stats.sendBufferPoolCapacity = snapshotInput.pools.sendBufferPoolCapacity;
		stats.sendBufferPoolUsage = snapshotInput.pools.sendBufferPoolUsage;
		stats.packetBufferPoolCapacity = snapshotInput.pools.packetBufferPoolCapacity;
		stats.packetBufferPoolUsage = snapshotInput.pools.packetBufferPoolUsage;
		return stats;
	}

	void FServerMonitoringRuntime::PopulateSnapshot(
		NetworkLib::Core::SServerStats& stats) const noexcept
	{
		stats.activeSessionCount = m_activeSessionCount.load(std::memory_order_relaxed);
		stats.acceptedSessionCount = m_acceptedSessionCount.load(std::memory_order_relaxed);
		stats.receivedPacketCount = m_receivedPacketCount.load(std::memory_order_relaxed);
		stats.sentPacketCount = m_sentPacketCount.load(std::memory_order_relaxed);
		stats.receivedByteCount = m_receivedByteCount.load(std::memory_order_relaxed);
		stats.sentByteCount = m_sentByteCount.load(std::memory_order_relaxed);
		stats.wsaRecvCallCount = m_wsaRecvCallCount.load(std::memory_order_relaxed);
		stats.wsaSendCallCount = m_wsaSendCallCount.load(std::memory_order_relaxed);
	}
}
