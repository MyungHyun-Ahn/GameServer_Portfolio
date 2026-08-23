#pragma once

namespace NetworkLib::Diagnostics
{
	class FServerMonitoringRuntime final
	{
	public:
		void Reset() noexcept;

		void OnSessionAccepted() noexcept;
		void OnSessionClosed() noexcept;
		void OnReceiveBytes(std::uint64_t byteCount) noexcept;
		void OnReceivePacket() noexcept;
		void OnSendPacket(std::uint64_t byteCount) noexcept;
		void OnWsaRecvCall() noexcept;
		void OnWsaSendCall() noexcept;

		std::uint32_t GetActiveSessionCount(std::memory_order order = std::memory_order_relaxed) const noexcept;
		NetworkLib::Diagnostics::Rio::FRioSendMetricsRuntime& GetRioSendMetrics() noexcept;
		const NetworkLib::Diagnostics::Rio::FRioSendMetricsRuntime& GetRioSendMetrics() const noexcept;
		NetworkLib::Core::SServerStats BuildSnapshot(const SServerMonitoringSnapshotInput& snapshotInput) const noexcept;
		void PopulateSnapshot(NetworkLib::Core::SServerStats& stats) const noexcept;

	private:
		std::atomic<std::uint32_t> m_activeSessionCount = 0;
		std::atomic<std::uint64_t> m_acceptedSessionCount = 0;
		std::atomic<std::uint64_t> m_receivedPacketCount = 0;
		std::atomic<std::uint64_t> m_sentPacketCount = 0;
		std::atomic<std::uint64_t> m_receivedByteCount = 0;
		std::atomic<std::uint64_t> m_sentByteCount = 0;
		std::atomic<std::uint64_t> m_wsaRecvCallCount = 0;
		std::atomic<std::uint64_t> m_wsaSendCallCount = 0;
		NetworkLib::Diagnostics::Rio::FRioSendMetricsRuntime m_rioSendMetrics{};
	};
}
