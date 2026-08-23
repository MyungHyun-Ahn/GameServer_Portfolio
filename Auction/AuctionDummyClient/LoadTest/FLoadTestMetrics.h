#pragma once

namespace AuctionDummyClient::LoadTest
{
	struct SOperationMetrics final
	{
		std::uint64_t sent = 0;
		std::uint64_t completed = 0;
		std::uint64_t succeeded = 0;
		std::uint64_t expectedRejected = 0;
		std::uint64_t failed = 0;
		std::uint64_t timedOut = 0;
		std::vector<double> latencyMilliseconds;
	};

	class FLoadTestMetrics final
	{
	public:
		void RecordSent(std::string_view operation);
		void RecordResponse(std::string_view operation, bool succeeded, std::chrono::steady_clock::duration latency);
		void RecordExpectedRejection(std::string_view operation, std::chrono::steady_clock::duration latency);
		void RecordNotification(std::string_view notification);
		void RecordTimeout(std::string_view operation);
		void RecordConnectFailure() noexcept;
		void RecordNetworkFailure() noexcept;
		void RecordUnexpectedPacket() noexcept;

		std::uint64_t GetCompletedCount() const noexcept;
		std::uint64_t GetUnexpectedFailureCount() const noexcept;
		void PrintSummary(std::chrono::steady_clock::duration elapsed,
			std::size_t connectedUsers,
			std::size_t authenticatedUsers,
			bool finalSummary) const;

	private:
		std::map<std::string, SOperationMetrics, std::less<>> m_operations;
		std::uint64_t m_connectFailures = 0;
		std::uint64_t m_networkFailures = 0;
		std::uint64_t m_unexpectedPackets = 0;
		std::map<std::string, std::uint64_t, std::less<>> m_notifications;
	};
}
