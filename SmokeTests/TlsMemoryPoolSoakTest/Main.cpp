#include "TlsMemoryPoolSoakTestPch.h"

#include "Memory/FTlsMemoryPool.h"

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
	struct SSoakConfig
	{
		int durationSeconds = 600;
		int threadCount = 8;
		int batchSize = 128;
		int reportIntervalSeconds = 10;
		std::string logPath;
	};

	struct SPayload
	{
		std::int32_t ownerThread = -1;
		std::int32_t sequence = -1;
		char padding[48]{};
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

	SSoakConfig ParseConfig(
		int argc,
		char* argv[]) noexcept
	{
		SSoakConfig config;
		for (int index = 1; index < argc; ++index)
		{
			const std::string argument = argv[index];
			if (argument == "--seconds" && index + 1 < argc)
			{
				config.durationSeconds = static_cast<int>(ParseInt64(argv[++index], config.durationSeconds));
			}
			else if (argument == "--threads" && index + 1 < argc)
			{
				config.threadCount = static_cast<int>(ParseInt64(argv[++index], config.threadCount));
			}
			else if (argument == "--batch-size" && index + 1 < argc)
			{
				config.batchSize = static_cast<int>(ParseInt64(argv[++index], config.batchSize));
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
		config.threadCount = (std::max)(1, config.threadCount);
		config.batchSize = (std::max)(1, config.batchSize);
		config.reportIntervalSeconds = (std::max)(1, config.reportIntervalSeconds);
		return config;
	}
}

int main(
	int argc,
	char* argv[])
{
	SSoakConfig config = ParseConfig(argc, argv);
	if (config.logPath.empty())
	{
		config.logPath = BuildDefaultLogPath(argc > 0 ? argv[0] : nullptr, "tls_memory_pool_soak");
	}

	FLogger logger(config.logPath);
	NetworkLib::Memory::FTlsMemoryPoolManager<SPayload, 64, 4> memoryPool;
	std::atomic<bool> stopRequested = false;
	std::atomic<bool> encounteredError = false;
	std::atomic<std::uint64_t> allocCount = 0;
	std::atomic<std::uint64_t> freeCount = 0;

	{
		std::ostringstream line;
		line << "TlsMemoryPoolSoakTest start"
			 << " seconds=" << config.durationSeconds << " threads=" << config.threadCount << " batchSize=" << config.batchSize
			 << " report=" << config.reportIntervalSeconds << "s"
			 << " logPath=" << config.logPath;
		logger.WriteLine(line.str());
	}

	std::vector<std::thread> workerThreads;
	workerThreads.reserve(config.threadCount);
	for (int threadIndex = 0; threadIndex < config.threadCount; ++threadIndex)
	{
		workerThreads.emplace_back(
			[&, threadIndex]()
			{
				std::vector<SPayload*> batch;
				batch.reserve(config.batchSize);
				int sequence = 0;

				while (!stopRequested.load(std::memory_order_acquire))
				{
					for (int batchIndex = 0; batchIndex < config.batchSize; ++batchIndex)
					{
						SPayload* payload = memoryPool.Alloc();
						if (payload == nullptr)
						{
							encounteredError.store(true, std::memory_order_relaxed);
							return;
						}

						payload->ownerThread = threadIndex;
						payload->sequence = sequence;
						batch.push_back(payload);
						allocCount.fetch_add(1, std::memory_order_relaxed);
						++sequence;
					}

					for (SPayload* payload : batch)
					{
						if (payload->ownerThread != threadIndex)
						{
							encounteredError.store(true, std::memory_order_relaxed);
							return;
						}

						memoryPool.Free(payload);
						freeCount.fetch_add(1, std::memory_order_relaxed);
					}

					batch.clear();
				}

				for (SPayload* payload : batch)
				{
					memoryPool.Free(payload);
					freeCount.fetch_add(1, std::memory_order_relaxed);
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
		line << "[progress] elapsed=" << elapsedSeconds << " alloc=" << allocCount.load() << " free=" << freeCount.load()
			 << " inUse=" << memoryPool.GetUseCount() << " capacity=" << memoryPool.GetCapacity();
		logger.WriteLine(line.str());
	}

	stopRequested.store(true, std::memory_order_release);

	for (auto& workerThread : workerThreads)
	{
		workerThread.join();
	}

	const bool countMatched = allocCount.load() == freeCount.load();
	const bool useCountIsZero = memoryPool.GetUseCount() == 0;
	const bool passed = !encounteredError.load(std::memory_order_relaxed) && countMatched && useCountIsZero;

	{
		std::ostringstream line;
		line << "[result] alloc=" << allocCount.load() << " free=" << freeCount.load() << " inUse=" << memoryPool.GetUseCount()
			 << " capacity=" << memoryPool.GetCapacity();
		logger.WriteLine(line.str());
	}
	logger.WriteLine(passed ? "[PASS] tls memory pool soak validation succeeded." : "[FAIL] tls memory pool soak validation failed.");
	return passed ? 0 : 1;
}
