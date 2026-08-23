#include "AuctionDummyClientPch.h"

#include "LoadTest/FLoadTestMetrics.h"

namespace
{
	double CalculatePercentile(
		const std::vector<double>& values,
		const double percentile)
	{
		if (values.empty())
			return 0.0;

		std::vector<double> sortedValues = values;
		std::sort(sortedValues.begin(), sortedValues.end());
		const double scaledIndex = percentile * static_cast<double>(sortedValues.size() - 1);
		const std::size_t index = static_cast<std::size_t>(std::ceil(scaledIndex));
		return sortedValues[index];
	}

}

namespace AuctionDummyClient::LoadTest
{
	void FLoadTestMetrics::RecordSent(
		const std::string_view operation)
	{
		++m_operations[std::string(operation)].sent;
	}

	void FLoadTestMetrics::RecordResponse(
		const std::string_view operation,
		const bool succeeded,
		const std::chrono::steady_clock::duration latency)
	{
		SOperationMetrics& metrics = m_operations[std::string(operation)];
		++metrics.completed;
		if (succeeded)
			++metrics.succeeded;
		else
			++metrics.failed;
		metrics.latencyMilliseconds.push_back(std::chrono::duration<double, std::milli>(latency).count());
	}

	void FLoadTestMetrics::RecordExpectedRejection(
		const std::string_view operation,
		const std::chrono::steady_clock::duration latency)
	{
		SOperationMetrics& metrics = m_operations[std::string(operation)];
		++metrics.completed;
		++metrics.expectedRejected;
		metrics.latencyMilliseconds.push_back(std::chrono::duration<double, std::milli>(latency).count());
	}

	void FLoadTestMetrics::RecordNotification(
		const std::string_view notification)
	{
		++m_notifications[std::string(notification)];
	}

	void FLoadTestMetrics::RecordTimeout(
		const std::string_view operation)
	{
		SOperationMetrics& metrics = m_operations[std::string(operation)];
		++metrics.timedOut;
		++metrics.failed;
	}

	void FLoadTestMetrics::RecordConnectFailure() noexcept
	{
		++m_connectFailures;
	}

	void FLoadTestMetrics::RecordNetworkFailure() noexcept
	{
		++m_networkFailures;
	}

	void FLoadTestMetrics::RecordUnexpectedPacket() noexcept
	{
		++m_unexpectedPackets;
	}

	std::uint64_t FLoadTestMetrics::GetCompletedCount() const noexcept
	{
		std::uint64_t total = 0;
		for (const auto& [operation, metrics] : m_operations)
			total += metrics.completed;
		return total;
	}

	std::uint64_t FLoadTestMetrics::GetUnexpectedFailureCount() const noexcept
	{
		std::uint64_t total = m_connectFailures + m_networkFailures + m_unexpectedPackets;
		for (const auto& [operation, metrics] : m_operations)
			total += metrics.failed;
		return total;
	}

	void FLoadTestMetrics::PrintSummary(
		const std::chrono::steady_clock::duration elapsed,
		const std::size_t connectedUsers,
		const std::size_t authenticatedUsers,
		const bool finalSummary) const
	{
		const double elapsedSeconds = std::max(0.001, std::chrono::duration<double>(elapsed).count());
		const std::uint64_t completed = GetCompletedCount();
		std::cout << (finalSummary ? "AUCTION_LOAD_TEST_RESULT" : "AUCTION_LOAD_TEST_PROGRESS") << " elapsedSeconds=" << std::fixed
				  << std::setprecision(1) << elapsedSeconds << " connectedUsers=" << connectedUsers
				  << " authenticatedUsers=" << authenticatedUsers << " completed=" << completed << " throughput=" << std::setprecision(2)
				  << (static_cast<double>(completed) / elapsedSeconds) << " connectFailures=" << m_connectFailures
				  << " networkFailures=" << m_networkFailures << " unexpectedPackets=" << m_unexpectedPackets << '\n';
		for (const auto& [notification, count] : m_notifications)
			std::cout << "AUCTION_LOAD_TEST_NOTIFICATION name=" << notification << " count=" << count << '\n';

		for (const auto& [operation, metrics] : m_operations)
		{
			std::cout << "AUCTION_LOAD_TEST_OPERATION"
					  << " name=" << operation << " sent=" << metrics.sent << " completed=" << metrics.completed
					  << " success=" << metrics.succeeded << " expectedRejected=" << metrics.expectedRejected
					  << " failed=" << metrics.failed << " timeout=" << metrics.timedOut << " p50Ms=" << std::setprecision(2)
					  << CalculatePercentile(metrics.latencyMilliseconds, 0.50)
					  << " p95Ms=" << CalculatePercentile(metrics.latencyMilliseconds, 0.95)
					  << " p99Ms=" << CalculatePercentile(metrics.latencyMilliseconds, 0.99) << " maxMs="
					  << (metrics.latencyMilliseconds.empty()
								 ? 0.0
								 : *std::max_element(metrics.latencyMilliseconds.begin(), metrics.latencyMilliseconds.end()))
					  << '\n';
		}
	}
}
