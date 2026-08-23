#include "TimingMetricsSmokeTestPch.h"

namespace
{
	using Foundation::Diagnostics::FTimingCsvLogger;
	using Foundation::Diagnostics::FTimingMetricIndex;
	using Foundation::Diagnostics::FTimingMetricsRuntime;
	using Foundation::Diagnostics::FTimingScope;
	using Foundation::Diagnostics::FTimingThreadLocalCollector;
	using Foundation::Diagnostics::STimingMetricsConfig;

	constexpr FTimingMetricIndex kScopedWorkMetric = 0;
	constexpr FTimingMetricIndex kQueueWaitMetric = 1;

	bool Contains(
		const std::string& text,
		const std::string_view expected)
	{
		return text.find(expected) != std::string::npos;
	}
}

int main(
	const int argumentCount,
	char* arguments[])
{
	if (argumentCount <= 0 || arguments[0] == nullptr)
	{
		std::cerr << "[FAIL] Executable path is unavailable.\n";
		return 1;
	}

	STimingMetricsConfig config{};
	config.flushIntervalSeconds = 60;
	config.metricNames = {"ScopedWork", "QueueWait"};

	FTimingMetricsRuntime runtime(config);
	const std::filesystem::path csvPath = std::filesystem::absolute(arguments[0]).parent_path() / "timing_metrics_smoke.csv";
	FTimingCsvLogger csvLogger(runtime, csvPath.string());
	csvLogger.Start();

	const auto crossThreadQueueWait = [&runtime]()
	{
		FTimingThreadLocalCollector enqueueCollector(&runtime);
		return enqueueCollector.BeginSample(kQueueWaitMetric, 202);
	}();

	std::vector<std::thread> workers;
	workers.reserve(2);
	for (std::uint64_t workerIndex = 0; workerIndex < 2; ++workerIndex)
	{
		workers.emplace_back(
			[&runtime, &crossThreadQueueWait, workerIndex]()
			{
				FTimingThreadLocalCollector collector(&runtime);
				const std::uint64_t contextBase = workerIndex * 1'000;

				{
					FTimingScope scope(collector, kScopedWorkMetric, contextBase + 101);
					std::this_thread::sleep_for(std::chrono::milliseconds(4));
				}

				if (workerIndex == 0)
				{
					collector.RecordSample(crossThreadQueueWait);
				}
				else
				{
					const auto pendingQueueWait = collector.BeginSample(kQueueWaitMetric, contextBase + 202);
					std::this_thread::sleep_for(std::chrono::milliseconds(2));
					collector.RecordSample(pendingQueueWait);
				}

				collector.RecordDuration(kScopedWorkMetric, std::chrono::milliseconds(3), contextBase + 303);

				FTimingScope canceledScope(collector, kQueueWaitMetric, contextBase + 404);
				canceledScope.Cancel();
			});
	}

	for (std::thread& worker : workers)
	{
		worker.join();
	}

	csvLogger.Stop();

	std::ifstream csvStream(csvPath);
	if (!csvStream.is_open())
	{
		std::cerr << "[FAIL] CSV was not created: " << csvPath.string() << '\n';
		return 1;
	}

	const std::string csvText((std::istreambuf_iterator<char>(csvStream)), std::istreambuf_iterator<char>());
	if (!Contains(csvText, "bucket_start_local,metric") || !Contains(csvText, ",ScopedWork,4,") || !Contains(csvText, ",QueueWait,2,") ||
		!Contains(csvText, ",101,") || !Contains(csvText, ",202,") || Contains(csvText, ",404,"))
	{
		std::cerr << "[FAIL] CSV content did not contain the expected aggregates.\n" << csvText;
		return 1;
	}

	std::cout << "[PASS] Timing metrics CSV verified: " << csvPath.string() << '\n';
	return 0;
}
