#include "FoundationPch.h"

#include "FRttThreadLocalCollector.h"

#include "FRttMetricsRuntime.h"

namespace
{
	struct STlsRttState final : Foundation::Diagnostics::FTlsCollectorRuntime::FRegisteredTlsShard
	{
		explicit STlsRttState(
			Foundation::Diagnostics::FRttMetricsRuntime& runtime)
			: FRegisteredTlsShard(runtime)
			, rttMetricsRuntime(&runtime)
		{
		}

		Foundation::Diagnostics::FRttMetricsRuntime* rttMetricsRuntime = nullptr;
		std::uint32_t collectorRefCount = 0;
		std::int64_t activeBucketStartEpochSeconds = 0;
		bool hasActiveBucket = false;
		std::vector<Foundation::Diagnostics::SRttStageAggregate> stageAggregates;
	};

	thread_local std::unordered_map<Foundation::Diagnostics::FRttMetricsRuntime*, std::unique_ptr<STlsRttState>> g_tlsRttStates{};

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

	void ResetStageAggregates(
		std::vector<Foundation::Diagnostics::SRttStageAggregate>& stageAggregates)
	{
		for (Foundation::Diagnostics::SRttStageAggregate& stageAggregate : stageAggregates)
		{
			stageAggregate = Foundation::Diagnostics::SRttStageAggregate{};
		}
	}

	STlsRttState* FindTlsState(
		Foundation::Diagnostics::FRttMetricsRuntime* const runtime) noexcept
	{
		if (runtime == nullptr)
		{
			return nullptr;
		}

		const auto stateIt = g_tlsRttStates.find(runtime);
		if (stateIt == g_tlsRttStates.end() || stateIt->second == nullptr || stateIt->second->GetOwnerRuntime() != runtime)
		{
			return nullptr;
		}

		return stateIt->second.get();
	}

	STlsRttState& GetOrCreateTlsState(
		Foundation::Diagnostics::FRttMetricsRuntime& runtime)
	{
		if (STlsRttState* const existingState = FindTlsState(&runtime); existingState != nullptr)
		{
			return *existingState;
		}

		g_tlsRttStates.erase(&runtime);

		auto tlsState = std::make_unique<STlsRttState>(runtime);
		tlsState->stageAggregates.assign(runtime.GetStageCount(), Foundation::Diagnostics::SRttStageAggregate{});

		STlsRttState& stateReference = *tlsState;
		g_tlsRttStates.emplace(&runtime, std::move(tlsState));
		return stateReference;
	}

	void FlushTlsSnapshot(
		STlsRttState& tlsState)
	{
		if (tlsState.rttMetricsRuntime == nullptr || !tlsState.hasActiveBucket)
		{
			return;
		}

		auto snapshot = std::make_unique<Foundation::Diagnostics::SRttSnapshot>();
		snapshot->bucketStartEpochSeconds = tlsState.activeBucketStartEpochSeconds;
		snapshot->stageAggregates = tlsState.stageAggregates;
		tlsState.rttMetricsRuntime->EnqueueSnapshot(std::move(snapshot));

		ResetStageAggregates(tlsState.stageAggregates);
		tlsState.activeBucketStartEpochSeconds = 0;
		tlsState.hasActiveBucket = false;
	}

	void EnsureTlsBucket(
		STlsRttState& tlsState,
		const std::chrono::system_clock::time_point nowSystem)
	{
		if (tlsState.rttMetricsRuntime == nullptr)
		{
			return;
		}

		const std::int64_t bucketStartEpochSeconds =
			ToBucketStartEpochSeconds(nowSystem, tlsState.rttMetricsRuntime->GetFlushIntervalSeconds());
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
	FRttThreadLocalCollector::FRttThreadLocalCollector(
		FRttMetricsRuntime* const rttMetricsRuntime)
		: m_rttMetricsRuntime(rttMetricsRuntime)
	{
		if (m_rttMetricsRuntime == nullptr)
		{
			return;
		}

		STlsRttState& tlsState = GetOrCreateTlsState(*m_rttMetricsRuntime);
		if (tlsState.collectorRefCount == 0)
		{
			tlsState.activeBucketStartEpochSeconds = 0;
			tlsState.hasActiveBucket = false;
			tlsState.stageAggregates.assign(m_rttMetricsRuntime->GetStageCount(), SRttStageAggregate{});
		}

		++tlsState.collectorRefCount;
	}

	FRttThreadLocalCollector::~FRttThreadLocalCollector()
	{
		STlsRttState* const tlsState = FindTlsState(m_rttMetricsRuntime);
		if (tlsState == nullptr || tlsState->collectorRefCount == 0)
		{
			return;
		}

		--tlsState->collectorRefCount;
		if (tlsState->collectorRefCount != 0)
		{
			return;
		}

		FlushTlsSnapshot(*tlsState);
		g_tlsRttStates.erase(m_rttMetricsRuntime);
	}

	SRttPendingRequest FRttThreadLocalCollector::BeginRequest(
		const FRttStageIndex stageIndex,
		const int sessionIndex) const
	{
		SRttPendingRequest pendingRequest{};
		pendingRequest.stageIndex = stageIndex;
		pendingRequest.sessionIndex = sessionIndex;
		pendingRequest.sentSteady = std::chrono::steady_clock::now();
		pendingRequest.sentSystem = std::chrono::system_clock::now();
		return pendingRequest;
	}

	void FRttThreadLocalCollector::RecordSample(
		const SRttPendingRequest& pendingRequest,
		const std::chrono::system_clock::time_point receivedSystem) const
	{
		STlsRttState* const tlsState = FindTlsState(m_rttMetricsRuntime);
		if (m_rttMetricsRuntime == nullptr || tlsState == nullptr || !m_rttMetricsRuntime->IsStageIndexValid(pendingRequest.stageIndex))
		{
			return;
		}

		EnsureTlsBucket(*tlsState, receivedSystem);
		if (!tlsState->hasActiveBucket)
		{
			return;
		}

		const auto receivedSteady = std::chrono::steady_clock::now();
		const double rttMs =
			static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(receivedSteady - pendingRequest.sentSteady).count()) /
			1000.0;

		SRttStageAggregate& stageAggregate = tlsState->stageAggregates[static_cast<std::size_t>(pendingRequest.stageIndex)];
		++stageAggregate.sampleCount;
		stageAggregate.totalRttMs += rttMs;

		SRttTopSample sample{};
		sample.rttMs = rttMs;
		sample.sessionIndex = pendingRequest.sessionIndex;
		sample.sentEpochMs = ToEpochMilliseconds(pendingRequest.sentSystem);
		sample.recvEpochMs = ToEpochMilliseconds(receivedSystem);
		InsertTopSample(stageAggregate.topSamples, sample);
	}

	void FRttThreadLocalCollector::RecordTimeout(
		const FRttStageIndex stageIndex,
		const std::chrono::system_clock::time_point timeoutSystem) const
	{
		STlsRttState* const tlsState = FindTlsState(m_rttMetricsRuntime);
		if (m_rttMetricsRuntime == nullptr || tlsState == nullptr || !m_rttMetricsRuntime->IsStageIndexValid(stageIndex))
		{
			return;
		}

		EnsureTlsBucket(*tlsState, timeoutSystem);
		if (!tlsState->hasActiveBucket)
		{
			return;
		}

		SRttStageAggregate& stageAggregate = tlsState->stageAggregates[static_cast<std::size_t>(stageIndex)];
		++stageAggregate.timeoutCount;
	}
}
