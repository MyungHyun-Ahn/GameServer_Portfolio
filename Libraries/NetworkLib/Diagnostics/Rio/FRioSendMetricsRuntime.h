#pragma once

namespace NetworkLib::Session
{
	class FRioSession;
}

namespace NetworkLib::Diagnostics::Rio
{
	class FRioSendMetricsRuntime final : public Foundation::Diagnostics::FTlsCollectorRuntime
	{
	public:
		void Reset() noexcept;

		void RecordSendPrepareSample(std::uint64_t durationNs) noexcept;
		void RecordSendRingTouch(NetworkLib::Session::FRioSession& sessionContext) noexcept;
		void RecordDirectSendRingLockSample(std::uint64_t waitDurationNs, std::uint64_t holdDurationNs) noexcept;

		void PopulateSnapshot(NetworkLib::Core::SServerStats& stats) const noexcept;
		std::uint32_t GetEpoch() const noexcept;

	private:
		std::atomic<std::uint32_t> m_epoch = 1;
	};
}
