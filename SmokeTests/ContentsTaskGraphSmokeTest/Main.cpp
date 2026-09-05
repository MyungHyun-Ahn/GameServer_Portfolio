#include "ContentsTaskGraphSmokeTestPch.h"

#if defined(NETWORKLIB_LOCKFREE_QUEUE_TEST_HOOKS)
namespace NetworkLib::Containers
{
	struct FLockFreeQueueTestAccess final
	{
		template <FundamentalOrPointer T, bool CAS2First, typename TDequeueHook>
		static bool DequeueWithHook(
			FLockFreeQueue<T, CAS2First>& queue,
			T& outValue,
			TDequeueHook& hook) noexcept
		{
			return queue.DequeueImpl(outValue, hook);
		}

		template <FundamentalOrPointer T, bool CAS2First>
		static std::uint64_t LoadHead(
			FLockFreeQueue<T, CAS2First>& queue) noexcept
		{
			return AtomicLoad64(&queue.m_head);
		}
	};
}
#endif

namespace
{
	using ContentsRuntime::Core::FContentId;
	using ContentsRuntime::Core::FContentInstanceId;
	using Foundation::Diagnostics::FTimingCsvLogger;
	using Foundation::Diagnostics::FTimingMetricIndex;
	using Foundation::Diagnostics::FTimingMetricsRuntime;
	using Foundation::Diagnostics::FTimingThreadLocalCollector;
	using Foundation::Diagnostics::STimingMetricsConfig;

	constexpr FContentId kMainContentId = 5000;
	constexpr FContentId kExecutorContentId = 5001;
	constexpr std::uint32_t kWaveCount = 4;
	constexpr std::uint32_t kUnassignedWorkerIndex = std::numeric_limits<std::uint32_t>::max();
	constexpr std::uint32_t kMaximumResolvedWorkIterations = 4'000'000;
	constexpr FTimingMetricIndex kTaskQueueWaitMetric = 0;
	constexpr FTimingMetricIndex kTaskExecuteMetric = 1;
	constexpr FTimingMetricIndex kWaveTotalMetric = 2;
	constexpr FTimingMetricIndex kWaveNotifyWaitMetric = 3;

	enum class EWorkloadMode : std::uint8_t
	{
		Uniform = 0,
		Random = 1,
		Hot = 2,
		HotWave = 3
	};

	enum class ETestSuite : std::uint8_t
	{
		Single = 0,
		Quick = 1,
		Soak = 2,
		QueueAba = 3
	};

	enum class EFailureCode : std::uint8_t
	{
		None = 0,
		InvalidWavePartition = 1,
		RuntimeEnqueueFailure = 2,
		ExecutionInvariantViolation = 3,
		Timeout = 4,
		ReferenceStateMismatch = 5,
		TimingOutputFailure = 6
	};

	struct STestConfig
	{
		std::uint32_t gridSize = 16;
		std::uint32_t tickCount = 120;
		std::uint32_t workerCount = 4;
		std::uint32_t workIterations = 2048;
		std::uint32_t pumpBatchSize = 4;
		std::uint32_t timeoutSeconds = 30;
		std::uint32_t durationSeconds = 0;
		std::uint32_t progressIntervalSeconds = 60;
		std::uint32_t hotSectorPercent = 10;
		std::uint32_t hotWorkMultiplier = 16;
		std::uint32_t referenceCheckpointIntervalTicks = 0;
		std::uint32_t referenceCheckpointWindowTicks = 0;
		std::uint64_t seed = 0xC0FFEEull;
		EWorkloadMode workloadMode = EWorkloadMode::Uniform;
		ETestSuite suite = ETestSuite::Single;
		bool workloadModeSpecified = false;
		bool injectInvalidWavePartition = false;
		bool enableParallelStartProbe = true;
		bool requireParallelObservation = true;
		bool timingEnabled = false;
		std::filesystem::path timingOutputDirectory;
	};

	struct STimingSummary
	{
		std::uint64_t sampleCount = 0;
		std::uint64_t totalNanoseconds = 0;
		std::uint64_t maximumNanoseconds = 0;

		double GetAverageMicroseconds() const noexcept
		{
			return sampleCount == 0 ? 0.0 : static_cast<double>(totalNanoseconds) / static_cast<double>(sampleCount) / 1'000.0;
		}

		double GetMaximumMicroseconds() const noexcept
		{
			return static_cast<double>(maximumNanoseconds) / 1'000.0;
		}
	};

	struct SAtomicTimingSummary
	{
		std::atomic<std::uint64_t> sampleCount = 0;
		std::atomic<std::uint64_t> totalNanoseconds = 0;
		std::atomic<std::uint64_t> maximumNanoseconds = 0;

		void Record(
			const std::uint64_t elapsedNanoseconds) noexcept
		{
			sampleCount.fetch_add(1, std::memory_order_relaxed);
			totalNanoseconds.fetch_add(elapsedNanoseconds, std::memory_order_relaxed);

			std::uint64_t observedMaximum = maximumNanoseconds.load(std::memory_order_relaxed);
			while (observedMaximum < elapsedNanoseconds &&
				   !maximumNanoseconds.compare_exchange_weak(
					   observedMaximum, elapsedNanoseconds, std::memory_order_relaxed, std::memory_order_relaxed))
			{
			}
		}

		STimingSummary Snapshot() const noexcept
		{
			STimingSummary result{};
			result.sampleCount = sampleCount.load(std::memory_order_relaxed);
			result.totalNanoseconds = totalNanoseconds.load(std::memory_order_relaxed);
			result.maximumNanoseconds = maximumNanoseconds.load(std::memory_order_relaxed);
			return result;
		}
	};

	struct SRunResult
	{
		bool passed = false;
		EFailureCode failureCode = EFailureCode::None;
		std::string failureReason;
		std::string label;
		std::uint32_t runtimeWorkerCount = 0;
		std::uint32_t workersUsed = 0;
		std::uint32_t maxParallelTasks = 0;
		std::uint32_t completedTickCount = 0;
		std::uint64_t completedTaskCount = 0;
		std::uint64_t expectedTaskCount = 0;
		std::uint64_t checksum = 0;
		std::uint64_t queueHighWatermark = 0;
		std::uint64_t emptyPumpCount = 0;
		std::uint64_t rescheduledPumpCount = 0;
		std::uint64_t referenceCheckpointCount = 0;
		std::uint64_t naturalParallelOverlapCount = 0;
		double elapsedSeconds = 0.0;
		STimingSummary taskQueueWait;
		STimingSummary taskExecute;
		STimingSummary waveTotal;
		STimingSummary waveNotifyWait;
		std::array<STimingSummary, kWaveCount> queueWaitByWave{};
		std::array<STimingSummary, kWaveCount> waveDurationByWave{};
		std::vector<std::uint64_t> workerTaskCounts;
		std::vector<std::uint64_t> workerWorkIterations;
		std::vector<std::uint64_t> sectorStates;
		std::filesystem::path timingCsvPath;
	};

	std::uint64_t GetSteadyNanoseconds() noexcept
	{
		return static_cast<std::uint64_t>(
			std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
	}

	std::uint64_t Mix64(
		std::uint64_t value) noexcept
	{
		value += 0x9E3779B97F4A7C15ull;
		value = (value ^ (value >> 30u)) * 0xBF58476D1CE4E5B9ull;
		value = (value ^ (value >> 27u)) * 0x94D049BB133111EBull;
		return value ^ (value >> 31u);
	}

	std::uint32_t GetWaveIndex(
		const std::uint32_t x,
		const std::uint32_t y) noexcept
	{
		return (x & 1u) | ((y & 1u) << 1u);
	}

	std::string_view GetWorkloadModeName(
		const EWorkloadMode mode) noexcept
	{
		switch (mode)
		{
			case EWorkloadMode::Uniform:
				return "uniform";
			case EWorkloadMode::Random:
				return "random";
			case EWorkloadMode::Hot:
				return "hot";
			case EWorkloadMode::HotWave:
				return "hot-wave";
			default:
				return "unknown";
		}
	}

	std::string_view GetFailureCodeName(
		const EFailureCode failureCode) noexcept
	{
		switch (failureCode)
		{
			case EFailureCode::None:
				return "NONE";
			case EFailureCode::InvalidWavePartition:
				return "INVALID_WAVE_PARTITION";
			case EFailureCode::RuntimeEnqueueFailure:
				return "RUNTIME_ENQUEUE_FAILURE";
			case EFailureCode::ExecutionInvariantViolation:
				return "EXECUTION_INVARIANT_VIOLATION";
			case EFailureCode::Timeout:
				return "TIMEOUT";
			case EFailureCode::ReferenceStateMismatch:
				return "REFERENCE_STATE_MISMATCH";
			case EFailureCode::TimingOutputFailure:
				return "TIMING_OUTPUT_FAILURE";
			default:
				return "UNKNOWN";
		}
	}

	std::uint32_t ResolveWorkIterations(
		const STestConfig& config,
		const std::uint32_t tickIndex,
		const std::uint32_t sectorIndex,
		const std::uint32_t waveIndex) noexcept
	{
		const std::uint64_t hash = Mix64(config.seed ^ (static_cast<std::uint64_t>(tickIndex) << 32u) ^
										 (static_cast<std::uint64_t>(sectorIndex) * 0xD6E8FEB86659FD93ull));

		switch (config.workloadMode)
		{
			case EWorkloadMode::Uniform:
				return config.workIterations;

			case EWorkloadMode::Random:
			{
				const std::uint32_t minimumIterations = std::max<std::uint32_t>(1u, config.workIterations / 4u);
				const std::uint64_t maximumIterations =
					std::min<std::uint64_t>(kMaximumResolvedWorkIterations, static_cast<std::uint64_t>(config.workIterations) * 2u);
				const std::uint64_t range = maximumIterations - minimumIterations + 1u;
				return minimumIterations + static_cast<std::uint32_t>(hash % range);
			}

			case EWorkloadMode::Hot:
			{
				const std::uint64_t sectorHash = Mix64(config.seed ^ (static_cast<std::uint64_t>(sectorIndex) * 0xD6E8FEB86659FD93ull));
				const bool isHotSector = sectorHash % 100u < config.hotSectorPercent;
				if (!isHotSector)
				{
					return std::max<std::uint32_t>(1u, config.workIterations / 4u);
				}

				return static_cast<std::uint32_t>(std::min<std::uint64_t>(
					kMaximumResolvedWorkIterations, static_cast<std::uint64_t>(config.workIterations) * config.hotWorkMultiplier));
			}

			case EWorkloadMode::HotWave:
			{
				const std::uint32_t hotWaveIndex = static_cast<std::uint32_t>(config.seed % kWaveCount);
				if (waveIndex != hotWaveIndex)
				{
					return std::max<std::uint32_t>(1u, config.workIterations / 4u);
				}

				return static_cast<std::uint32_t>(std::min<std::uint64_t>(
					kMaximumResolvedWorkIterations, static_cast<std::uint64_t>(config.workIterations) * config.hotWorkMultiplier));
			}

			default:
				return config.workIterations;
		}
	}

	void AdvanceReferenceSectorStates(
		const STestConfig& config,
		const std::uint32_t tickIndex,
		std::vector<std::uint64_t>& sectorStates)
	{
		for (std::uint32_t waveIndex = 0; waveIndex < kWaveCount; ++waveIndex)
		{
			const std::vector<std::uint64_t> sourceStates = sectorStates;
			for (std::uint32_t y = 0; y < config.gridSize; ++y)
			{
				for (std::uint32_t x = 0; x < config.gridSize; ++x)
				{
					if (GetWaveIndex(x, y) != waveIndex)
					{
						continue;
					}

					const std::uint32_t sectorIndex = y * config.gridSize + x;
					std::uint64_t neighborMix = 0xCBF29CE484222325ull;
					for (std::int32_t yOffset = -1; yOffset <= 1; ++yOffset)
					{
						for (std::int32_t xOffset = -1; xOffset <= 1; ++xOffset)
						{
							if (xOffset == 0 && yOffset == 0)
							{
								continue;
							}

							const std::int32_t neighborX = static_cast<std::int32_t>(x) + xOffset;
							const std::int32_t neighborY = static_cast<std::int32_t>(y) + yOffset;
							if (neighborX < 0 || neighborY < 0 || neighborX >= static_cast<std::int32_t>(config.gridSize) ||
								neighborY >= static_cast<std::int32_t>(config.gridSize))
							{
								continue;
							}

							const std::size_t neighborIndex =
								static_cast<std::size_t>(neighborY) * config.gridSize + static_cast<std::size_t>(neighborX);
							const std::uint64_t neighborValue = sourceStates[neighborIndex];
							neighborMix ^= neighborValue + 0x9E3779B97F4A7C15ull + (neighborMix << 6u) + (neighborMix >> 2u);
						}
					}

					std::uint64_t value = sourceStates[sectorIndex];
					value ^= neighborMix + static_cast<std::uint64_t>(tickIndex + 1u) * 0xD6E8FEB86659FD93ull;
					const std::uint32_t workIterations = ResolveWorkIterations(config, tickIndex, sectorIndex, waveIndex);
					for (std::uint32_t iteration = 0; iteration < workIterations; ++iteration)
					{
						value ^= value >> 30u;
						value *= 0xBF58476D1CE4E5B9ull;
						value ^= value >> 27u;
						value *= 0x94D049BB133111EBull;
						value ^= value >> 31u;
						value += neighborMix ^ static_cast<std::uint64_t>(iteration + 1u);
					}

					sectorStates[sectorIndex] = value;
				}
			}
		}
	}

	std::uint64_t BuildTimingContext(
		const std::uint32_t tickIndex,
		const std::uint32_t itemIndex) noexcept
	{
		return (static_cast<std::uint64_t>(tickIndex) << 32u) | itemIndex;
	}

	void UpdateMaximum(
		std::atomic<std::uint32_t>& target,
		const std::uint32_t value) noexcept
	{
		std::uint32_t observed = target.load(std::memory_order_relaxed);
		while (observed < value && !target.compare_exchange_weak(observed, value, std::memory_order_relaxed, std::memory_order_relaxed))
		{
		}
	}

	void UpdateMaximum(
		std::atomic<std::uint64_t>& target,
		const std::uint64_t value) noexcept
	{
		std::uint64_t observed = target.load(std::memory_order_relaxed);
		while (observed < value && !target.compare_exchange_weak(observed, value, std::memory_order_relaxed, std::memory_order_relaxed))
		{
		}
	}

	class FTaskHostContent final : public ContentsRuntime::Core::IContent
	{
	public:
		FTaskHostContent(
			const FContentId contentId,
			const FContentInstanceId contentInstanceId)
			: m_contentId(contentId)
			, m_contentInstanceId(contentInstanceId)
		{
		}

		FContentId GetContentId() const noexcept override
		{
			return m_contentId;
		}

		FContentInstanceId GetContentInstanceId() const noexcept override
		{
			return m_contentInstanceId;
		}

		std::uint32_t GetTargetFps() const noexcept override
		{
			return 1;
		}

		void OnEnter(
			std::uint64_t,
			std::uint64_t,
			ContentsRuntime::Bridge::IContentBridge&) override
		{
		}

		void OnLeave(
			std::uint64_t,
			std::uint64_t,
			ContentsRuntime::Bridge::IContentBridge&) override
		{
		}

		void OnPacket(
			std::uint64_t,
			std::uint64_t,
			std::uint16_t,
			std::span<const char>,
			ContentsRuntime::Bridge::IContentBridge&) override
		{
		}

	private:
		FContentId m_contentId = ContentsRuntime::Core::kInvalidContentId;
		FContentInstanceId m_contentInstanceId = ContentsRuntime::Core::kInvalidContentInstanceId;
	};

	class FTaskGraphScenario final
	{
	private:
		struct STask
		{
			std::uint32_t sectorIndex = 0;
			std::uint32_t x = 0;
			std::uint32_t y = 0;
			std::uint32_t waveIndex = 0;
			std::uint32_t tickIndex = 0;
			std::uint64_t enqueuedAtNanoseconds = 0;
		};

	public:
		FTaskGraphScenario(
			ContentsRuntime::Routing::FContentRuntime& runtime,
			const FContentInstanceId mainInstanceId,
			std::vector<FContentInstanceId> executorInstanceIds,
			const STestConfig& config,
			const std::uint32_t runtimeWorkerCount,
			std::string label,
			FTimingMetricsRuntime* const timingMetricsRuntime)
			: m_runtime(runtime)
			, m_mainInstanceId(mainInstanceId)
			, m_executorInstanceIds(std::move(executorInstanceIds))
			, m_config(config)
			, m_runtimeWorkerCount(runtimeWorkerCount)
			, m_label(std::move(label))
			, m_timingMetricsRuntime(timingMetricsRuntime)
			, m_sectorCount(static_cast<std::size_t>(config.gridSize) * config.gridSize)
			, m_tasks(std::make_unique<STask[]>(m_sectorCount))
			, m_sectorStates(std::make_unique<std::atomic<std::uint64_t>[]>(m_sectorCount))
			, m_sectorExecutionCounts(std::make_unique<std::atomic<std::uint32_t>[]>(m_sectorCount))
			, m_sectorActiveFlags(std::make_unique<std::atomic<bool>[]>(m_sectorCount))
			, m_workerUsedFlags(std::make_unique<std::atomic<bool>[]>(runtimeWorkerCount))
			, m_workerTaskCounts(std::make_unique<std::atomic<std::uint64_t>[]>(runtimeWorkerCount))
			, m_workerWorkIterations(std::make_unique<std::atomic<std::uint64_t>[]>(runtimeWorkerCount))
		{
			for (std::size_t index = 0; index < m_sectorCount; ++index)
			{
				const std::uint32_t sectorIndex = static_cast<std::uint32_t>(index);
				const std::uint32_t x = sectorIndex % m_config.gridSize;
				const std::uint32_t y = sectorIndex / m_config.gridSize;
				const std::uint32_t waveIndex = GetWaveIndex(x, y);

				m_tasks[index].sectorIndex = sectorIndex;
				m_tasks[index].x = x;
				m_tasks[index].y = y;
				m_tasks[index].waveIndex = waveIndex;
				m_waves[waveIndex].push_back(&m_tasks[index]);

				m_sectorStates[index].store(
					(static_cast<std::uint64_t>(sectorIndex) + 1u) * 0x9E3779B185EBCA87ull, std::memory_order_relaxed);
				m_sectorExecutionCounts[index].store(0, std::memory_order_relaxed);
				m_sectorActiveFlags[index].store(false, std::memory_order_relaxed);
			}

			for (std::uint32_t workerIndex = 0; workerIndex < runtimeWorkerCount; ++workerIndex)
			{
				m_workerUsedFlags[workerIndex].store(false, std::memory_order_relaxed);
				m_workerTaskCounts[workerIndex].store(0, std::memory_order_relaxed);
				m_workerWorkIterations[workerIndex].store(0, std::memory_order_relaxed);
			}

			if (m_config.injectInvalidWavePartition && m_sectorCount >= 2)
			{
				m_tasks[1].waveIndex = m_tasks[0].waveIndex;
			}

			if (m_config.enableParallelStartProbe)
			{
				m_parallelProbeTarget = std::min<std::uint32_t>(
					2u, std::min<std::uint32_t>(m_runtimeWorkerCount, static_cast<std::uint32_t>(m_waves[0].size())));
			}
			m_wavePartitionValid = ValidateWavePartition();
		}

		bool Start()
		{
			m_startTime = std::chrono::steady_clock::now();
			m_startNanoseconds = GetSteadyNanoseconds();
			if (!m_wavePartitionValid)
			{
				Fail(EFailureCode::InvalidWavePartition, "같은 Wave에 인접 Sector가 포함된 잘못된 Task Graph입니다.");
				return false;
			}

			if (!m_runtime.EnqueueCompletionToInstance(m_mainInstanceId,
					[this]()
					{
						BeginTickOnMain(0);
					}))
			{
				Fail(EFailureCode::RuntimeEnqueueFailure, "Main Instance 시작 작업을 ContentsRuntime에 등록하지 못했습니다.");
				return false;
			}

			return true;
		}

		bool Wait()
		{
			const auto timeoutAt = m_startTime + std::chrono::seconds(m_config.timeoutSeconds);
			auto nextProgressAt = m_startTime + std::chrono::seconds(m_config.progressIntervalSeconds);
			std::unique_lock<std::mutex> lock(m_completionMutex);

			while (!m_finished.load(std::memory_order_acquire))
			{
				const auto now = std::chrono::steady_clock::now();
				if (now >= timeoutAt)
				{
					lock.unlock();
					Fail(EFailureCode::Timeout, "제한 시간 안에 모든 Task Wave가 끝나지 않았습니다.");
					return false;
				}

				const auto wakeAt = std::min(timeoutAt, nextProgressAt);
				m_completionCondition.wait_until(lock,
					wakeAt,
					[this]()
					{
						return m_finished.load(std::memory_order_acquire);
					});

				if (!m_finished.load(std::memory_order_acquire) && std::chrono::steady_clock::now() >= nextProgressAt)
				{
					std::cout << "[progress:" << m_label << "] ticks=" << m_completedTickCount.load(std::memory_order_relaxed)
							  << " tasks=" << m_completedTaskCount.load(std::memory_order_relaxed)
							  << " queued=" << m_queuedTaskCount.load(std::memory_order_relaxed)
							  << " inFlight=" << m_inFlightTaskCount.load(std::memory_order_relaxed) << '\n'
							  << std::flush;
					nextProgressAt += std::chrono::seconds(m_config.progressIntervalSeconds);
				}
			}

			return !m_failed.load(std::memory_order_acquire);
		}

		SRunResult BuildResult() const
		{
			SRunResult result{};
			result.passed = m_finished.load(std::memory_order_acquire) && !m_failed.load(std::memory_order_acquire);
			result.failureCode = m_failureCode.load(std::memory_order_relaxed);
			result.label = m_label;
			result.runtimeWorkerCount = m_runtimeWorkerCount;
			result.maxParallelTasks = m_maxParallelTasks.load(std::memory_order_relaxed);
			result.completedTickCount = m_completedTickCount.load(std::memory_order_relaxed);
			result.completedTaskCount = m_completedTaskCount.load(std::memory_order_relaxed);
			result.expectedTaskCount = static_cast<std::uint64_t>(m_sectorCount) * result.completedTickCount;
			result.checksum = m_checksum.load(std::memory_order_relaxed);
			result.queueHighWatermark = m_queueHighWatermark.load(std::memory_order_relaxed);
			result.emptyPumpCount = m_emptyPumpCount.load(std::memory_order_relaxed);
			result.rescheduledPumpCount = m_rescheduledPumpCount.load(std::memory_order_relaxed);
			result.referenceCheckpointCount = m_referenceCheckpointCount.load(std::memory_order_relaxed);
			result.naturalParallelOverlapCount = m_naturalParallelOverlapCount.load(std::memory_order_relaxed);
			result.elapsedSeconds = m_elapsedSeconds.load(std::memory_order_relaxed);
			result.taskQueueWait = m_taskQueueWait.Snapshot();
			result.taskExecute = m_taskExecute.Snapshot();
			result.waveTotal = m_waveTotal.Snapshot();
			result.waveNotifyWait = m_waveNotifyWait.Snapshot();

			result.workerTaskCounts.resize(m_runtimeWorkerCount);
			result.workerWorkIterations.resize(m_runtimeWorkerCount);
			for (std::uint32_t workerIndex = 0; workerIndex < m_runtimeWorkerCount; ++workerIndex)
			{
				if (m_workerUsedFlags[workerIndex].load(std::memory_order_relaxed))
				{
					++result.workersUsed;
				}
				result.workerTaskCounts[workerIndex] = m_workerTaskCounts[workerIndex].load(std::memory_order_relaxed);
				result.workerWorkIterations[workerIndex] = m_workerWorkIterations[workerIndex].load(std::memory_order_relaxed);
			}

			for (std::uint32_t waveIndex = 0; waveIndex < kWaveCount; ++waveIndex)
			{
				result.queueWaitByWave[waveIndex] = m_taskQueueWaitByWave[waveIndex].Snapshot();
				result.waveDurationByWave[waveIndex] = m_waveDurationByWave[waveIndex].Snapshot();
			}

			result.sectorStates.resize(m_sectorCount);
			for (std::size_t sectorIndex = 0; sectorIndex < m_sectorCount; ++sectorIndex)
			{
				result.sectorStates[sectorIndex] = m_sectorStates[sectorIndex].load(std::memory_order_relaxed);
			}

			std::lock_guard<std::mutex> lock(m_failureMutex);
			result.failureReason = m_failureReason;
			return result;
		}

	private:
		void BeginTickOnMain(
			const std::uint32_t tickIndex)
		{
			RecordMainWorker();
			if (m_finished.load(std::memory_order_acquire))
			{
				return;
			}

			m_currentTick.store(tickIndex, std::memory_order_release);
			m_lastCompletedWave.store(-1, std::memory_order_release);
			CaptureReferenceCheckpointOnMain(tickIndex);
			BeginWaveOnMain(0);
		}

		void CaptureReferenceCheckpointOnMain(
			const std::uint32_t tickIndex)
		{
			if (m_config.referenceCheckpointIntervalTicks == 0 || m_config.referenceCheckpointWindowTicks == 0 ||
				m_referenceCheckpointRemainingTicks != 0 || tickIndex % m_config.referenceCheckpointIntervalTicks != 0)
			{
				return;
			}

			m_referenceCheckpointStates.resize(m_sectorCount);
			for (std::size_t sectorIndex = 0; sectorIndex < m_sectorCount; ++sectorIndex)
			{
				m_referenceCheckpointStates[sectorIndex] = m_sectorStates[sectorIndex].load(std::memory_order_acquire);
			}
			m_referenceCheckpointTick = tickIndex;
			m_referenceCheckpointRemainingTicks = m_config.referenceCheckpointWindowTicks;
		}

		bool ValidateReferenceCheckpointOnMain(
			const std::uint32_t tickIndex)
		{
			if (m_referenceCheckpointTick != tickIndex)
			{
				return true;
			}

			AdvanceReferenceSectorStates(m_config, tickIndex, m_referenceCheckpointStates);
			for (std::size_t sectorIndex = 0; sectorIndex < m_sectorCount; ++sectorIndex)
			{
				const std::uint64_t actualState = m_sectorStates[sectorIndex].load(std::memory_order_acquire);
				if (actualState == m_referenceCheckpointStates[sectorIndex])
				{
					continue;
				}

				std::ostringstream reason;
				reason << "Reference Checkpoint 상태 불일치 tick=" << tickIndex << " sector=" << sectorIndex << " expected=0x" << std::hex
					   << std::uppercase << m_referenceCheckpointStates[sectorIndex] << " actual=0x" << actualState;
				Fail(EFailureCode::ReferenceStateMismatch, reason.str());
				return false;
			}

			m_referenceCheckpointCount.fetch_add(1, std::memory_order_relaxed);
			--m_referenceCheckpointRemainingTicks;
			m_referenceCheckpointTick =
				m_referenceCheckpointRemainingTicks == 0 ? std::numeric_limits<std::uint32_t>::max() : tickIndex + 1u;
			return true;
		}

		void BeginWaveOnMain(
			const std::uint32_t waveIndex)
		{
			RecordMainWorker();
			if (m_finished.load(std::memory_order_acquire))
			{
				return;
			}

			const std::uint32_t tickIndex = m_currentTick.load(std::memory_order_acquire);
			m_currentWave.store(waveIndex, std::memory_order_release);
			m_wavePumpPublicationComplete.store(false, std::memory_order_release);
			m_waveCompletedTaskCount.store(0, std::memory_order_release);
			m_remainingTaskCount.store(static_cast<std::uint32_t>(m_waves[waveIndex].size()), std::memory_order_release);
			m_waveStartedAtNanoseconds[waveIndex] = GetSteadyNanoseconds();

			const std::vector<STask*>& waveTasks = m_waves[waveIndex];
			const std::size_t taskCount = waveTasks.size();
			const std::uint64_t orderHash =
				Mix64(m_config.seed ^ (static_cast<std::uint64_t>(tickIndex) << 32u) ^ (static_cast<std::uint64_t>(waveIndex) << 56u));
			const std::size_t rotation = taskCount == 0 ? 0 : static_cast<std::size_t>(orderHash % taskCount);
			const bool reverseOrder = m_config.workloadMode != EWorkloadMode::Uniform && (orderHash & 1u) != 0;

			for (std::size_t ordinal = 0; ordinal < taskCount; ++ordinal)
			{
				const std::size_t logicalIndex = reverseOrder ? taskCount - ordinal - 1u : ordinal;
				STask* const task = waveTasks[(logicalIndex + rotation) % taskCount];
				task->tickIndex = tickIndex;
				task->enqueuedAtNanoseconds = GetSteadyNanoseconds();

				const std::uint64_t queuedCount = m_queuedTaskCount.fetch_add(1, std::memory_order_acq_rel) + 1;
				UpdateMaximum(m_queueHighWatermark, queuedCount);
				m_taskQueue.Enqueue(task);
			}

			for (const FContentInstanceId executorInstanceId : m_executorInstanceIds)
			{
				if (!ScheduleExecutorPump(executorInstanceId, tickIndex, waveIndex))
				{
					Fail(EFailureCode::RuntimeEnqueueFailure, "Shard Executor 작업을 ContentsRuntime에 등록하지 못했습니다.");
					return;
				}
			}

			m_wavePumpPublicationComplete.store(true, std::memory_order_release);
			if (HasQuiescentMissingTask(tickIndex, waveIndex))
			{
				Fail(EFailureCode::ExecutionInvariantViolation, "실행할 Pump가 없지만 Wave 완료 카운터가 남아 있습니다.");
			}
		}

		bool ScheduleExecutorPump(
			const FContentInstanceId executorInstanceId,
			const std::uint32_t tickIndex,
			const std::uint32_t waveIndex)
		{
			m_outstandingPumpCallbackCount.fetch_add(1, std::memory_order_acq_rel);
			const bool enqueued = m_runtime.EnqueueCompletionToInstance(executorInstanceId,
				[this, executorInstanceId, tickIndex, waveIndex]()
				{
					PumpTasks(executorInstanceId, tickIndex, waveIndex);
					OnPumpCallbackExited(tickIndex, waveIndex);
				});
			if (!enqueued)
			{
				m_outstandingPumpCallbackCount.fetch_sub(1, std::memory_order_acq_rel);
			}
			return enqueued;
		}

		void PumpTasks(
			const FContentInstanceId executorInstanceId,
			const std::uint32_t tickIndex,
			const std::uint32_t waveIndex)
		{
			if (m_finished.load(std::memory_order_acquire) || tickIndex != m_currentTick.load(std::memory_order_acquire) ||
				waveIndex != m_currentWave.load(std::memory_order_acquire))
			{
				return;
			}

			std::uint32_t processedCount = 0;
			while (processedCount < m_config.pumpBatchSize)
			{
				if (tickIndex != m_currentTick.load(std::memory_order_acquire) ||
					waveIndex != m_currentWave.load(std::memory_order_acquire))
				{
					break;
				}

				STask* task = nullptr;
				if (!m_taskQueue.Dequeue(task))
				{
					break;
				}

				m_inFlightTaskCount.fetch_add(1, std::memory_order_acq_rel);
				const std::uint64_t previousQueuedCount = m_queuedTaskCount.fetch_sub(1, std::memory_order_acq_rel);
				if (previousQueuedCount == 0)
				{
					m_queuedTaskCount.fetch_add(1, std::memory_order_relaxed);
					m_inFlightTaskCount.fetch_sub(1, std::memory_order_relaxed);
					Fail(EFailureCode::ExecutionInvariantViolation, "Queue 적재 카운터가 0 아래로 감소했습니다.");
					return;
				}

				if (task == nullptr)
				{
					m_inFlightTaskCount.fetch_sub(1, std::memory_order_relaxed);
					Fail(EFailureCode::ExecutionInvariantViolation, "Task Queue에서 null Task를 꺼냈습니다.");
					return;
				}

				const std::uint64_t dequeuedAtNanoseconds = GetSteadyNanoseconds();
				const std::uint64_t queueWaitNanoseconds =
					dequeuedAtNanoseconds >= task->enqueuedAtNanoseconds ? dequeuedAtNanoseconds - task->enqueuedAtNanoseconds : 0;
				RecordQueueWait(*task, queueWaitNanoseconds);
				ExecuteTask(*task);
				++processedCount;
			}

			if (processedCount == 0)
			{
				m_emptyPumpCount.fetch_add(1, std::memory_order_relaxed);
			}

			if (tickIndex != m_currentTick.load(std::memory_order_acquire) || waveIndex != m_currentWave.load(std::memory_order_acquire))
			{
				return;
			}

			if (processedCount == m_config.pumpBatchSize && m_remainingTaskCount.load(std::memory_order_acquire) > 0 &&
				!m_finished.load(std::memory_order_acquire))
			{
				m_rescheduledPumpCount.fetch_add(1, std::memory_order_relaxed);
				if (!ScheduleExecutorPump(executorInstanceId, tickIndex, waveIndex))
				{
					Fail(EFailureCode::RuntimeEnqueueFailure, "남은 Shard Task의 Pump 작업을 다시 등록하지 못했습니다.");
					return;
				}
			}
		}

		void OnPumpCallbackExited(
			const std::uint32_t tickIndex,
			const std::uint32_t waveIndex)
		{
			const std::uint64_t previousOutstanding = m_outstandingPumpCallbackCount.fetch_sub(1, std::memory_order_acq_rel);
			if (previousOutstanding == 0)
			{
				m_outstandingPumpCallbackCount.fetch_add(1, std::memory_order_relaxed);
				Fail(EFailureCode::ExecutionInvariantViolation, "Pump Callback 카운터가 0 아래로 감소했습니다.");
				return;
			}

			if (previousOutstanding == 1)
			{
				if (HasQuiescentMissingTask(tickIndex, waveIndex))
				{
					Fail(EFailureCode::ExecutionInvariantViolation, "실행할 Pump가 없지만 Wave 완료 카운터가 남아 있습니다.");
					return;
				}

				TryScheduleFinalize();
			}
		}

		bool HasQuiescentMissingTask(
			const std::uint32_t tickIndex,
			const std::uint32_t waveIndex) const noexcept
		{
			if (m_finished.load(std::memory_order_acquire) || tickIndex != m_currentTick.load(std::memory_order_acquire) ||
				waveIndex != m_currentWave.load(std::memory_order_acquire) ||
				!m_wavePumpPublicationComplete.load(std::memory_order_acquire) ||
				m_outstandingPumpCallbackCount.load(std::memory_order_acquire) != 0)
			{
				return false;
			}

			const std::uint32_t remainingTaskCount = m_remainingTaskCount.load(std::memory_order_acquire);
			return remainingTaskCount > 0 && tickIndex == m_currentTick.load(std::memory_order_acquire) &&
				   waveIndex == m_currentWave.load(std::memory_order_acquire) &&
				   m_wavePumpPublicationComplete.load(std::memory_order_acquire) &&
				   m_outstandingPumpCallbackCount.load(std::memory_order_acquire) == 0;
		}

		void ExecuteTask(
			const STask& task)
		{
			const std::uint64_t taskStartedAtNanoseconds = GetSteadyNanoseconds();
			const std::uint32_t workerIndex = ContentsRuntime::Threading::FContentThread::GetCurrentWorkerIndex();
			if (workerIndex >= m_runtimeWorkerCount)
			{
				Fail(EFailureCode::ExecutionInvariantViolation, "Task가 ContentsRuntime Worker 밖에서 실행됐습니다.");
			}
			else
			{
				m_workerUsedFlags[workerIndex].store(true, std::memory_order_relaxed);
				m_workerTaskCounts[workerIndex].fetch_add(1, std::memory_order_relaxed);
			}

			const std::uint32_t currentTick = m_currentTick.load(std::memory_order_acquire);
			const std::uint32_t currentWave = m_currentWave.load(std::memory_order_acquire);
			if (task.tickIndex != currentTick || task.waveIndex != currentWave)
			{
				Fail(EFailureCode::ExecutionInvariantViolation, "현재 Wave와 다른 Task가 실행됐습니다.");
			}

			const std::int32_t expectedCompletedWave = task.waveIndex == 0 ? -1 : static_cast<std::int32_t>(task.waveIndex - 1);
			if (m_lastCompletedWave.load(std::memory_order_acquire) != expectedCompletedWave)
			{
				Fail(EFailureCode::ExecutionInvariantViolation, "이전 Wave가 끝나기 전에 다음 Wave Task가 실행됐습니다.");
			}

			if (m_sectorActiveFlags[task.sectorIndex].exchange(true, std::memory_order_acq_rel))
			{
				Fail(EFailureCode::ExecutionInvariantViolation, "같은 Sector Task가 동시에 두 번 실행됐습니다.");
			}

			const std::uint32_t parallelTaskCount = m_activeTaskCount.fetch_add(1, std::memory_order_acq_rel) + 1;
			UpdateMaximum(m_maxParallelTasks, parallelTaskCount);
			if (parallelTaskCount >= 2)
			{
				m_naturalParallelOverlapCount.fetch_add(1, std::memory_order_relaxed);
			}
			RunParallelStartProbe(currentTick, currentWave);
			ValidateNoAdjacentWriter(task);

			const std::uint32_t workIterations = ResolveWorkIterations(m_config, currentTick, task.sectorIndex, task.waveIndex);
			if (workerIndex < m_runtimeWorkerCount)
			{
				m_workerWorkIterations[workerIndex].fetch_add(workIterations, std::memory_order_relaxed);
			}
			RunDeterministicSectorWork(task, workIterations);

			const std::uint32_t previousExecutionCount = m_sectorExecutionCounts[task.sectorIndex].fetch_add(1, std::memory_order_acq_rel);
			if (previousExecutionCount != currentTick)
			{
				Fail(EFailureCode::ExecutionInvariantViolation, "Sector Task의 중복 실행 또는 Tick 누락을 발견했습니다.");
			}

			m_sectorActiveFlags[task.sectorIndex].store(false, std::memory_order_release);
			m_activeTaskCount.fetch_sub(1, std::memory_order_acq_rel);
			m_waveCompletedTaskCount.fetch_add(1, std::memory_order_acq_rel);
			m_completedTaskCount.fetch_add(1, std::memory_order_relaxed);

			const std::uint64_t taskFinishedAtNanoseconds = GetSteadyNanoseconds();
			RecordTaskExecution(
				task, taskFinishedAtNanoseconds >= taskStartedAtNanoseconds ? taskFinishedAtNanoseconds - taskStartedAtNanoseconds : 0);
			m_inFlightTaskCount.fetch_sub(1, std::memory_order_acq_rel);
			const std::uint32_t previousRemaining = m_remainingTaskCount.fetch_sub(1, std::memory_order_acq_rel);
			if (previousRemaining == 0)
			{
				m_remainingTaskCount.fetch_add(1, std::memory_order_relaxed);
				Fail(EFailureCode::ExecutionInvariantViolation, "Wave Task 완료 카운터가 0 아래로 감소했습니다.");
				return;
			}

			if (previousRemaining != 1)
			{
				return;
			}

			if (m_queuedTaskCount.load(std::memory_order_acquire) != 0 || m_inFlightTaskCount.load(std::memory_order_acquire) != 0 ||
				m_activeTaskCount.load(std::memory_order_acquire) != 0)
			{
				Fail(EFailureCode::ExecutionInvariantViolation, "마지막 Wave Task 완료 시 실행 카운터가 0이 아닙니다.");
				return;
			}

			if (!m_runtime.EnqueueCompletionToInstance(m_mainInstanceId,
					[this, tickIndex = currentTick, waveIndex = currentWave, notificationEnqueuedAtNanoseconds = GetSteadyNanoseconds()]()
					{
						CompleteWaveOnMain(tickIndex, waveIndex, notificationEnqueuedAtNanoseconds);
					}))
			{
				Fail(EFailureCode::RuntimeEnqueueFailure, "Wave 완료 알림을 Main Instance에 등록하지 못했습니다.");
			}
		}

		void RunParallelStartProbe(
			const std::uint32_t tickIndex,
			const std::uint32_t waveIndex)
		{
			if (m_parallelProbeTarget < 2 || tickIndex != 0 || waveIndex != 0)
			{
				return;
			}

			const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
			while (m_activeTaskCount.load(std::memory_order_acquire) < m_parallelProbeTarget &&
				   !m_finished.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline)
			{
				std::this_thread::yield();
			}
		}

		void ValidateNoAdjacentWriter(
			const STask& task)
		{
			const std::int32_t gridSize = static_cast<std::int32_t>(m_config.gridSize);
			for (std::int32_t yOffset = -1; yOffset <= 1; ++yOffset)
			{
				for (std::int32_t xOffset = -1; xOffset <= 1; ++xOffset)
				{
					if (xOffset == 0 && yOffset == 0)
					{
						continue;
					}

					const std::int32_t neighborX = static_cast<std::int32_t>(task.x) + xOffset;
					const std::int32_t neighborY = static_cast<std::int32_t>(task.y) + yOffset;
					if (neighborX < 0 || neighborY < 0 || neighborX >= gridSize || neighborY >= gridSize)
					{
						continue;
					}

					const std::size_t neighborIndex =
						static_cast<std::size_t>(neighborY) * m_config.gridSize + static_cast<std::size_t>(neighborX);
					if (m_sectorActiveFlags[neighborIndex].load(std::memory_order_acquire))
					{
						Fail(EFailureCode::ExecutionInvariantViolation, "인접 Sector Task가 같은 Wave에서 동시에 실행됐습니다.");
					}
				}
			}
		}

		bool ValidateWavePartition() const
		{
			const std::int32_t gridSize = static_cast<std::int32_t>(m_config.gridSize);
			for (std::size_t sectorIndex = 0; sectorIndex < m_sectorCount; ++sectorIndex)
			{
				const STask& task = m_tasks[sectorIndex];
				for (std::int32_t yOffset = -1; yOffset <= 1; ++yOffset)
				{
					for (std::int32_t xOffset = -1; xOffset <= 1; ++xOffset)
					{
						if (xOffset == 0 && yOffset == 0)
						{
							continue;
						}

						const std::int32_t neighborX = static_cast<std::int32_t>(task.x) + xOffset;
						const std::int32_t neighborY = static_cast<std::int32_t>(task.y) + yOffset;
						if (neighborX < 0 || neighborY < 0 || neighborX >= gridSize || neighborY >= gridSize)
						{
							continue;
						}

						const std::size_t neighborIndex =
							static_cast<std::size_t>(neighborY) * m_config.gridSize + static_cast<std::size_t>(neighborX);
						if (task.waveIndex == m_tasks[neighborIndex].waveIndex)
						{
							return false;
						}
					}
				}
			}

			return true;
		}

		void RunDeterministicSectorWork(
			const STask& task,
			const std::uint32_t workIterations)
		{
			std::uint64_t neighborMix = 0xCBF29CE484222325ull;
			const std::int32_t gridSize = static_cast<std::int32_t>(m_config.gridSize);
			for (std::int32_t yOffset = -1; yOffset <= 1; ++yOffset)
			{
				for (std::int32_t xOffset = -1; xOffset <= 1; ++xOffset)
				{
					if (xOffset == 0 && yOffset == 0)
					{
						continue;
					}

					const std::int32_t neighborX = static_cast<std::int32_t>(task.x) + xOffset;
					const std::int32_t neighborY = static_cast<std::int32_t>(task.y) + yOffset;
					if (neighborX < 0 || neighborY < 0 || neighborX >= gridSize || neighborY >= gridSize)
					{
						continue;
					}

					const std::size_t neighborIndex =
						static_cast<std::size_t>(neighborY) * m_config.gridSize + static_cast<std::size_t>(neighborX);
					const std::uint64_t neighborValue = m_sectorStates[neighborIndex].load(std::memory_order_relaxed);
					neighborMix ^= neighborValue + 0x9E3779B97F4A7C15ull + (neighborMix << 6u) + (neighborMix >> 2u);
				}
			}

			std::uint64_t value = m_sectorStates[task.sectorIndex].load(std::memory_order_relaxed);
			value ^= neighborMix + static_cast<std::uint64_t>(task.tickIndex + 1u) * 0xD6E8FEB86659FD93ull;
			for (std::uint32_t iteration = 0; iteration < workIterations; ++iteration)
			{
				value ^= value >> 30u;
				value *= 0xBF58476D1CE4E5B9ull;
				value ^= value >> 27u;
				value *= 0x94D049BB133111EBull;
				value ^= value >> 31u;
				value += neighborMix ^ static_cast<std::uint64_t>(iteration + 1u);
			}

			m_sectorStates[task.sectorIndex].store(value, std::memory_order_relaxed);
		}

		void CompleteWaveOnMain(
			const std::uint32_t tickIndex,
			const std::uint32_t waveIndex,
			const std::uint64_t notificationEnqueuedAtNanoseconds)
		{
			RecordMainWorker();
			if (m_finished.load(std::memory_order_acquire))
			{
				return;
			}

			const std::uint64_t completedAtNanoseconds = GetSteadyNanoseconds();
			if (tickIndex != m_currentTick.load(std::memory_order_acquire) || waveIndex != m_currentWave.load(std::memory_order_acquire))
			{
				Fail(EFailureCode::ExecutionInvariantViolation, "Main Instance가 오래된 Wave 완료 알림을 받았습니다.");
				return;
			}

			if (m_remainingTaskCount.load(std::memory_order_acquire) != 0 ||
				m_waveCompletedTaskCount.load(std::memory_order_acquire) != static_cast<std::uint32_t>(m_waves[waveIndex].size()))
			{
				Fail(EFailureCode::ExecutionInvariantViolation, "Wave Barrier가 모든 Task 완료 전에 열렸습니다.");
				return;
			}

			RecordWaveTiming(tickIndex, waveIndex, completedAtNanoseconds, notificationEnqueuedAtNanoseconds);
			m_lastCompletedWave.store(static_cast<std::int32_t>(waveIndex), std::memory_order_release);
			if (waveIndex + 1u < kWaveCount)
			{
				BeginWaveOnMain(waveIndex + 1u);
				return;
			}

			if (!ValidateReferenceCheckpointOnMain(tickIndex))
			{
				return;
			}

			const std::uint32_t completedTickCount = tickIndex + 1u;
			m_completedTickCount.store(completedTickCount, std::memory_order_release);
			bool shouldContinue = false;
			if (m_config.durationSeconds > 0)
			{
				const std::uint64_t targetDurationNanoseconds = static_cast<std::uint64_t>(m_config.durationSeconds) * 1'000'000'000ull;
				shouldContinue = completedAtNanoseconds - m_startNanoseconds < targetDurationNanoseconds;
			}
			else
			{
				shouldContinue = completedTickCount < m_config.tickCount;
			}

			if (shouldContinue)
			{
				BeginTickOnMain(completedTickCount);
				return;
			}

			m_finishRequested.store(true, std::memory_order_release);
			TryScheduleFinalize();
		}

		void RecordMainWorker()
		{
			const std::uint32_t workerIndex = ContentsRuntime::Threading::FContentThread::GetCurrentWorkerIndex();
			std::uint32_t expected = kUnassignedWorkerIndex;
			if (m_mainWorkerIndex.compare_exchange_strong(expected, workerIndex, std::memory_order_acq_rel, std::memory_order_acquire))
			{
				return;
			}

			if (expected != workerIndex)
			{
				Fail(EFailureCode::ExecutionInvariantViolation, "Main Instance 실행 Worker가 테스트 도중 바뀌었습니다.");
			}
		}

		void RecordQueueWait(
			const STask& task,
			const std::uint64_t elapsedNanoseconds)
		{
			if (task.tickIndex == 0)
			{
				return;
			}

			m_taskQueueWait.Record(elapsedNanoseconds);
			m_taskQueueWaitByWave[task.waveIndex].Record(elapsedNanoseconds);
			RecordFoundationTiming(kTaskQueueWaitMetric, elapsedNanoseconds, BuildTimingContext(task.tickIndex, task.sectorIndex));
		}

		void RecordTaskExecution(
			const STask& task,
			const std::uint64_t elapsedNanoseconds)
		{
			if (task.tickIndex == 0)
			{
				return;
			}

			m_taskExecute.Record(elapsedNanoseconds);
			RecordFoundationTiming(kTaskExecuteMetric, elapsedNanoseconds, BuildTimingContext(task.tickIndex, task.sectorIndex));
		}

		void RecordWaveTiming(
			const std::uint32_t tickIndex,
			const std::uint32_t waveIndex,
			const std::uint64_t completedAtNanoseconds,
			const std::uint64_t notificationEnqueuedAtNanoseconds)
		{
			if (tickIndex == 0)
			{
				return;
			}

			const std::uint64_t waveDurationNanoseconds = completedAtNanoseconds >= m_waveStartedAtNanoseconds[waveIndex]
															  ? completedAtNanoseconds - m_waveStartedAtNanoseconds[waveIndex]
															  : 0;
			const std::uint64_t notifyWaitNanoseconds = completedAtNanoseconds >= notificationEnqueuedAtNanoseconds
															? completedAtNanoseconds - notificationEnqueuedAtNanoseconds
															: 0;

			m_waveTotal.Record(waveDurationNanoseconds);
			m_waveDurationByWave[waveIndex].Record(waveDurationNanoseconds);
			m_waveNotifyWait.Record(notifyWaitNanoseconds);
			RecordFoundationTiming(kWaveTotalMetric, waveDurationNanoseconds, BuildTimingContext(tickIndex, waveIndex));
			RecordFoundationTiming(kWaveNotifyWaitMetric, notifyWaitNanoseconds, BuildTimingContext(tickIndex, waveIndex));
		}

		void RecordFoundationTiming(
			const FTimingMetricIndex metricIndex,
			const std::uint64_t elapsedNanoseconds,
			const std::uint64_t contextId)
		{
			if (m_timingMetricsRuntime == nullptr)
			{
				return;
			}

			FTimingThreadLocalCollector collector(m_timingMetricsRuntime);
			collector.RecordDuration(metricIndex, std::chrono::nanoseconds(elapsedNanoseconds), contextId);
		}

		void TryScheduleFinalize()
		{
			if (!m_finishRequested.load(std::memory_order_acquire) || m_finished.load(std::memory_order_acquire) ||
				m_outstandingPumpCallbackCount.load(std::memory_order_acquire) != 0)
			{
				return;
			}

			bool expected = false;
			if (!m_finalizeCallbackScheduled.compare_exchange_strong(expected, true, std::memory_order_acq_rel, std::memory_order_acquire))
			{
				return;
			}

			if (!m_runtime.EnqueueCompletionToInstance(m_mainInstanceId,
					[this]()
					{
						FinishSuccess();
					}))
			{
				Fail(EFailureCode::RuntimeEnqueueFailure, "최종 종료 검증 작업을 Main Instance에 등록하지 못했습니다.");
			}
		}

		void FinishSuccess()
		{
			const std::uint32_t completedTickCount = m_completedTickCount.load(std::memory_order_acquire);
			for (std::size_t sectorIndex = 0; sectorIndex < m_sectorCount; ++sectorIndex)
			{
				if (m_sectorExecutionCounts[sectorIndex].load(std::memory_order_acquire) != completedTickCount)
				{
					Fail(EFailureCode::ExecutionInvariantViolation, "모든 완료 Tick에서 한 번씩 실행되지 않은 Sector가 있습니다.");
					return;
				}
			}

			if (m_queuedTaskCount.load(std::memory_order_acquire) != 0 || m_inFlightTaskCount.load(std::memory_order_acquire) != 0 ||
				m_activeTaskCount.load(std::memory_order_acquire) != 0 || m_remainingTaskCount.load(std::memory_order_acquire) != 0 ||
				m_outstandingPumpCallbackCount.load(std::memory_order_acquire) != 0)
			{
				Fail(EFailureCode::ExecutionInvariantViolation, "종료 시점에 Queue, In-Flight 또는 Remaining Task가 남았습니다.");
				return;
			}

			for (std::size_t sectorIndex = 0; sectorIndex < m_sectorCount; ++sectorIndex)
			{
				if (m_sectorActiveFlags[sectorIndex].load(std::memory_order_acquire))
				{
					Fail(EFailureCode::ExecutionInvariantViolation, "종료 시점에 실행 중으로 표시된 Sector가 남았습니다.");
					return;
				}
			}

			std::uint64_t checksum = 0x84222325CBF29CE4ull;
			for (std::size_t sectorIndex = 0; sectorIndex < m_sectorCount; ++sectorIndex)
			{
				const std::uint64_t value = m_sectorStates[sectorIndex].load(std::memory_order_relaxed);
				checksum ^= value + 0x9E3779B97F4A7C15ull + (checksum << 6u) + (checksum >> 2u);
			}

			m_checksum.store(checksum, std::memory_order_relaxed);
			m_elapsedSeconds.store(
				std::chrono::duration<double>(std::chrono::steady_clock::now() - m_startTime).count(), std::memory_order_relaxed);

			bool expected = false;
			if (m_finished.compare_exchange_strong(expected, true, std::memory_order_release, std::memory_order_relaxed))
			{
				m_completionCondition.notify_all();
			}
		}

		void Fail(
			const EFailureCode failureCode,
			std::string reason)
		{
			EFailureCode expectedFailureCode = EFailureCode::None;
			if (!m_failureCode.compare_exchange_strong(
					expectedFailureCode, failureCode, std::memory_order_acq_rel, std::memory_order_acquire))
			{
				return;
			}

			{
				std::lock_guard<std::mutex> lock(m_failureMutex);
				m_failureReason = std::move(reason);
			}

			m_failed.store(true, std::memory_order_release);
			m_elapsedSeconds.store(
				std::chrono::duration<double>(std::chrono::steady_clock::now() - m_startTime).count(), std::memory_order_relaxed);
			bool expected = false;
			if (m_finished.compare_exchange_strong(expected, true, std::memory_order_release, std::memory_order_relaxed))
			{
				m_completionCondition.notify_all();
			}
		}

	private:
		ContentsRuntime::Routing::FContentRuntime& m_runtime;
		FContentInstanceId m_mainInstanceId = ContentsRuntime::Core::kInvalidContentInstanceId;
		std::vector<FContentInstanceId> m_executorInstanceIds;
		STestConfig m_config;
		std::uint32_t m_runtimeWorkerCount = 0;
		std::uint32_t m_parallelProbeTarget = 0;
		bool m_wavePartitionValid = false;
		std::string m_label;
		FTimingMetricsRuntime* m_timingMetricsRuntime = nullptr;
		std::size_t m_sectorCount = 0;
		std::unique_ptr<STask[]> m_tasks;
		std::array<std::vector<STask*>, kWaveCount> m_waves;
		NetworkLib::Containers::FLockFreeQueue<STask*> m_taskQueue;
		std::unique_ptr<std::atomic<std::uint64_t>[]> m_sectorStates;
		std::unique_ptr<std::atomic<std::uint32_t>[]> m_sectorExecutionCounts;
		std::unique_ptr<std::atomic<bool>[]> m_sectorActiveFlags;
		std::unique_ptr<std::atomic<bool>[]> m_workerUsedFlags;
		std::unique_ptr<std::atomic<std::uint64_t>[]> m_workerTaskCounts;
		std::unique_ptr<std::atomic<std::uint64_t>[]> m_workerWorkIterations;
		std::vector<std::uint64_t> m_referenceCheckpointStates;
		std::uint32_t m_referenceCheckpointTick = std::numeric_limits<std::uint32_t>::max();
		std::uint32_t m_referenceCheckpointRemainingTicks = 0;
		std::array<std::uint64_t, kWaveCount> m_waveStartedAtNanoseconds{};
		SAtomicTimingSummary m_taskQueueWait;
		SAtomicTimingSummary m_taskExecute;
		SAtomicTimingSummary m_waveTotal;
		SAtomicTimingSummary m_waveNotifyWait;
		std::array<SAtomicTimingSummary, kWaveCount> m_taskQueueWaitByWave{};
		std::array<SAtomicTimingSummary, kWaveCount> m_waveDurationByWave{};
		std::atomic<std::uint32_t> m_currentTick = 0;
		std::atomic<std::uint32_t> m_currentWave = 0;
		std::atomic<std::int32_t> m_lastCompletedWave = -1;
		std::atomic<std::uint32_t> m_remainingTaskCount = 0;
		std::atomic<std::uint32_t> m_waveCompletedTaskCount = 0;
		std::atomic<std::uint32_t> m_activeTaskCount = 0;
		std::atomic<std::uint32_t> m_maxParallelTasks = 0;
		std::atomic<std::uint64_t> m_naturalParallelOverlapCount = 0;
		std::atomic<std::uint32_t> m_mainWorkerIndex = kUnassignedWorkerIndex;
		std::atomic<std::uint32_t> m_completedTickCount = 0;
		std::atomic<std::uint64_t> m_completedTaskCount = 0;
		std::atomic<std::uint64_t> m_queuedTaskCount = 0;
		std::atomic<std::uint64_t> m_inFlightTaskCount = 0;
		std::atomic<std::uint64_t> m_queueHighWatermark = 0;
		std::atomic<std::uint64_t> m_emptyPumpCount = 0;
		std::atomic<std::uint64_t> m_rescheduledPumpCount = 0;
		std::atomic<std::uint64_t> m_referenceCheckpointCount = 0;
		std::atomic<std::uint64_t> m_outstandingPumpCallbackCount = 0;
		std::atomic<std::uint64_t> m_checksum = 0;
		std::atomic<double> m_elapsedSeconds = 0.0;
		std::atomic<EFailureCode> m_failureCode = EFailureCode::None;
		std::atomic<bool> m_wavePumpPublicationComplete = false;
		std::atomic<bool> m_finishRequested = false;
		std::atomic<bool> m_finalizeCallbackScheduled = false;
		std::atomic<bool> m_failed = false;
		std::atomic<bool> m_finished = false;
		std::chrono::steady_clock::time_point m_startTime{};
		std::uint64_t m_startNanoseconds = 0;
		mutable std::mutex m_failureMutex;
		std::string m_failureReason;
		std::mutex m_completionMutex;
		std::condition_variable m_completionCondition;
	};

	std::vector<std::uint64_t> BuildReferenceSectorStates(
		const STestConfig& config)
	{
		const std::size_t sectorCount = static_cast<std::size_t>(config.gridSize) * config.gridSize;
		std::vector<std::uint64_t> sectorStates(sectorCount);
		for (std::size_t sectorIndex = 0; sectorIndex < sectorCount; ++sectorIndex)
		{
			sectorStates[sectorIndex] = (static_cast<std::uint64_t>(sectorIndex) + 1u) * 0x9E3779B185EBCA87ull;
		}

		for (std::uint32_t tickIndex = 0; tickIndex < config.tickCount; ++tickIndex)
		{
			AdvanceReferenceSectorStates(config, tickIndex, sectorStates);
		}

		return sectorStates;
	}

	std::uint64_t CalculateChecksum(
		const std::vector<std::uint64_t>& sectorStates)
	{
		std::uint64_t checksum = 0x84222325CBF29CE4ull;
		for (const std::uint64_t value : sectorStates)
		{
			checksum ^= value + 0x9E3779B97F4A7C15ull + (checksum << 6u) + (checksum >> 2u);
		}
		return checksum;
	}

	bool CompareSectorStates(
		const std::vector<std::uint64_t>& expectedStates,
		const SRunResult& result)
	{
		if (expectedStates.size() != result.sectorStates.size())
		{
			std::cerr << "[FAIL:" << result.label << "] Sector 수가 다릅니다. expected=" << expectedStates.size()
					  << " actual=" << result.sectorStates.size() << '\n';
			return false;
		}

		for (std::size_t sectorIndex = 0; sectorIndex < expectedStates.size(); ++sectorIndex)
		{
			if (expectedStates[sectorIndex] == result.sectorStates[sectorIndex])
			{
				continue;
			}

			std::cerr << "[FAIL:" << result.label << "] Reference 상태 불일치 sector=" << sectorIndex << " expected=0x" << std::hex
					  << std::uppercase << expectedStates[sectorIndex] << " actual=0x" << result.sectorStates[sectorIndex] << std::dec
					  << std::nouppercase << '\n';
			return false;
		}

		return true;
	}

	std::string SanitizeLabel(
		std::string label)
	{
		for (char& character : label)
		{
			const unsigned char unsignedCharacter = static_cast<unsigned char>(character);
			if (!std::isalnum(unsignedCharacter) && character != '-' && character != '_')
			{
				character = '_';
			}
		}
		return label;
	}

	bool ValidateTimingCsv(
		const std::filesystem::path& csvPath,
		std::string& outFailureReason)
	{
		std::error_code error;
		if (!std::filesystem::is_regular_file(csvPath, error) || error)
		{
			outFailureReason = "Timing CSV 파일이 생성되지 않았습니다: " + csvPath.string();
			return false;
		}

		std::ifstream csvStream(csvPath, std::ios::in);
		if (!csvStream.is_open())
		{
			outFailureReason = "Timing CSV 파일을 열 수 없습니다: " + csvPath.string();
			return false;
		}

		const std::string csvText((std::istreambuf_iterator<char>(csvStream)), std::istreambuf_iterator<char>());
		if (csvText.find("bucket_start_local,metric,") == std::string::npos)
		{
			outFailureReason = "Timing CSV 헤더가 없거나 손상됐습니다.";
			return false;
		}

		constexpr std::array<std::string_view, 4> requiredMetrics = {",TaskQueueWait,", ",TaskExecute,", ",WaveTotal,", ",WaveNotifyWait,"};
		for (const std::string_view metric : requiredMetrics)
		{
			if (csvText.find(metric) == std::string::npos)
			{
				outFailureReason = "Timing CSV에 필수 Metric 표본이 없습니다: " + std::string(metric);
				return false;
			}
		}

		return true;
	}

	SRunResult RunScenario(
		const STestConfig& config,
		const std::uint32_t runtimeWorkerCount,
		const std::string_view label)
	{
		std::unique_ptr<FTimingMetricsRuntime> timingMetricsRuntime;
		std::unique_ptr<FTimingCsvLogger> timingCsvLogger;
		std::filesystem::path timingCsvPath;
		if (config.timingEnabled)
		{
			try
			{
				std::filesystem::create_directories(config.timingOutputDirectory);
				STimingMetricsConfig timingConfig{};
				timingConfig.flushIntervalSeconds = 60;
				timingConfig.metricNames = {"TaskQueueWait", "TaskExecute", "WaveTotal", "WaveNotifyWait"};
				timingMetricsRuntime = std::make_unique<FTimingMetricsRuntime>(timingConfig);
				timingCsvPath = config.timingOutputDirectory / ("taskgraph_" + SanitizeLabel(std::string(label)) + ".csv");
				timingCsvLogger = std::make_unique<FTimingCsvLogger>(*timingMetricsRuntime, timingCsvPath.string());
				timingCsvLogger->Start();
			}
			catch (const std::exception& exception)
			{
				SRunResult failure{};
				failure.label = std::string(label);
				failure.failureCode = EFailureCode::RuntimeEnqueueFailure;
				failure.failureReason = std::string("Timing CSV 초기화 실패: ") + exception.what();
				return failure;
			}
		}

		NetworkLib::Core::FStubServer server(NetworkLib::Core::EBackendKind::Iocp);
		ContentsRuntime::Routing::FContentRuntime runtime;
		ContentsRuntime::Core::SContentRuntimeConfig runtimeConfig{};
		runtimeConfig.workerThreadCount = runtimeWorkerCount;
		runtimeConfig.enableOwnershipTransferPolicy = false;
		runtimeConfig.failFastOnRuntimeError = false;
		runtimeConfig.timingMetricsRuntime = timingMetricsRuntime.get();
		runtime.SetConfig(runtimeConfig);

		const FContentInstanceId mainInstanceId =
			ContentsRuntime::Core::MakeContentInstanceId(kMainContentId, ContentsRuntime::Core::kDefaultContentInstanceReserveBits, 1);
		std::vector<FContentInstanceId> executorInstanceIds;
		executorInstanceIds.reserve(config.workerCount);

		if (!runtime.RegisterContent(std::make_unique<FTaskHostContent>(kMainContentId, mainInstanceId)))
		{
			SRunResult failure{};
			failure.label = std::string(label);
			failure.failureCode = EFailureCode::RuntimeEnqueueFailure;
			failure.failureReason = "Main Content Instance 등록에 실패했습니다.";
			if (timingCsvLogger != nullptr)
			{
				timingCsvLogger->Stop();
			}
			return failure;
		}

		for (std::uint32_t index = 0; index < config.workerCount; ++index)
		{
			const FContentInstanceId instanceId = ContentsRuntime::Core::MakeContentInstanceId(
				kExecutorContentId, ContentsRuntime::Core::kDefaultContentInstanceReserveBits, static_cast<std::uint64_t>(index) + 1u);
			executorInstanceIds.push_back(instanceId);
			if (!runtime.RegisterContent(std::make_unique<FTaskHostContent>(kExecutorContentId, instanceId)))
			{
				SRunResult failure{};
				failure.label = std::string(label);
				failure.failureCode = EFailureCode::RuntimeEnqueueFailure;
				failure.failureReason = "Shard Executor Content Instance 등록에 실패했습니다.";
				if (timingCsvLogger != nullptr)
				{
					timingCsvLogger->Stop();
				}
				return failure;
			}
		}

		FTaskGraphScenario scenario(runtime,
			mainInstanceId,
			std::move(executorInstanceIds),
			config,
			runtimeWorkerCount,
			std::string(label),
			timingMetricsRuntime.get());

		runtime.Start(server);
		const bool started = scenario.Start();
		if (started)
		{
			scenario.Wait();
		}
		runtime.Stop();
		if (timingCsvLogger != nullptr)
		{
			timingCsvLogger->Stop();
		}

		SRunResult result = scenario.BuildResult();
		result.timingCsvPath = timingCsvPath;
		if (result.passed && config.timingEnabled)
		{
			std::string timingFailureReason;
			if (!ValidateTimingCsv(timingCsvPath, timingFailureReason))
			{
				result.passed = false;
				result.failureCode = EFailureCode::TimingOutputFailure;
				result.failureReason = std::move(timingFailureReason);
			}
		}
		return result;
	}

	void PrintDuration(
		const std::string_view label,
		const STimingSummary& timing)
	{
		std::cout << "  " << std::left << std::setw(18) << label << std::right << " avg=" << std::fixed << std::setprecision(2)
				  << timing.GetAverageMicroseconds() << " us max=" << timing.GetMaximumMicroseconds()
				  << " us samples=" << timing.sampleCount << '\n';
	}

	void PrintResult(
		const SRunResult& result)
	{
		const double tasksPerSecond =
			result.elapsedSeconds > 0.0 ? static_cast<double>(result.completedTaskCount) / result.elapsedSeconds : 0.0;

		std::cout << '[' << result.label << "] " << (result.passed ? "PASS" : "FAIL") << '\n'
				  << "  runtime workers   : " << result.runtimeWorkerCount << '\n'
				  << "  workers used      : " << result.workersUsed << '\n'
				  << "  max parallel      : " << result.maxParallelTasks << '\n'
				  << "  parallel overlaps : " << result.naturalParallelOverlapCount << '\n'
				  << "  completed ticks   : " << result.completedTickCount << '\n'
				  << "  completed tasks   : " << result.completedTaskCount << " / " << result.expectedTaskCount << '\n'
				  << "  queue high-water  : " << result.queueHighWatermark << '\n'
				  << "  empty/rescheduled : " << result.emptyPumpCount << " / " << result.rescheduledPumpCount << '\n'
				  << "  reference checks  : " << result.referenceCheckpointCount << '\n'
				  << "  elapsed           : " << std::fixed << std::setprecision(3) << result.elapsedSeconds << " sec\n"
				  << "  task throughput   : " << std::fixed << std::setprecision(0) << tasksPerSecond << " task/s\n"
				  << "  checksum          : 0x" << std::hex << std::uppercase << result.checksum << std::dec << std::nouppercase << '\n';

		PrintDuration("queue wait", result.taskQueueWait);
		PrintDuration("task execute", result.taskExecute);
		PrintDuration("wave total", result.waveTotal);
		PrintDuration("wave notify wait", result.waveNotifyWait);

		for (std::uint32_t waveIndex = 0; waveIndex < kWaveCount; ++waveIndex)
		{
			std::cout << "  wave " << waveIndex << " queue(avg/max)=" << std::fixed << std::setprecision(2)
					  << result.queueWaitByWave[waveIndex].GetAverageMicroseconds() << '/'
					  << result.queueWaitByWave[waveIndex].GetMaximumMicroseconds()
					  << " us total(avg/max)=" << result.waveDurationByWave[waveIndex].GetAverageMicroseconds() << '/'
					  << result.waveDurationByWave[waveIndex].GetMaximumMicroseconds() << " us\n";
		}

		if (!result.workerTaskCounts.empty())
		{
			std::cout << "  worker distribution:";
			for (std::size_t workerIndex = 0; workerIndex < result.workerTaskCounts.size(); ++workerIndex)
			{
				std::cout << " W" << workerIndex << '=' << result.workerTaskCounts[workerIndex] << "task/"
						  << result.workerWorkIterations[workerIndex] << "iter";
			}
			std::cout << '\n';
		}

		if (!result.timingCsvPath.empty())
		{
			std::cout << "  timing csv        : " << result.timingCsvPath.string() << '\n';
		}

		if (!result.failureReason.empty())
		{
			std::cout << "  failure           : " << GetFailureCodeName(result.failureCode) << " - " << result.failureReason << '\n';
		}
	}

	bool IsParallelExecutionExpected(
		const STestConfig& config,
		const SRunResult& result)
	{
		const std::uint32_t firstWaveWidth = (config.gridSize + 1u) / 2u;
		return result.runtimeWorkerCount > 1 && firstWaveWidth * firstWaveWidth > 1;
	}

	bool ValidateRunResult(
		const STestConfig& config,
		const SRunResult& result,
		const std::vector<std::uint64_t>* const referenceStates)
	{
		bool passed = result.passed;
		if (result.completedTaskCount != result.expectedTaskCount)
		{
			std::cerr << "[FAIL:" << result.label << "] 완료 Task 수가 기대값과 다릅니다.\n";
			passed = false;
		}

		if (config.durationSeconds == 0 && result.completedTickCount != config.tickCount)
		{
			std::cerr << "[FAIL:" << result.label << "] 완료 Tick 수가 기대값과 다릅니다.\n";
			passed = false;
		}

		if (config.durationSeconds > 0 && result.elapsedSeconds + 0.001 < static_cast<double>(config.durationSeconds))
		{
			std::cerr << "[FAIL:" << result.label << "] 설정한 Soak 시간이 끝나기 전에 종료됐습니다.\n";
			passed = false;
		}

		if (config.referenceCheckpointIntervalTicks > 0 && result.referenceCheckpointCount == 0)
		{
			std::cerr << "[FAIL:" << result.label << "] Reference Checkpoint가 한 번도 실행되지 않았습니다.\n";
			passed = false;
		}

		if (config.requireParallelObservation && IsParallelExecutionExpected(config, result) &&
			(result.workersUsed < 2 || result.maxParallelTasks < 2 || result.naturalParallelOverlapCount == 0))
		{
			std::cerr << "[FAIL:" << result.label << "] 실제 다중 Worker 병렬 실행이 관찰되지 않았습니다.\n";
			passed = false;
		}

		if (referenceStates != nullptr)
		{
			const std::uint64_t referenceChecksum = CalculateChecksum(*referenceStates);
			if (result.checksum != referenceChecksum)
			{
				std::cerr << "[FAIL:" << result.label << "] Reference 체크섬이 다릅니다. expected=0x" << std::hex << std::uppercase
						  << referenceChecksum << " actual=0x" << result.checksum << std::dec << std::nouppercase << '\n';
				passed = false;
			}

			if (!CompareSectorStates(*referenceStates, result))
			{
				passed = false;
			}
		}

		return passed;
	}

	bool RunInvalidWaveInjectionSelfTest(
		const std::uint32_t workerCount)
	{
		STestConfig config{};
		config.gridSize = 4;
		config.tickCount = 1;
		config.workerCount = std::max<std::uint32_t>(1u, workerCount);
		config.workIterations = 1;
		config.injectInvalidWavePartition = true;
		config.timingEnabled = false;
		const SRunResult result = RunScenario(config, config.workerCount, "invalid-wave-injection");
		const bool passed = !result.passed && result.failureCode == EFailureCode::InvalidWavePartition && result.completedTaskCount == 0;

		std::cout << "[invalid-wave-injection] " << (passed ? "PASS" : "FAIL") << " detected=" << GetFailureCodeName(result.failureCode)
				  << '\n';
		if (!passed)
		{
			PrintResult(result);
		}
		return passed;
	}

	bool RunSingleValidationCase(
		const std::string_view label,
		const STestConfig& config,
		const bool printDetails)
	{
		const std::vector<std::uint64_t> referenceStates = BuildReferenceSectorStates(config);
		const SRunResult result = RunScenario(config, config.workerCount, label);
		const bool passed = ValidateRunResult(config, result, &referenceStates);
		if (printDetails || !passed)
		{
			PrintResult(result);
		}
		else
		{
			std::cout << '[' << label << "] PASS"
					  << " grid=" << config.gridSize << " workers=" << config.workerCount << " batch=" << config.pumpBatchSize
					  << " mode=" << GetWorkloadModeName(config.workloadMode) << " seed=" << config.seed
					  << " ticks=" << result.completedTickCount << " tasks=" << result.completedTaskCount
					  << " maxParallel=" << result.maxParallelTasks << '\n';
		}
		return passed;
	}

	STestConfig MakeQuickCase(
		const STestConfig& base,
		const std::uint32_t gridSize,
		const std::uint32_t tickCount,
		const std::uint32_t workerCount,
		const std::uint32_t batchSize,
		const std::uint32_t workIterations,
		const EWorkloadMode workloadMode,
		const std::uint64_t seed)
	{
		STestConfig config = base;
		config.gridSize = gridSize;
		config.tickCount = tickCount;
		config.workerCount = workerCount;
		config.pumpBatchSize = batchSize;
		config.workIterations = workIterations;
		config.workloadMode = workloadMode;
		config.seed = seed;
		config.durationSeconds = 0;
		config.timeoutSeconds = 120;
		config.progressIntervalSeconds = 60;
		config.injectInvalidWavePartition = false;
		config.enableParallelStartProbe = false;
		config.requireParallelObservation = false;
		config.timingEnabled = false;
		return config;
	}

	bool RunQuickSuite(
		const STestConfig& baseConfig)
	{
		const std::uint32_t hardwareThreadCount = std::max<std::uint32_t>(1u, std::thread::hardware_concurrency());
		const std::uint32_t worker2 = std::min<std::uint32_t>(2u, hardwareThreadCount);
		const std::uint32_t worker3 = std::min<std::uint32_t>(3u, hardwareThreadCount);
		const std::uint32_t worker4 = std::min<std::uint32_t>(4u, hardwareThreadCount);
		const std::uint32_t worker8 = std::min<std::uint32_t>(8u, hardwareThreadCount);

		struct SQuickCase
		{
			std::string label;
			STestConfig config;
		};

		std::vector<SQuickCase> cases;
		cases.push_back({"grid2-w1-b1-uniform", MakeQuickCase(baseConfig, 2, 200, 1, 1, 64, EWorkloadMode::Uniform, 1)});
		cases.push_back({"grid2-w4-b64-uniform", MakeQuickCase(baseConfig, 2, 200, worker4, 64, 64, EWorkloadMode::Uniform, 0xC0FFEE)});
		cases.push_back({"grid3-w2-b1-random", MakeQuickCase(baseConfig, 3, 300, worker2, 1, 128, EWorkloadMode::Random, 0xDEADBEEF)});
		cases.push_back({"grid5-w3-b4-random", MakeQuickCase(baseConfig, 5, 300, worker3, 4, 128, EWorkloadMode::Random, 1)});
		STestConfig parallelProbeCase = MakeQuickCase(baseConfig, 16, 120, worker4, 1, 512, EWorkloadMode::Uniform, 0xC0FFEE);
		parallelProbeCase.enableParallelStartProbe = true;
		parallelProbeCase.requireParallelObservation = true;
		cases.push_back({"grid16-w4-b1-parallel-probe", std::move(parallelProbeCase)});
		cases.push_back({"grid17-w4-b64-random", MakeQuickCase(baseConfig, 17, 120, worker4, 64, 256, EWorkloadMode::Random, 0xDEADBEEF)});
		cases.push_back({"grid31-w8-b4-hot", MakeQuickCase(baseConfig, 31, 40, worker8, 4, 64, EWorkloadMode::Hot, 1)});
		cases.push_back({"grid31-w4-b4-hot-wave", MakeQuickCase(baseConfig, 31, 40, worker4, 4, 64, EWorkloadMode::HotWave, 0xC0FFEE)});
		cases.push_back(
			{"grid128-w8-b1024-random", MakeQuickCase(baseConfig, 128, 4, worker8, 1024, 16, EWorkloadMode::Random, 0xDEADBEEF)});
		cases.push_back({"grid5-w4-b4-reuse", MakeQuickCase(baseConfig, 5, 10'000, worker4, 4, 1, EWorkloadMode::Random, 0xC0FFEE)});

		std::uint32_t passedCaseCount = 0;
		for (const SQuickCase& testCase : cases)
		{
			if (RunSingleValidationCase(testCase.label, testCase.config, false))
			{
				++passedCaseCount;
			}
		}

		STestConfig repeatConfig = MakeQuickCase(baseConfig, 16, 80, worker4, 4, 64, EWorkloadMode::Random, 0xC0FFEE);
		const std::vector<std::uint64_t> repeatReference = BuildReferenceSectorStates(repeatConfig);
		bool repeatPassed = true;
		std::uint64_t repeatChecksum = 0;
		for (std::uint32_t repeatIndex = 0; repeatIndex < 5; ++repeatIndex)
		{
			const std::string label = "repeat-random-" + std::to_string(repeatIndex + 1u);
			const SRunResult result = RunScenario(repeatConfig, repeatConfig.workerCount, label);
			const bool currentPassed = ValidateRunResult(repeatConfig, result, &repeatReference);
			if (repeatIndex == 0)
			{
				repeatChecksum = result.checksum;
			}
			else if (result.checksum != repeatChecksum)
			{
				std::cerr << "[FAIL:" << label << "] 반복 실행 체크섬이 달라졌습니다.\n";
				repeatPassed = false;
			}
			repeatPassed = repeatPassed && currentPassed;
		}
		std::cout << "[repeat-random-5x] " << (repeatPassed ? "PASS" : "FAIL") << '\n';

		const bool injectionPassed = RunInvalidWaveInjectionSelfTest(worker4);
		const bool passed = passedCaseCount == static_cast<std::uint32_t>(cases.size()) && repeatPassed && injectionPassed;
		std::cout << "\n[quick-suite] " << (passed ? "PASS" : "FAIL") << " matrix=" << passedCaseCount << '/' << cases.size()
				  << " repeat=" << (repeatPassed ? "5/5" : "FAIL") << " injection=" << (injectionPassed ? "1/1" : "FAIL") << '\n';
		return passed;
	}

#if defined(NETWORKLIB_LOCKFREE_QUEUE_TEST_HOOKS)
	struct SQueueAbaPauseHook final
	{
		void BeforeHeadCompareExchange(
			const std::uint64_t observedHead,
			std::uint64_t) noexcept
		{
			bool expected = false;
			if (!pauseEntered.compare_exchange_strong(expected, true, std::memory_order_acq_rel, std::memory_order_acquire))
			{
				return;
			}

			capturedHead.store(observedHead, std::memory_order_release);
			if (SetEvent(snapshotReadyEvent) == 0 || WaitForSingleObject(resumeEvent, 5'000) != WAIT_OBJECT_0)
			{
				waitFailed.store(true, std::memory_order_release);
			}
		}

		void AfterHeadCompareExchange(
			const bool succeeded) noexcept
		{
			bool expected = false;
			if (firstCompareExchangeRecorded.compare_exchange_strong(expected, true, std::memory_order_acq_rel, std::memory_order_acquire))
			{
				firstCompareExchangeSucceeded.store(succeeded, std::memory_order_release);
			}
		}

		HANDLE snapshotReadyEvent = nullptr;
		HANDLE resumeEvent = nullptr;
		std::atomic<std::uint64_t> capturedHead = 0;
		std::atomic<bool> pauseEntered = false;
		std::atomic<bool> firstCompareExchangeRecorded = false;
		std::atomic<bool> firstCompareExchangeSucceeded = false;
		std::atomic<bool> waitFailed = false;
	};

	struct SQueueAbaCaseResult final
	{
		bool setupCompleted = false;
		bool mutatorSequenceValid = false;
		bool taggedHeadRestored = false;
		bool headAddressRestored = false;
		bool victimDequeued = false;
		bool firstCompareExchangeSucceeded = false;
		bool queueEmptyAfterVictim = false;
		bool waitFailed = false;
		std::uint64_t victimValue = 0;
		std::uint64_t finalProducedValue = 0;
	};

	SQueueAbaCaseResult RunQueueAbaCase(
		const std::uint32_t rotationCount)
	{
		SQueueAbaCaseResult result{};
		NetworkLib::Containers::FLockFreeQueue<std::uint64_t> queue;
		queue.Enqueue(1);

		SQueueAbaPauseHook hook{};
		hook.snapshotReadyEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		hook.resumeEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		if (hook.snapshotReadyEvent == nullptr || hook.resumeEvent == nullptr)
		{
			if (hook.snapshotReadyEvent != nullptr)
			{
				CloseHandle(hook.snapshotReadyEvent);
			}
			if (hook.resumeEvent != nullptr)
			{
				CloseHandle(hook.resumeEvent);
			}
			return result;
		}

		std::uint64_t victimValue = 0;
		bool victimDequeued = false;
		std::thread victimThread(
			[&]()
			{
				victimDequeued = NetworkLib::Containers::FLockFreeQueueTestAccess::DequeueWithHook(queue, victimValue, hook);
			});

		if (WaitForSingleObject(hook.snapshotReadyEvent, 5'000) != WAIT_OBJECT_0)
		{
			hook.waitFailed.store(true, std::memory_order_release);
			SetEvent(hook.resumeEvent);
			victimThread.join();
			CloseHandle(hook.snapshotReadyEvent);
			CloseHandle(hook.resumeEvent);
			return result;
		}

		result.setupCompleted = true;
		result.mutatorSequenceValid = true;
		for (std::uint32_t rotationIndex = 0; rotationIndex < rotationCount; ++rotationIndex)
		{
			std::uint64_t dequeuedValue = 0;
			if (!queue.Dequeue(dequeuedValue) || dequeuedValue != static_cast<std::uint64_t>(rotationIndex) + 1u)
			{
				result.mutatorSequenceValid = false;
				break;
			}

			queue.Enqueue(static_cast<std::uint64_t>(rotationIndex) + 2u);
		}

		const std::uint64_t capturedHead = hook.capturedHead.load(std::memory_order_acquire);
		const std::uint64_t currentHead = NetworkLib::Containers::FLockFreeQueueTestAccess::LoadHead(queue);
		result.taggedHeadRestored = currentHead == capturedHead;
		result.headAddressRestored =
			NetworkLib::Containers::GetPointer<void>(currentHead) == NetworkLib::Containers::GetPointer<void>(capturedHead);
		result.finalProducedValue = static_cast<std::uint64_t>(rotationCount) + 1u;

		SetEvent(hook.resumeEvent);
		victimThread.join();
		result.victimDequeued = victimDequeued;
		result.victimValue = victimValue;
		result.firstCompareExchangeSucceeded = hook.firstCompareExchangeSucceeded.load(std::memory_order_acquire);
		result.waitFailed = hook.waitFailed.load(std::memory_order_acquire);

		std::uint64_t leftoverValue = 0;
		result.queueEmptyAfterVictim = !queue.Dequeue(leftoverValue);
		CloseHandle(hook.snapshotReadyEvent);
		CloseHandle(hook.resumeEvent);
		return result;
	}

	bool RunLockFreeQueueAbaValidation()
	{
		constexpr std::uint32_t controlRotations = (1u << NetworkLib::Containers::kTagBitCount) - 2u;
		constexpr std::uint32_t wrapRotations = 1u << NetworkLib::Containers::kTagBitCount;
		const SQueueAbaCaseResult control = RunQueueAbaCase(controlRotations);
		const SQueueAbaCaseResult wrap = RunQueueAbaCase(wrapRotations);

		const bool controlPassed = control.setupCompleted && control.mutatorSequenceValid && control.headAddressRestored &&
								   !control.taggedHeadRestored && control.victimDequeued && !control.firstCompareExchangeSucceeded &&
								   control.victimValue == control.finalProducedValue && control.queueEmptyAfterVictim &&
								   !control.waitFailed;
		const bool wrapPassed = wrap.setupCompleted && wrap.mutatorSequenceValid && wrap.victimDequeued &&
								wrap.victimValue == wrap.finalProducedValue && wrap.queueEmptyAfterVictim && !wrap.waitFailed;

		std::cout << "[queue-aba-control] " << (controlPassed ? "PASS" : "FAIL") << " rotations=" << controlRotations
				  << " addressRestored=" << control.headAddressRestored << " taggedRestored=" << control.taggedHeadRestored
				  << " firstCas=" << control.firstCompareExchangeSucceeded << " victim=" << control.victimValue
				  << " expected=" << control.finalProducedValue << '\n'
				  << "[queue-aba-wrap] " << (wrapPassed ? "PASS" : "FAIL") << " rotations=" << wrapRotations
				  << " addressRestored=" << wrap.headAddressRestored << " taggedRestored=" << wrap.taggedHeadRestored
				  << " firstCas=" << wrap.firstCompareExchangeSucceeded << " victim=" << wrap.victimValue
				  << " expected=" << wrap.finalProducedValue << '\n';

		if (!wrapPassed && wrap.taggedHeadRestored && wrap.firstCompareExchangeSucceeded && wrap.victimValue == 1)
		{
			std::cout << "[queue-aba] ABA_REPRODUCED duplicate=1 missing=" << wrap.finalProducedValue << '\n';
		}

		return controlPassed && wrapPassed;
	}
#endif

	bool RunSoakSuite(
		STestConfig config)
	{
		STestConfig preflightConfig = config;
		preflightConfig.durationSeconds = 0;
		preflightConfig.tickCount = 50;
		preflightConfig.timeoutSeconds = 120;
		preflightConfig.timingEnabled = false;
		preflightConfig.enableParallelStartProbe = true;
		preflightConfig.requireParallelObservation = true;

		std::cout << "[soak] correctness preflight\n";
		if (!RunSingleValidationCase("soak-preflight", preflightConfig, true))
		{
			return false;
		}
		if (!RunInvalidWaveInjectionSelfTest(config.workerCount))
		{
			return false;
		}

		config.enableParallelStartProbe = false;
		config.requireParallelObservation = true;
		if (config.referenceCheckpointIntervalTicks == 0)
		{
			config.referenceCheckpointIntervalTicks = 4'096;
		}
		if (config.referenceCheckpointWindowTicks == 0)
		{
			config.referenceCheckpointWindowTicks = 4;
		}
		std::cout << "[soak] start duration=" << config.durationSeconds << " sec mode=" << GetWorkloadModeName(config.workloadMode)
				  << " seed=" << config.seed << " referenceEvery=" << config.referenceCheckpointIntervalTicks
				  << " window=" << config.referenceCheckpointWindowTicks << " ticks\n";
		const SRunResult result = RunScenario(config, config.workerCount, "soak");
		PrintResult(result);
		return ValidateRunResult(config, result, nullptr);
	}

	bool ParseUnsigned(
		const std::string_view text,
		std::uint64_t& outValue)
	{
		int base = 10;
		std::string_view digits = text;
		if (digits.size() > 2 && digits[0] == '0' && (digits[1] == 'x' || digits[1] == 'X'))
		{
			base = 16;
			digits.remove_prefix(2);
		}

		if (digits.empty())
		{
			return false;
		}

		const char* begin = digits.data();
		const char* end = begin + digits.size();
		const auto [next, error] = std::from_chars(begin, end, outValue, base);
		return error == std::errc{} && next == end;
	}

	bool ParsePositive32(
		const std::string_view text,
		std::uint32_t& outValue)
	{
		std::uint64_t parsedValue = 0;
		if (!ParseUnsigned(text, parsedValue) || parsedValue == 0 || parsedValue > std::numeric_limits<std::uint32_t>::max())
		{
			return false;
		}

		outValue = static_cast<std::uint32_t>(parsedValue);
		return true;
	}

	bool ParseWorkloadMode(
		const std::string_view text,
		EWorkloadMode& outMode)
	{
		if (text == "uniform")
		{
			outMode = EWorkloadMode::Uniform;
			return true;
		}
		if (text == "random")
		{
			outMode = EWorkloadMode::Random;
			return true;
		}
		if (text == "hot")
		{
			outMode = EWorkloadMode::Hot;
			return true;
		}
		if (text == "hot-wave")
		{
			outMode = EWorkloadMode::HotWave;
			return true;
		}
		return false;
	}

	bool ParseSuite(
		const std::string_view text,
		ETestSuite& outSuite)
	{
		if (text == "single")
		{
			outSuite = ETestSuite::Single;
			return true;
		}
		if (text == "quick")
		{
			outSuite = ETestSuite::Quick;
			return true;
		}
		if (text == "soak")
		{
			outSuite = ETestSuite::Soak;
			return true;
		}
		if (text == "queue-aba")
		{
			outSuite = ETestSuite::QueueAba;
			return true;
		}
		return false;
	}

	bool ParseArguments(
		const int argumentCount,
		char* arguments[],
		STestConfig& config,
		bool& outHelpRequested)
	{
		outHelpRequested = false;
		for (int index = 1; index < argumentCount; ++index)
		{
			const std::string_view option = arguments[index];
			if (option == "--help")
			{
				outHelpRequested = true;
				return true;
			}

			if (index + 1 >= argumentCount)
			{
				std::cerr << "옵션 값이 없습니다: " << option << '\n';
				return false;
			}

			const std::string_view value = arguments[++index];
			if (option == "--suite")
			{
				if (!ParseSuite(value, config.suite))
				{
					std::cerr << "알 수 없는 suite입니다: " << value << '\n';
					return false;
				}
				continue;
			}
			if (option == "--mode")
			{
				if (!ParseWorkloadMode(value, config.workloadMode))
				{
					std::cerr << "알 수 없는 workload mode입니다: " << value << '\n';
					return false;
				}
				config.workloadModeSpecified = true;
				continue;
			}
			if (option == "--timing-dir")
			{
				config.timingEnabled = true;
				config.timingOutputDirectory = std::filesystem::path(value);
				continue;
			}
			if (option == "--seed")
			{
				if (!ParseUnsigned(value, config.seed))
				{
					std::cerr << "올바르지 않은 seed입니다: " << value << '\n';
					return false;
				}
				continue;
			}

			std::uint32_t parsedValue = 0;
			if (!ParsePositive32(value, parsedValue))
			{
				std::cerr << "양의 정수가 아닌 옵션 값입니다: " << value << '\n';
				return false;
			}

			if (option == "--grid")
			{
				config.gridSize = parsedValue;
			}
			else if (option == "--ticks")
			{
				config.tickCount = parsedValue;
			}
			else if (option == "--workers")
			{
				config.workerCount = parsedValue;
			}
			else if (option == "--work-iterations")
			{
				config.workIterations = parsedValue;
			}
			else if (option == "--batch")
			{
				config.pumpBatchSize = parsedValue;
			}
			else if (option == "--timeout-sec")
			{
				config.timeoutSeconds = parsedValue;
			}
			else if (option == "--duration-sec")
			{
				config.durationSeconds = parsedValue;
			}
			else if (option == "--progress-sec")
			{
				config.progressIntervalSeconds = parsedValue;
			}
			else if (option == "--hot-percent")
			{
				config.hotSectorPercent = parsedValue;
			}
			else if (option == "--hot-multiplier")
			{
				config.hotWorkMultiplier = parsedValue;
			}
			else
			{
				std::cerr << "알 수 없는 옵션입니다: " << option << '\n';
				return false;
			}
		}

		if (config.gridSize < 2 || config.gridSize > 128 || config.workerCount > 32 || config.tickCount > 1'000'000 ||
			config.workIterations > 1'000'000 || config.pumpBatchSize > 1024 || config.timeoutSeconds > 86'400 ||
			config.durationSeconds > 86'400 || config.progressIntervalSeconds > 3'600 || config.hotSectorPercent > 100 ||
			config.hotWorkMultiplier > 64)
		{
			std::cerr << "옵션 값이 허용 범위를 벗어났습니다. --help를 확인해 주세요.\n";
			return false;
		}

		if (config.durationSeconds > 0)
		{
			config.timeoutSeconds = std::max<std::uint32_t>(config.timeoutSeconds, config.durationSeconds + 120u);
		}
		return true;
	}

	void PrintHelp()
	{
		std::cout << "ContentsTaskGraphSmokeTest options:\n"
				  << "  --suite <single|quick|soak|queue-aba>\n"
				  << "  --mode <uniform|random|hot|hot-wave>\n"
				  << "  --grid <2..128>\n"
				  << "  --ticks <count>\n"
				  << "  --workers <1..32>\n"
				  << "  --work-iterations <count>\n"
				  << "  --batch <count>\n"
				  << "  --seed <decimal|0xhex>\n"
				  << "  --hot-percent <1..100>\n"
				  << "  --hot-multiplier <1..64>\n"
				  << "  --duration-sec <count>\n"
				  << "  --progress-sec <count>\n"
				  << "  --timeout-sec <count>\n"
				  << "  --timing-dir <directory>\n";
	}
}

int main(
	const int argumentCount,
	char* arguments[])
{
	STestConfig config{};
	const std::uint32_t hardwareThreadCount = std::max<std::uint32_t>(1u, std::thread::hardware_concurrency());
	config.workerCount = std::min<std::uint32_t>(4u, hardwareThreadCount);

	bool helpRequested = false;
	if (!ParseArguments(argumentCount, arguments, config, helpRequested))
	{
		return 2;
	}
	if (helpRequested)
	{
		PrintHelp();
		return 0;
	}

	if (config.suite == ETestSuite::Quick)
	{
		return RunQuickSuite(config) ? 0 : 1;
	}

	if (config.suite == ETestSuite::QueueAba)
	{
#if defined(NETWORKLIB_LOCKFREE_QUEUE_TEST_HOOKS)
		return RunLockFreeQueueAbaValidation() ? 0 : 1;
#else
		std::cerr << "Queue ABA 테스트 훅이 빌드에 포함되지 않았습니다.\n";
		return 2;
#endif
	}

	if (config.suite == ETestSuite::Soak)
	{
		if (config.durationSeconds == 0)
		{
			config.durationSeconds = 60;
		}
		if (!config.workloadModeSpecified)
		{
			config.workloadMode = EWorkloadMode::Hot;
		}
		if (!config.timingEnabled)
		{
			const std::filesystem::path executableDirectory = argumentCount > 0 && arguments[0] != nullptr
																  ? std::filesystem::absolute(arguments[0]).parent_path()
																  : std::filesystem::current_path();
			config.timingEnabled = true;
			config.timingOutputDirectory = executableDirectory / "timing";
		}
		config.timeoutSeconds = std::max<std::uint32_t>(config.timeoutSeconds, config.durationSeconds + 120u);
		return RunSoakSuite(config) ? 0 : 1;
	}

	if (config.timingEnabled)
	{
		config.enableParallelStartProbe = false;
		config.requireParallelObservation = false;
	}

	std::cout << "ContentsRuntime Task Graph validation\n"
			  << "  model      : Main Instance -> 4 waves -> FLockFreeQueue -> Shard Instances\n"
			  << "  grid       : " << config.gridSize << " x " << config.gridSize << '\n'
			  << "  ticks      : " << config.tickCount << '\n'
			  << "  workers    : baseline 1, parallel " << config.workerCount << '\n'
			  << "  batch      : " << config.pumpBatchSize << '\n'
			  << "  workload   : " << GetWorkloadModeName(config.workloadMode) << '\n'
			  << "  seed       : " << config.seed << "\n\n";

	const std::vector<std::uint64_t> referenceStates = BuildReferenceSectorStates(config);
	const SRunResult baselineResult = RunScenario(config, 1, "single-worker-reference");
	PrintResult(baselineResult);

	SRunResult parallelResult = baselineResult;
	if (config.workerCount > 1)
	{
		parallelResult = RunScenario(config, config.workerCount, "multi-worker-taskgraph");
		PrintResult(parallelResult);
	}

	bool passed =
		ValidateRunResult(config, baselineResult, &referenceStates) && ValidateRunResult(config, parallelResult, &referenceStates);
	if (baselineResult.sectorStates != parallelResult.sectorStates)
	{
		std::cerr << "[FAIL] 단일/다중 Worker의 전체 Sector 상태가 다릅니다.\n";
		passed = false;
	}
	if (!RunInvalidWaveInjectionSelfTest(config.workerCount))
	{
		passed = false;
	}

	std::cout << "\n[single-suite] " << (passed ? "PASS" : "FAIL") << " reference=verified invalid-wave=verified\n";
	return passed ? 0 : 1;
}
