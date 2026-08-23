#include "NetLibPch.h"

#include "Diagnostics/Rio/FRioSendMetricsRuntime.h"

#include "Servers/Session/FRioSession.h"

namespace NetworkLib::Diagnostics::Rio
{
	namespace
	{
		void UpdateMaxAtomic(
			std::atomic<std::uint64_t>& target,
			const std::uint64_t candidate) noexcept
		{
			std::uint64_t observed = target.load(std::memory_order_relaxed);
			while (candidate > observed &&
				   !target.compare_exchange_weak(observed, candidate, std::memory_order_relaxed, std::memory_order_relaxed))
			{
			}
		}

		struct STlsRioSendMetricsShard final : Foundation::Diagnostics::FTlsCollectorRuntime::FRegisteredTlsShard
		{
			explicit STlsRioSendMetricsShard(
				FRioSendMetricsRuntime& ownerRuntime)
				: FRegisteredTlsShard(ownerRuntime)
			{
			}

			void ResetForEpoch(
				const std::uint32_t newEpoch) noexcept
			{
				rioSendPrepareCount.store(0, std::memory_order_relaxed);
				rioSendPrepareTotalNs.store(0, std::memory_order_relaxed);
				rioSendPrepareMaxNs.store(0, std::memory_order_relaxed);
				rioSendRingTouchCount.store(0, std::memory_order_relaxed);
				rioSendRingCrossThreadTouchCount.store(0, std::memory_order_relaxed);
				rioDirectSendRingLockCount.store(0, std::memory_order_relaxed);
				rioDirectSendRingLockWaitTotalNs.store(0, std::memory_order_relaxed);
				rioDirectSendRingLockWaitMaxNs.store(0, std::memory_order_relaxed);
				rioDirectSendRingLockHoldTotalNs.store(0, std::memory_order_relaxed);
				rioDirectSendRingLockHoldMaxNs.store(0, std::memory_order_relaxed);
				epoch.store(newEpoch, std::memory_order_release);
			}

			std::atomic<std::uint32_t> epoch = 0;
			std::atomic<std::uint64_t> rioSendPrepareCount = 0;
			std::atomic<std::uint64_t> rioSendPrepareTotalNs = 0;
			std::atomic<std::uint64_t> rioSendPrepareMaxNs = 0;
			std::atomic<std::uint64_t> rioSendRingTouchCount = 0;
			std::atomic<std::uint64_t> rioSendRingCrossThreadTouchCount = 0;
			std::atomic<std::uint64_t> rioDirectSendRingLockCount = 0;
			std::atomic<std::uint64_t> rioDirectSendRingLockWaitTotalNs = 0;
			std::atomic<std::uint64_t> rioDirectSendRingLockWaitMaxNs = 0;
			std::atomic<std::uint64_t> rioDirectSendRingLockHoldTotalNs = 0;
			std::atomic<std::uint64_t> rioDirectSendRingLockHoldMaxNs = 0;
		};

		thread_local std::unordered_map<FRioSendMetricsRuntime*, std::unique_ptr<STlsRioSendMetricsShard>> g_tlsRioSendMetricsShards{};
		thread_local const std::uint32_t g_cachedCurrentThreadId = ::GetCurrentThreadId();

		STlsRioSendMetricsShard& GetOrCreateTlsRioSendMetricsShard(
			FRioSendMetricsRuntime& ownerRuntime)
		{
			const std::uint32_t currentEpoch = ownerRuntime.GetEpoch();
			const auto shardIt = g_tlsRioSendMetricsShards.find(&ownerRuntime);
			if (shardIt != g_tlsRioSendMetricsShards.end())
			{
				if (shardIt->second != nullptr && shardIt->second->GetOwnerRuntime() == &ownerRuntime)
				{
					if (shardIt->second->epoch.load(std::memory_order_acquire) != currentEpoch)
					{
						shardIt->second->ResetForEpoch(currentEpoch);
					}

					return *shardIt->second;
				}

				g_tlsRioSendMetricsShards.erase(shardIt);
			}

			auto newShard = std::make_unique<STlsRioSendMetricsShard>(ownerRuntime);
			newShard->ResetForEpoch(currentEpoch);
			STlsRioSendMetricsShard& shardReference = *newShard;
			g_tlsRioSendMetricsShards.emplace(&ownerRuntime, std::move(newShard));
			return shardReference;
		}
	}

	void FRioSendMetricsRuntime::Reset() noexcept
	{
		std::uint32_t nextEpoch = m_epoch.fetch_add(1, std::memory_order_relaxed) + 1;
		if (nextEpoch == 0)
		{
			m_epoch.store(1, std::memory_order_relaxed);
		}
	}

	void FRioSendMetricsRuntime::RecordSendPrepareSample(
		const std::uint64_t durationNs) noexcept
	{
		STlsRioSendMetricsShard& tlsShard = GetOrCreateTlsRioSendMetricsShard(*this);
		tlsShard.rioSendPrepareCount.fetch_add(1, std::memory_order_relaxed);
		tlsShard.rioSendPrepareTotalNs.fetch_add(durationNs, std::memory_order_relaxed);
		UpdateMaxAtomic(tlsShard.rioSendPrepareMaxNs, durationNs);
	}

	void FRioSendMetricsRuntime::RecordSendRingTouch(
		NetworkLib::Session::FRioSession& sessionContext) noexcept
	{
		STlsRioSendMetricsShard& tlsShard = GetOrCreateTlsRioSendMetricsShard(*this);
		tlsShard.rioSendRingTouchCount.fetch_add(1, std::memory_order_relaxed);
		if (sessionContext.ObserveSendRingTouch(g_cachedCurrentThreadId))
		{
			tlsShard.rioSendRingCrossThreadTouchCount.fetch_add(1, std::memory_order_relaxed);
		}
	}

	void FRioSendMetricsRuntime::RecordDirectSendRingLockSample(
		const std::uint64_t waitDurationNs,
		const std::uint64_t holdDurationNs) noexcept
	{
		STlsRioSendMetricsShard& tlsShard = GetOrCreateTlsRioSendMetricsShard(*this);
		tlsShard.rioDirectSendRingLockCount.fetch_add(1, std::memory_order_relaxed);
		tlsShard.rioDirectSendRingLockWaitTotalNs.fetch_add(waitDurationNs, std::memory_order_relaxed);
		tlsShard.rioDirectSendRingLockHoldTotalNs.fetch_add(holdDurationNs, std::memory_order_relaxed);
		UpdateMaxAtomic(tlsShard.rioDirectSendRingLockWaitMaxNs, waitDurationNs);
		UpdateMaxAtomic(tlsShard.rioDirectSendRingLockHoldMaxNs, holdDurationNs);
	}

	void FRioSendMetricsRuntime::PopulateSnapshot(
		NetworkLib::Core::SServerStats& stats) const noexcept
	{
		const std::uint32_t currentEpoch = GetEpoch();
		ForEachRegisteredTlsShard(
			[&stats, currentEpoch](const Foundation::Diagnostics::FTlsCollectorRuntime::FRegisteredTlsShard& baseShard)
			{
				const auto& shard = static_cast<const STlsRioSendMetricsShard&>(baseShard);
				if (shard.epoch.load(std::memory_order_acquire) != currentEpoch)
				{
					return;
				}

				stats.rioSendPrepareCount += shard.rioSendPrepareCount.load(std::memory_order_relaxed);
				stats.rioSendPrepareTotalNs += shard.rioSendPrepareTotalNs.load(std::memory_order_relaxed);
				stats.rioSendPrepareMaxNs =
					std::max<std::uint64_t>(stats.rioSendPrepareMaxNs, shard.rioSendPrepareMaxNs.load(std::memory_order_relaxed));
				stats.rioSendRingTouchCount += shard.rioSendRingTouchCount.load(std::memory_order_relaxed);
				stats.rioSendRingCrossThreadTouchCount += shard.rioSendRingCrossThreadTouchCount.load(std::memory_order_relaxed);
				stats.rioDirectSendRingLockCount += shard.rioDirectSendRingLockCount.load(std::memory_order_relaxed);
				stats.rioDirectSendRingLockWaitTotalNs += shard.rioDirectSendRingLockWaitTotalNs.load(std::memory_order_relaxed);
				stats.rioDirectSendRingLockWaitMaxNs = std::max<std::uint64_t>(
					stats.rioDirectSendRingLockWaitMaxNs, shard.rioDirectSendRingLockWaitMaxNs.load(std::memory_order_relaxed));
				stats.rioDirectSendRingLockHoldTotalNs += shard.rioDirectSendRingLockHoldTotalNs.load(std::memory_order_relaxed);
				stats.rioDirectSendRingLockHoldMaxNs = std::max<std::uint64_t>(
					stats.rioDirectSendRingLockHoldMaxNs, shard.rioDirectSendRingLockHoldMaxNs.load(std::memory_order_relaxed));
			});
	}

	std::uint32_t FRioSendMetricsRuntime::GetEpoch() const noexcept
	{
		return m_epoch.load(std::memory_order_relaxed);
	}
}
