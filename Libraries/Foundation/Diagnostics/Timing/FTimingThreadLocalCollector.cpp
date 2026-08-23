#include "FoundationPch.h"

#include "FTimingThreadLocalCollector.h"

#include "FTimingMetricsRuntime.h"

namespace
{
	struct STlsTimingState;
	void FlushTlsSnapshot(STlsTimingState& tlsState);

	struct STlsTimingState final : Foundation::Diagnostics::FTlsCollectorRuntime::FRegisteredTlsShard
	{
		explicit STlsTimingState(
			Foundation::Diagnostics::FTimingMetricsRuntime& runtime)
			: FRegisteredTlsShard(runtime)
			, timingMetricsRuntime(&runtime)
		{
		}

		~STlsTimingState() override;

		Foundation::Diagnostics::FTimingMetricsRuntime* timingMetricsRuntime = nullptr;
		std::uint32_t collectorRefCount = 0;
		std::int64_t activeBucketStartEpochSeconds = 0;
		bool hasActiveBucket = false;
		std::vector<Foundation::Diagnostics::STimingMetricAggregate> metricAggregates;
	};

	thread_local std::unordered_map<Foundation::Diagnostics::FTimingMetricsRuntime*, std::unique_ptr<STlsTimingState>> g_tlsTimingStates{};

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

	std::uint64_t ToUnsignedNanoseconds(
		const std::chrono::nanoseconds elapsed)
	{
		return elapsed.count() > 0 ? static_cast<std::uint64_t>(elapsed.count()) : 0;
	}

	std::uint64_t SaturatingAdd(
		const std::uint64_t left,
		const std::uint64_t right)
	{
		const std::uint64_t maximum = std::numeric_limits<std::uint64_t>::max();
		return maximum - left < right ? maximum : left + right;
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

	void ResetMetricAggregates(
		std::vector<Foundation::Diagnostics::STimingMetricAggregate>& metricAggregates)
	{
		for (Foundation::Diagnostics::STimingMetricAggregate& metricAggregate : metricAggregates)
		{
			metricAggregate = Foundation::Diagnostics::STimingMetricAggregate{};
		}
	}

	STlsTimingState* FindTlsState(
		Foundation::Diagnostics::FTimingMetricsRuntime* const runtime) noexcept
	{
		if (runtime == nullptr)
		{
			return nullptr;
		}

		const auto stateIt = g_tlsTimingStates.find(runtime);
		if (stateIt == g_tlsTimingStates.end() || stateIt->second == nullptr || stateIt->second->GetOwnerRuntime() != runtime)
		{
			return nullptr;
		}

		return stateIt->second.get();
	}

	STlsTimingState& GetOrCreateTlsState(
		Foundation::Diagnostics::FTimingMetricsRuntime& runtime)
	{
		if (STlsTimingState* const existingState = FindTlsState(&runtime); existingState != nullptr)
		{
			return *existingState;
		}

		g_tlsTimingStates.erase(&runtime);

		auto tlsState = std::make_unique<STlsTimingState>(runtime);
		tlsState->metricAggregates.assign(runtime.GetMetricCount(), Foundation::Diagnostics::STimingMetricAggregate{});

		STlsTimingState& stateReference = *tlsState;
		g_tlsTimingStates.emplace(&runtime, std::move(tlsState));
		return stateReference;
	}

	void FlushTlsSnapshot(
		STlsTimingState& tlsState)
	{
		if (tlsState.timingMetricsRuntime == nullptr || !tlsState.hasActiveBucket)
		{
			return;
		}

		auto snapshot = std::make_unique<Foundation::Diagnostics::STimingSnapshot>();
		snapshot->bucketStartEpochSeconds = tlsState.activeBucketStartEpochSeconds;
		snapshot->metricAggregates = tlsState.metricAggregates;
		tlsState.timingMetricsRuntime->EnqueueSnapshot(std::move(snapshot));

		ResetMetricAggregates(tlsState.metricAggregates);
		tlsState.activeBucketStartEpochSeconds = 0;
		tlsState.hasActiveBucket = false;
	}

	STlsTimingState::~STlsTimingState()
	{
		try
		{
			FlushTlsSnapshot(*this);
		}
		catch (...)
		{
		}
	}

	void EnsureTlsBucket(
		STlsTimingState& tlsState,
		const std::chrono::system_clock::time_point nowSystem)
	{
		if (tlsState.timingMetricsRuntime == nullptr)
		{
			return;
		}

		const std::int64_t bucketStartEpochSeconds =
			ToBucketStartEpochSeconds(nowSystem, tlsState.timingMetricsRuntime->GetFlushIntervalSeconds());
		if (!tlsState.hasActiveBucket)
		{
			tlsState.hasActiveBucket = true;
			tlsState.activeBucketStartEpochSeconds = bucketStartEpochSeconds;
			return;
		}

		if (tlsState.activeBucketStartEpochSeconds != bucketStartEpochSeconds)
		{
			FlushTlsSnapshot(tlsState);
			tlsState.hasActiveBucket = true;
			tlsState.activeBucketStartEpochSeconds = bucketStartEpochSeconds;
		}
	}
}

namespace Foundation::Diagnostics
{
	FTimingThreadLocalCollector::FTimingThreadLocalCollector(
		FTimingMetricsRuntime* const timingMetricsRuntime)
		: m_timingMetricsRuntime(timingMetricsRuntime)
	{
		if (m_timingMetricsRuntime == nullptr)
		{
			return;
		}

		STlsTimingState& tlsState = GetOrCreateTlsState(*m_timingMetricsRuntime);
		if (tlsState.collectorRefCount == 0)
		{
			tlsState.activeBucketStartEpochSeconds = 0;
			tlsState.hasActiveBucket = false;
			tlsState.metricAggregates.assign(m_timingMetricsRuntime->GetMetricCount(), STimingMetricAggregate{});
		}

		++tlsState.collectorRefCount;
	}

	FTimingThreadLocalCollector::~FTimingThreadLocalCollector()
	{
		STlsTimingState* const tlsState = FindTlsState(m_timingMetricsRuntime);
		if (tlsState == nullptr || tlsState->collectorRefCount == 0)
		{
			return;
		}

		--tlsState->collectorRefCount;
		if (tlsState->collectorRefCount != 0)
		{
			return;
		}

		try
		{
			FlushTlsSnapshot(*tlsState);
		}
		catch (...)
		{
		}

		g_tlsTimingStates.erase(m_timingMetricsRuntime);
	}

	STimingPendingSample FTimingThreadLocalCollector::BeginSample(
		const FTimingMetricIndex metricIndex,
		const std::uint64_t contextId) const noexcept
	{
		STimingPendingSample pendingSample{};
		pendingSample.metricIndex = metricIndex;
		pendingSample.contextId = contextId;
		pendingSample.startedSteady = std::chrono::steady_clock::now();
		pendingSample.startedSystem = std::chrono::system_clock::now();
		return pendingSample;
	}

	void FTimingThreadLocalCollector::RecordSample(
		const STimingPendingSample& pendingSample,
		const std::chrono::system_clock::time_point finishedSystem) const noexcept
	{
		const auto finishedSteady = std::chrono::steady_clock::now();
		const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(finishedSteady - pendingSample.startedSteady);
		RecordDurationInternal(pendingSample.metricIndex, elapsed, pendingSample.contextId, pendingSample.startedSystem, finishedSystem);
	}

	void FTimingThreadLocalCollector::RecordDuration(
		const FTimingMetricIndex metricIndex,
		const std::chrono::nanoseconds elapsed,
		const std::uint64_t contextId) const noexcept
	{
		const std::chrono::nanoseconds normalizedElapsed = std::max(elapsed, std::chrono::nanoseconds::zero());
		const auto finishedSystem = std::chrono::system_clock::now();
		const auto startedSystem = finishedSystem - std::chrono::duration_cast<std::chrono::system_clock::duration>(normalizedElapsed);
		RecordDurationInternal(metricIndex, normalizedElapsed, contextId, startedSystem, finishedSystem);
	}

	void FTimingThreadLocalCollector::RecordDurationInternal(
		const FTimingMetricIndex metricIndex,
		const std::chrono::nanoseconds elapsed,
		const std::uint64_t contextId,
		const std::chrono::system_clock::time_point startedSystem,
		const std::chrono::system_clock::time_point finishedSystem) const noexcept
	{
		try
		{
			STlsTimingState* const tlsState = FindTlsState(m_timingMetricsRuntime);
			if (m_timingMetricsRuntime == nullptr || tlsState == nullptr || !m_timingMetricsRuntime->IsMetricIndexValid(metricIndex))
			{
				return;
			}

			EnsureTlsBucket(*tlsState, finishedSystem);
			if (!tlsState->hasActiveBucket)
			{
				return;
			}

			const std::uint64_t elapsedNanoseconds = ToUnsignedNanoseconds(elapsed);
			STimingMetricAggregate& aggregate = tlsState->metricAggregates[static_cast<std::size_t>(metricIndex)];
			++aggregate.sampleCount;
			aggregate.totalElapsedNanoseconds = SaturatingAdd(aggregate.totalElapsedNanoseconds, elapsedNanoseconds);
			aggregate.minElapsedNanoseconds = std::min(aggregate.minElapsedNanoseconds, elapsedNanoseconds);
			aggregate.maxElapsedNanoseconds = std::max(aggregate.maxElapsedNanoseconds, elapsedNanoseconds);

			STimingTopSample sample{};
			sample.elapsedNanoseconds = elapsedNanoseconds;
			sample.contextId = contextId;
			sample.threadId = GetCurrentThreadId();
			sample.startedEpochMicroseconds = ToEpochMicroseconds(startedSystem);
			sample.finishedEpochMicroseconds = ToEpochMicroseconds(finishedSystem);
			InsertTopSample(aggregate.topSamples, sample);
		}
		catch (...)
		{
		}
	}
}
