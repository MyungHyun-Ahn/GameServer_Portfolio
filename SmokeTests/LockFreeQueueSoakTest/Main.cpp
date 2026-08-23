#include "LockFreeQueueSoakTestPch.h"

#include "Containers/FLockFreeQueue.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace
{
	struct SQueueSoakConfig
	{
		int durationSeconds = 600;
		int producerCount = 4;
		int consumerCount = 4;
		int reportIntervalSeconds = 10;
		std::string logPath;
	};

	class FLogger
	{
	public:
		explicit FLogger(
			const std::string& logPath)
		{
			m_stream.open(logPath, std::ios::out | std::ios::trunc);
		}

		void WriteLine(
			const std::string& message)
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			std::cout << message << "\n";
			if (m_stream.is_open())
			{
				m_stream << message << "\n";
				m_stream.flush();
			}
		}

	private:
		std::mutex m_mutex;
		std::ofstream m_stream;
	};

	std::string BuildDefaultLogPath(
		const char* executablePath,
		const char* baseName)
	{
		namespace fs = std::filesystem;

		fs::path outputDirectory = executablePath != nullptr ? fs::path(executablePath).parent_path() : fs::current_path();

		const auto now = std::chrono::system_clock::now();
		const std::time_t timeValue = std::chrono::system_clock::to_time_t(now);
		std::tm localTime{};
		localtime_s(&localTime, &timeValue);

		std::ostringstream fileName;
		fileName << baseName << "_" << std::put_time(&localTime, "%Y%m%d_%H%M%S") << ".log";
		return (outputDirectory / fileName.str()).string();
	}

	std::int64_t ParseInt64(
		const char* value,
		std::int64_t fallback) noexcept
	{
		if (value == nullptr)
		{
			return fallback;
		}

		char* endPointer = nullptr;
		const long long parsedValue = std::strtoll(value, &endPointer, 10);
		return endPointer == value ? fallback : parsedValue;
	}

	SQueueSoakConfig ParseConfig(
		int argc,
		char* argv[]) noexcept
	{
		SQueueSoakConfig config;
		for (int index = 1; index < argc; ++index)
		{
			const std::string argument = argv[index];
			if (argument == "--seconds" && index + 1 < argc)
			{
				config.durationSeconds = static_cast<int>(ParseInt64(argv[++index], config.durationSeconds));
			}
			else if (argument == "--producers" && index + 1 < argc)
			{
				config.producerCount = static_cast<int>(ParseInt64(argv[++index], config.producerCount));
			}
			else if (argument == "--consumers" && index + 1 < argc)
			{
				config.consumerCount = static_cast<int>(ParseInt64(argv[++index], config.consumerCount));
			}
			else if (argument == "--report-seconds" && index + 1 < argc)
			{
				config.reportIntervalSeconds = static_cast<int>(ParseInt64(argv[++index], config.reportIntervalSeconds));
			}
			else if (argument == "--log-path" && index + 1 < argc)
			{
				config.logPath = argv[++index];
			}
		}

		config.durationSeconds = (std::max)(1, config.durationSeconds);
		config.producerCount = (std::max)(1, config.producerCount);
		config.consumerCount = (std::max)(1, config.consumerCount);
		config.reportIntervalSeconds = (std::max)(1, config.reportIntervalSeconds);
		return config;
	}
}

int main(
	int argc,
	char* argv[])
{
	using TQueue = NetworkLib::Containers::FLockFreeQueue<std::uint64_t>;

	SQueueSoakConfig config = ParseConfig(argc, argv);
	if (config.logPath.empty())
	{
		config.logPath = BuildDefaultLogPath(argc > 0 ? argv[0] : nullptr, "lockfree_queue_soak");
	}

	FLogger logger(config.logPath);
	TQueue queue;
	std::atomic<bool> stopRequested = false;
	std::atomic<int> activeProducers = config.producerCount;
	std::atomic<std::uint64_t> nextValue = 0;
	std::atomic<std::uint64_t> producedCount = 0;
	std::atomic<std::uint64_t> consumedCount = 0;
	std::atomic<std::uint64_t> producedSum = 0;
	std::atomic<std::uint64_t> consumedSum = 0;

	{
		std::ostringstream line;
		line << "LockFreeQueueSoakTest start"
			 << " seconds=" << config.durationSeconds << " producers=" << config.producerCount << " consumers=" << config.consumerCount
			 << " report=" << config.reportIntervalSeconds << "s"
			 << " logPath=" << config.logPath;
		logger.WriteLine(line.str());
	}

	std::vector<std::thread> producerThreads;
	producerThreads.reserve(config.producerCount);
	for (int producerIndex = 0; producerIndex < config.producerCount; ++producerIndex)
	{
		producerThreads.emplace_back(
			[&]()
			{
				while (!stopRequested.load(std::memory_order_acquire))
				{
					const std::uint64_t value = nextValue.fetch_add(1, std::memory_order_relaxed) + 1;
					queue.Enqueue(value);
					producedCount.fetch_add(1, std::memory_order_relaxed);
					producedSum.fetch_add(value, std::memory_order_relaxed);
				}

				activeProducers.fetch_sub(1, std::memory_order_release);
			});
	}

	std::vector<std::thread> consumerThreads;
	consumerThreads.reserve(config.consumerCount);
	for (int consumerIndex = 0; consumerIndex < config.consumerCount; ++consumerIndex)
	{
		consumerThreads.emplace_back(
			[&]()
			{
				while (true)
				{
					std::uint64_t value = 0;
					if (queue.Dequeue(value))
					{
						consumedCount.fetch_add(1, std::memory_order_relaxed);
						consumedSum.fetch_add(value, std::memory_order_relaxed);
						continue;
					}

					if (activeProducers.load(std::memory_order_acquire) == 0 &&
						consumedCount.load(std::memory_order_relaxed) >= producedCount.load(std::memory_order_relaxed))
					{
						return;
					}

					std::this_thread::yield();
				}
			});
	}

	const auto startedAt = std::chrono::steady_clock::now();
	std::uint64_t elapsedSeconds = 0;
	while (elapsedSeconds < static_cast<std::uint64_t>(config.durationSeconds))
	{
		std::this_thread::sleep_for(std::chrono::seconds(config.reportIntervalSeconds));
		elapsedSeconds = static_cast<std::uint64_t>(
			std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - startedAt).count());

		std::ostringstream line;
		line << "[progress] elapsed=" << elapsedSeconds << " produced=" << producedCount.load() << " consumed=" << consumedCount.load()
			 << " backlog~=" << (producedCount.load() - consumedCount.load());
		logger.WriteLine(line.str());
	}

	stopRequested.store(true, std::memory_order_release);

	for (auto& producerThread : producerThreads)
	{
		producerThread.join();
	}

	for (auto& consumerThread : consumerThreads)
	{
		consumerThread.join();
	}

	const std::uint64_t finalProducedCount = producedCount.load();
	const std::uint64_t finalConsumedCount = consumedCount.load();
	const std::uint64_t finalProducedSum = producedSum.load();
	const std::uint64_t finalConsumedSum = consumedSum.load();

	const bool countMatched = finalProducedCount == finalConsumedCount;
	const bool sumMatched = finalProducedSum == finalConsumedSum;
	const bool passed = countMatched && sumMatched;

	{
		std::ostringstream line;
		line << "[result] produced=" << finalProducedCount << " consumed=" << finalConsumedCount << " producedSum=" << finalProducedSum
			 << " consumedSum=" << finalConsumedSum;
		logger.WriteLine(line.str());
	}
	logger.WriteLine(passed ? "[PASS] queue soak validation succeeded." : "[FAIL] queue soak validation failed.");
	return passed ? 0 : 1;
}
