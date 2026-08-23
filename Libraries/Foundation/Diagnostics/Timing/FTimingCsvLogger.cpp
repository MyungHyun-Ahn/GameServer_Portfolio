#include "FoundationPch.h"

#include "FTimingCsvLogger.h"

#include "FTimingMetricsRuntime.h"

namespace
{
	constexpr double kNanosecondsPerMillisecond = 1'000'000.0;

	std::int64_t ToEpochMicroseconds(
		const std::chrono::system_clock::time_point timePoint)
	{
		return std::chrono::duration_cast<std::chrono::microseconds>(timePoint.time_since_epoch()).count();
	}

	std::int64_t ToBucketStartEpochSeconds(
		const std::chrono::system_clock::time_point timePoint,
		const int intervalSeconds)
	{
		const std::int64_t epochSeconds = std::chrono::duration_cast<std::chrono::seconds>(timePoint.time_since_epoch()).count();
		return epochSeconds - (epochSeconds % std::max(1, intervalSeconds));
	}

	std::uint64_t SaturatingAdd(
		const std::uint64_t left,
		const std::uint64_t right)
	{
		const std::uint64_t maximum = std::numeric_limits<std::uint64_t>::max();
		return maximum - left < right ? maximum : left + right;
	}

	double ToMilliseconds(
		const std::uint64_t elapsedNanoseconds)
	{
		return static_cast<double>(elapsedNanoseconds) / kNanosecondsPerMillisecond;
	}

	std::string FormatEpochMicroseconds(
		const std::int64_t epochMicroseconds)
	{
		if (epochMicroseconds <= 0)
		{
			return {};
		}

		const std::time_t epochSeconds = static_cast<std::time_t>(epochMicroseconds / 1'000'000);
		std::tm localTime{};
		localtime_s(&localTime, &epochSeconds);

		std::ostringstream oss;
		oss << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S") << '.' << std::setw(6) << std::setfill('0')
			<< (epochMicroseconds % 1'000'000);
		return oss.str();
	}

	void WriteCsvText(
		std::ofstream& csvStream,
		const std::string_view text)
	{
		const bool needsQuotes = text.find_first_of(",\"\r\n") != std::string_view::npos;
		if (!needsQuotes)
		{
			csvStream << text;
			return;
		}

		csvStream << '"';
		for (const char character : text)
		{
			if (character == '"')
			{
				csvStream << '"';
			}
			csvStream << character;
		}
		csvStream << '"';
	}

	void InsertTopSample(
		std::array<Foundation::Diagnostics::STimingTopSample, 3>& topSamples,
		const Foundation::Diagnostics::STimingTopSample& sample)
	{
		for (std::size_t sampleIndex = 0; sampleIndex < topSamples.size(); ++sampleIndex)
		{
			if (sample.elapsedNanoseconds <= topSamples[sampleIndex].elapsedNanoseconds)
			{
				continue;
			}

			for (std::size_t moveIndex = topSamples.size() - 1; moveIndex > sampleIndex; --moveIndex)
			{
				topSamples[moveIndex] = topSamples[moveIndex - 1];
			}

			topSamples[sampleIndex] = sample;
			return;
		}
	}

	void MergeMetricAggregate(
		Foundation::Diagnostics::STimingMetricAggregate& target,
		const Foundation::Diagnostics::STimingMetricAggregate& source)
	{
		if (source.sampleCount == 0)
		{
			return;
		}

		target.sampleCount = SaturatingAdd(target.sampleCount, source.sampleCount);
		target.totalElapsedNanoseconds = SaturatingAdd(target.totalElapsedNanoseconds, source.totalElapsedNanoseconds);
		target.minElapsedNanoseconds = std::min(target.minElapsedNanoseconds, source.minElapsedNanoseconds);
		target.maxElapsedNanoseconds = std::max(target.maxElapsedNanoseconds, source.maxElapsedNanoseconds);
		for (const Foundation::Diagnostics::STimingTopSample& sample : source.topSamples)
		{
			InsertTopSample(target.topSamples, sample);
		}
	}

	double CalculateAverageMilliseconds(
		const Foundation::Diagnostics::STimingMetricAggregate& aggregate)
	{
		if (aggregate.sampleCount == 0)
		{
			return 0.0;
		}

		return ToMilliseconds(aggregate.totalElapsedNanoseconds) / static_cast<double>(aggregate.sampleCount);
	}

	double CalculateMinimumMilliseconds(
		const Foundation::Diagnostics::STimingMetricAggregate& aggregate)
	{
		return aggregate.sampleCount == 0 ? 0.0 : ToMilliseconds(aggregate.minElapsedNanoseconds);
	}
}

namespace Foundation::Diagnostics
{
	FTimingCsvLogger::FTimingCsvLogger(
		FTimingMetricsRuntime& timingMetricsRuntime,
		std::string csvPath)
		: m_timingMetricsRuntime(timingMetricsRuntime)
		, m_csvPath(std::move(csvPath))
		, m_overallMetricAggregates(timingMetricsRuntime.GetMetricCount())
	{
	}

	FTimingCsvLogger::~FTimingCsvLogger()
	{
		Stop();
	}

	void FTimingCsvLogger::Start()
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

	void FTimingCsvLogger::Stop()
	{
		m_stopRequested.store(true);
		if (m_thread.joinable())
		{
			m_thread.join();
		}
	}

	void FTimingCsvLogger::Run()
	{
		auto writeHeader = [](std::ofstream& csvStream)
		{
			csvStream
				<< "bucket_start_local,metric,bucket_count,bucket_avg_ms,bucket_min_ms,bucket_max_ms,"
				<< "overall_count,overall_avg_ms,overall_min_ms,overall_max_ms,"
				<< "bucket_top1_ms,bucket_top1_thread_id,bucket_top1_context_id,bucket_top1_started_at,bucket_top1_finished_at,"
				<< "bucket_top2_ms,bucket_top2_thread_id,bucket_top2_context_id,bucket_top2_started_at,bucket_top2_finished_at,"
				<< "bucket_top3_ms,bucket_top3_thread_id,bucket_top3_context_id,bucket_top3_started_at,bucket_top3_finished_at,"
				<< "overall_top1_ms,overall_top1_thread_id,overall_top1_context_id,overall_top1_started_at,overall_top1_finished_at,"
				<< "overall_top2_ms,overall_top2_thread_id,overall_top2_context_id,overall_top2_started_at,overall_top2_finished_at,"
				<< "overall_top3_ms,overall_top3_thread_id,overall_top3_context_id,overall_top3_started_at,overall_top3_finished_at\n";
		};

		auto writeTopSampleColumns = [](std::ofstream& csvStream, const std::array<STimingTopSample, 3>& topSamples)
		{
			for (const STimingTopSample& sample : topSamples)
			{
				if (sample.elapsedNanoseconds == 0)
				{
					csvStream << ",,,,,";
					continue;
				}

				csvStream << ',' << std::fixed << std::setprecision(6) << ToMilliseconds(sample.elapsedNanoseconds) << ','
						  << sample.threadId << ',' << sample.contextId << ',' << FormatEpochMicroseconds(sample.startedEpochMicroseconds)
						  << ',' << FormatEpochMicroseconds(sample.finishedEpochMicroseconds);
			}
		};

		auto flushBucket = [this, &writeTopSampleColumns](std::ofstream& csvStream,
							   const std::int64_t bucketStartEpochSeconds,
							   const std::vector<STimingMetricAggregate>& bucketMetricAggregates)
		{
			const auto bucketStartTime = std::chrono::system_clock::time_point(std::chrono::seconds(bucketStartEpochSeconds));
			const std::string bucketStartText = FormatEpochMicroseconds(ToEpochMicroseconds(bucketStartTime));

			for (std::size_t metricIndex = 0; metricIndex < bucketMetricAggregates.size(); ++metricIndex)
			{
				const STimingMetricAggregate& bucketAggregate = bucketMetricAggregates[metricIndex];
				if (bucketAggregate.sampleCount == 0)
				{
					continue;
				}

				MergeMetricAggregate(m_overallMetricAggregates[metricIndex], bucketAggregate);
				const STimingMetricAggregate& overallAggregate = m_overallMetricAggregates[metricIndex];

				csvStream << bucketStartText << ',';
				WriteCsvText(csvStream, m_timingMetricsRuntime.GetMetricName(static_cast<FTimingMetricIndex>(metricIndex)));
				csvStream << ',' << bucketAggregate.sampleCount << ',' << std::fixed << std::setprecision(6)
						  << CalculateAverageMilliseconds(bucketAggregate) << ',' << CalculateMinimumMilliseconds(bucketAggregate) << ','
						  << ToMilliseconds(bucketAggregate.maxElapsedNanoseconds) << ',' << overallAggregate.sampleCount << ','
						  << CalculateAverageMilliseconds(overallAggregate) << ',' << CalculateMinimumMilliseconds(overallAggregate) << ','
						  << ToMilliseconds(overallAggregate.maxElapsedNanoseconds);
				writeTopSampleColumns(csvStream, bucketAggregate.topSamples);
				writeTopSampleColumns(csvStream, overallAggregate.topSamples);
				csvStream << '\n';
			}

			csvStream.flush();
		};

		auto consumePendingSnapshots = [this]()
		{
			std::unique_ptr<STimingSnapshot> snapshot;
			while (m_timingMetricsRuntime.TryDequeueSnapshot(snapshot))
			{
				if (snapshot == nullptr)
				{
					continue;
				}

				std::vector<STimingMetricAggregate>& bucketAggregates = m_pendingBuckets[snapshot->bucketStartEpochSeconds];
				if (bucketAggregates.empty())
				{
					bucketAggregates.resize(m_timingMetricsRuntime.GetMetricCount());
				}

				const std::size_t metricCount = std::min(bucketAggregates.size(), snapshot->metricAggregates.size());
				for (std::size_t metricIndex = 0; metricIndex < metricCount; ++metricIndex)
				{
					MergeMetricAggregate(bucketAggregates[metricIndex], snapshot->metricAggregates[metricIndex]);
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
				ToBucketStartEpochSeconds(std::chrono::system_clock::now(), m_timingMetricsRuntime.GetFlushIntervalSeconds());
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
