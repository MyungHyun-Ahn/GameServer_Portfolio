#include "FoundationPch.h"

#include "FRttCsvLogger.h"

#include "FRttMetricsRuntime.h"

namespace
{
	std::int64_t ToEpochMilliseconds(
		const std::chrono::system_clock::time_point timePoint)
	{
		return std::chrono::duration_cast<std::chrono::milliseconds>(timePoint.time_since_epoch()).count();
	}

	std::int64_t ToBucketStartEpochSeconds(
		const std::chrono::system_clock::time_point timePoint,
		const int intervalSeconds)
	{
		const std::int64_t epochSeconds = std::chrono::duration_cast<std::chrono::seconds>(timePoint.time_since_epoch()).count();
		return epochSeconds - (epochSeconds % std::max(1, intervalSeconds));
	}

	std::string FormatEpochMilliseconds(
		const std::int64_t epochMilliseconds)
	{
		if (epochMilliseconds <= 0)
		{
			return {};
		}

		const std::time_t epochSeconds = static_cast<std::time_t>(epochMilliseconds / 1000);
		std::tm localTime{};
		localtime_s(&localTime, &epochSeconds);

		std::ostringstream oss;
		oss << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S") << '.' << std::setw(3) << std::setfill('0') << (epochMilliseconds % 1000);
		return oss.str();
	}

	void InsertTopSample(
		std::array<Foundation::Diagnostics::SRttTopSample, 3>& topSamples,
		const Foundation::Diagnostics::SRttTopSample& sample)
	{
		if (sample.rttMs <= 0.0)
		{
			return;
		}

		for (std::size_t sampleIndex = 0; sampleIndex < topSamples.size(); ++sampleIndex)
		{
			if (sample.rttMs > topSamples[sampleIndex].rttMs)
			{
				for (std::size_t moveIndex = topSamples.size() - 1; moveIndex > sampleIndex; --moveIndex)
				{
					topSamples[moveIndex] = topSamples[moveIndex - 1];
				}

				topSamples[sampleIndex] = sample;
				return;
			}
		}
	}

	void MergeStageAggregate(
		Foundation::Diagnostics::SRttStageAggregate& target,
		const Foundation::Diagnostics::SRttStageAggregate& source)
	{
		target.sampleCount += source.sampleCount;
		target.timeoutCount += source.timeoutCount;
		target.totalRttMs += source.totalRttMs;
		for (const Foundation::Diagnostics::SRttTopSample& sample : source.topSamples)
		{
			InsertTopSample(target.topSamples, sample);
		}
	}
}

namespace Foundation::Diagnostics
{
	FRttCsvLogger::FRttCsvLogger(
		FRttMetricsRuntime& rttMetricsRuntime,
		std::string csvPath)
		: m_rttMetricsRuntime(rttMetricsRuntime)
		, m_csvPath(std::move(csvPath))
		, m_overallStageAggregates(rttMetricsRuntime.GetStageCount())
	{
	}

	FRttCsvLogger::~FRttCsvLogger()
	{
		Stop();
	}

	void FRttCsvLogger::Start()
	{
		if (m_thread.joinable())
		{
			return;
		}

		m_stopRequested.store(false);
		m_thread = std::thread(
			[this]()
			{
				Run();
			});
	}

	void FRttCsvLogger::Stop()
	{
		m_stopRequested.store(true);
		if (m_thread.joinable())
		{
			m_thread.join();
		}
	}

	void FRttCsvLogger::Run()
	{
		auto writeHeader = [](std::ofstream& csvStream)
		{
			csvStream << "bucket_start_local,stage,minute_count,minute_timeout_count,minute_avg_ms,"
					  << "overall_count,overall_timeout_count,overall_avg_ms,"
					  << "minute_max1_ms,minute_max1_session,minute_max1_sent_at,minute_max1_recv_at,"
					  << "minute_max2_ms,minute_max2_session,minute_max2_sent_at,minute_max2_recv_at,"
					  << "minute_max3_ms,minute_max3_session,minute_max3_sent_at,minute_max3_recv_at,"
					  << "overall_max1_ms,overall_max1_session,overall_max1_sent_at,overall_max1_recv_at,"
					  << "overall_max2_ms,overall_max2_session,overall_max2_sent_at,overall_max2_recv_at,"
					  << "overall_max3_ms,overall_max3_session,overall_max3_sent_at,overall_max3_recv_at\n";
		};

		auto writeTopSampleColumns = [](std::ofstream& csvStream, const std::array<SRttTopSample, 3>& topSamples)
		{
			for (const SRttTopSample& sample : topSamples)
			{
				if (sample.rttMs <= 0.0)
				{
					csvStream << ",,,,";
					continue;
				}

				csvStream << ',' << std::fixed << std::setprecision(3) << sample.rttMs << ',' << sample.sessionIndex << ','
						  << FormatEpochMilliseconds(sample.sentEpochMs) << ',' << FormatEpochMilliseconds(sample.recvEpochMs);
			}
		};

		auto flushBucket = [this, &writeTopSampleColumns](std::ofstream& csvStream,
							   const std::int64_t bucketStartEpochSeconds,
							   const std::vector<SRttStageAggregate>& minuteStageAggregates)
		{
			const std::chrono::system_clock::time_point bucketStartTime =
				std::chrono::system_clock::time_point(std::chrono::seconds(bucketStartEpochSeconds));
			const std::string bucketStartText = FormatEpochMilliseconds(ToEpochMilliseconds(bucketStartTime));

			for (std::size_t stageIndex = 0; stageIndex < minuteStageAggregates.size(); ++stageIndex)
			{
				const SRttStageAggregate& minuteStage = minuteStageAggregates[stageIndex];
				MergeStageAggregate(m_overallStageAggregates[stageIndex], minuteStage);

				const double minuteAverageMs =
					minuteStage.sampleCount > 0 ? minuteStage.totalRttMs / static_cast<double>(minuteStage.sampleCount) : 0.0;
				const double overallAverageMs = m_overallStageAggregates[stageIndex].sampleCount > 0
													? m_overallStageAggregates[stageIndex].totalRttMs /
														  static_cast<double>(m_overallStageAggregates[stageIndex].sampleCount)
													: 0.0;

				csvStream << bucketStartText << ',' << m_rttMetricsRuntime.GetStageName(static_cast<FRttStageIndex>(stageIndex)) << ','
						  << minuteStage.sampleCount << ',' << minuteStage.timeoutCount << ',' << std::fixed << std::setprecision(3)
						  << minuteAverageMs << ',' << m_overallStageAggregates[stageIndex].sampleCount << ','
						  << m_overallStageAggregates[stageIndex].timeoutCount << ',' << std::fixed << std::setprecision(3)
						  << overallAverageMs;
				writeTopSampleColumns(csvStream, minuteStage.topSamples);
				writeTopSampleColumns(csvStream, m_overallStageAggregates[stageIndex].topSamples);
				csvStream << '\n';
			}

			csvStream.flush();
		};

		auto consumePendingSnapshots = [this]()
		{
			std::unique_ptr<SRttSnapshot> snapshot;
			while (m_rttMetricsRuntime.TryDequeueSnapshot(snapshot))
			{
				if (snapshot == nullptr)
				{
					continue;
				}

				std::vector<SRttStageAggregate>& bucketAggregates = m_pendingBuckets[snapshot->bucketStartEpochSeconds];
				if (bucketAggregates.empty())
				{
					bucketAggregates.resize(m_rttMetricsRuntime.GetStageCount());
				}

				for (std::size_t stageIndex = 0; stageIndex < snapshot->stageAggregates.size(); ++stageIndex)
				{
					MergeStageAggregate(bucketAggregates[stageIndex], snapshot->stageAggregates[stageIndex]);
				}
			}
		};

		auto flushCompletedBuckets = [this, &flushBucket](std::ofstream& csvStream, const bool flushAll)
		{
			if (m_pendingBuckets.empty())
			{
				return;
			}

			const std::int64_t currentBucket =
				ToBucketStartEpochSeconds(std::chrono::system_clock::now(), m_rttMetricsRuntime.GetFlushIntervalSeconds());
			std::vector<std::int64_t> completedBuckets;
			completedBuckets.reserve(m_pendingBuckets.size());
			for (const auto& [bucketStart, _] : m_pendingBuckets)
			{
				if (flushAll || bucketStart < currentBucket)
				{
					completedBuckets.push_back(bucketStart);
				}
			}

			std::sort(completedBuckets.begin(), completedBuckets.end());
			for (const std::int64_t bucketStart : completedBuckets)
			{
				auto bucketIt = m_pendingBuckets.find(bucketStart);
				if (bucketIt == m_pendingBuckets.end())
				{
					continue;
				}

				flushBucket(csvStream, bucketStart, bucketIt->second);
				m_pendingBuckets.erase(bucketIt);
			}
		};

		const std::filesystem::path csvPath(m_csvPath);
		if (csvPath.has_parent_path())
		{
			std::filesystem::create_directories(csvPath.parent_path());
		}

		std::ofstream csvStream(csvPath, std::ios::out | std::ios::trunc);
		if (!csvStream.is_open())
		{
			return;
		}

		writeHeader(csvStream);

		while (true)
		{
			consumePendingSnapshots();
			flushCompletedBuckets(csvStream, false);

			if (m_stopRequested.load())
			{
				consumePendingSnapshots();
				flushCompletedBuckets(csvStream, true);
				break;
			}

			std::this_thread::sleep_for(std::chrono::milliseconds(200));
		}
	}
}
