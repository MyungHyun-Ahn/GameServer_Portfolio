#include "WorldServerPch.h"

#include "WorldServer/Contents/Map/Sector/FTaskGraphSectorExecutionService.h"

#include "WorldCore/Entity/FEntityRegistry.h"
#include "WorldCore/Map/Sector/FSectorGrid.h"
#include "WorldCore/Map/Sector/FSectorTaskProcessor.h"
#include "WorldCore/Map/Sector/FSectorTickPlan.h"

namespace WorldServer::Contents
{
	namespace
	{
		template <typename TValue>
		void UpdateMaximum(
			std::atomic<TValue>& target,
			const TValue candidate) noexcept
		{
			TValue current = target.load(std::memory_order_relaxed);
			while (current < candidate &&
				   !target.compare_exchange_weak(current, candidate, std::memory_order_relaxed, std::memory_order_relaxed))
			{
			}
		}

		struct STaskGraphDispatchEnvironment final
		{
			std::shared_ptr<Foundation::ILogger> logger;
			std::vector<ContentsRuntime::Core::FContentInstanceId> executorInstanceIds;
			std::uint32_t pumpBatchSize = 1;
			std::atomic<ContentsRuntime::Bridge::IContentBridge*> bridge = nullptr;
			std::atomic<std::uint64_t> startedExecutionCount = 0;
			std::atomic<std::uint64_t> completedExecutionCount = 0;
			std::atomic<std::uint64_t> failedExecutionCount = 0;
			std::atomic<std::uint64_t> canceledExecutionCount = 0;
			std::atomic<std::uint64_t> executedTaskCount = 0;
			std::atomic<std::uint32_t> activeTaskCount = 0;
			std::atomic<std::uint32_t> maxParallelTaskCount = 0;
			std::atomic<std::uint64_t> workerMask = 0;
			std::atomic<bool> shuttingDown = false;
			std::mutex fallbackOwnerLock;
			std::unordered_map<ContentsRuntime::Core::FContentInstanceId, std::deque<std::function<void()>>> fallbackOwnerCallbacks;

			bool Enqueue(
				const ContentsRuntime::Core::FContentInstanceId contentInstanceId,
				std::function<void()> callback) const
			{
				ContentsRuntime::Bridge::IContentBridge* const currentBridge = bridge.load(std::memory_order_acquire);
				return currentBridge != nullptr && currentBridge->EnqueueCompletionToInstance(contentInstanceId, std::move(callback));
			}

			bool StoreFallbackOwnerCallback(
				const ContentsRuntime::Core::FContentInstanceId contentInstanceId,
				std::function<void()> callback) noexcept
			{
				if (!callback || shuttingDown.load(std::memory_order_acquire))
				{
					return false;
				}

				try
				{
					std::lock_guard lock(fallbackOwnerLock);
					if (shuttingDown.load(std::memory_order_acquire))
					{
						return false;
					}
					fallbackOwnerCallbacks[contentInstanceId].push_back(std::move(callback));
					return true;
				}
				catch (...)
				{
					return false;
				}
			}

			void DrainFallbackOwnerCallbacks(
				const ContentsRuntime::Core::FContentInstanceId contentInstanceId)
			{
				std::deque<std::function<void()>> callbacks;
				{
					std::lock_guard lock(fallbackOwnerLock);
					const auto found = fallbackOwnerCallbacks.find(contentInstanceId);
					if (found == fallbackOwnerCallbacks.end())
					{
						return;
					}
					callbacks.swap(found->second);
					fallbackOwnerCallbacks.erase(found);
				}

				for (std::function<void()>& callback : callbacks)
				{
					try
					{
						callback();
					}
					catch (const std::exception& exception)
					{
						Log(Foundation::ELogLevel::Error, "TaskGraph fallback Owner callback failed: {}", exception.what());
					}
					catch (...)
					{
						Log(Foundation::ELogLevel::Error, "TaskGraph fallback Owner callback failed with an unknown exception.");
					}
				}
			}

			void ClearFallbackOwnerCallbacks() noexcept
			{
				std::unordered_map<ContentsRuntime::Core::FContentInstanceId, std::deque<std::function<void()>>> callbacks;
				{
					std::lock_guard lock(fallbackOwnerLock);
					callbacks.swap(fallbackOwnerCallbacks);
				}
			}

			void Log(
				const Foundation::ELogLevel level,
				const std::string& message) const
			{
				if (logger != nullptr)
				{
					logger->Log(level, "WorldTaskGraph", message);
				}
			}

			template <typename... TArgs>
				requires(sizeof...(TArgs) > 0)
			void Log(
				const Foundation::ELogLevel level,
				std::format_string<TArgs...> format,
				TArgs&&... args) const
			{
				if (logger != nullptr)
				{
					logger->Log(level, "WorldTaskGraph", format, std::forward<TArgs>(args)...);
				}
			}
		};

		class FTaskGraphExecutionContext final : public std::enable_shared_from_this<FTaskGraphExecutionContext>
		{
		private:
			struct STaskEntry final
			{
				const WorldCore::SSectorTask* task = nullptr;
				std::uint32_t waveIndex = 0;
				std::size_t outputIndex = 0;
			};

		public:
			FTaskGraphExecutionContext(
				std::shared_ptr<STaskGraphDispatchEnvironment> environment,
				const ContentsRuntime::Core::FContentInstanceId ownerContentInstanceId,
				const WorldCore::SMapTickTicket& ticket,
				const WorldCore::FSectorTickPlan& tickPlan,
				const WorldCore::FSectorTaskProcessor& taskProcessor,
				const WorldCore::FEntityRegistry& entityRegistry,
				const WorldCore::FSectorGrid& sectorGrid,
				const WorldCore::SMapDefinition& mapDefinition,
				FTaskGraphSectorExecutionService::FCompletionHandler completionHandler)
				: m_environment(std::move(environment))
				, m_ownerContentInstanceId(ownerContentInstanceId)
				, m_ticket(ticket)
				, m_tickPlan(tickPlan)
				, m_taskProcessor(taskProcessor)
				, m_entityRegistry(entityRegistry)
				, m_sectorGrid(sectorGrid)
				, m_mapDefinition(mapDefinition)
				, m_completionHandler(std::move(completionHandler))
			{
				BuildTaskEntries();
			}

			~FTaskGraphExecutionContext()
			{
				if (m_started.load(std::memory_order_acquire) && !m_finalCompletionPosted.load(std::memory_order_acquire))
				{
					m_environment->canceledExecutionCount.fetch_add(1, std::memory_order_relaxed);
				}
			}

			[[nodiscard]] bool IsValid() const noexcept
			{
				return m_validationError.empty();
			}

			[[nodiscard]] const std::string& GetValidationError() const noexcept
			{
				return m_validationError;
			}

			void Start()
			{
				m_started.store(true, std::memory_order_release);
				m_environment->startedExecutionCount.fetch_add(1, std::memory_order_relaxed);
				BeginWaveOnOwner(0);
			}

		private:
			void BuildTaskEntries()
			{
				if (!ContentsRuntime::Core::IsValidContentInstanceId(m_ownerContentInstanceId) || !m_completionHandler)
				{
					m_validationError = "TaskGraph execution has an invalid owner or completion handler.";
					return;
				}

				const std::size_t taskCount = m_tickPlan.GetTaskCount();
				m_outputs.resize(taskCount);
				m_taskEntries = std::make_unique<STaskEntry[]>(taskCount);
				std::vector<bool> outputSlots(taskCount, false);
				std::size_t entryIndex = 0;
				for (const WorldCore::SSectorTaskWave& wave : m_tickPlan.GetWaves())
				{
					if (wave.waveIndex >= WorldCore::kSectorTaskWaveCount)
					{
						m_validationError = "TaskGraph Tick Plan contains an invalid Wave index.";
						return;
					}

					for (const WorldCore::SSectorTask& task : wave.tasks)
					{
						if (entryIndex >= taskCount || task.stableOrder >= taskCount || outputSlots[task.stableOrder])
						{
							m_validationError = "TaskGraph Tick Plan contains a duplicate or invalid output slot.";
							return;
						}

						STaskEntry& entry = m_taskEntries[entryIndex++];
						entry.task = &task;
						entry.waveIndex = wave.waveIndex;
						entry.outputIndex = task.stableOrder;
						m_waveTasks[wave.waveIndex].push_back(&entry);
						outputSlots[task.stableOrder] = true;
					}
				}

				if (entryIndex != taskCount || std::find(outputSlots.begin(), outputSlots.end(), false) != outputSlots.end())
				{
					m_validationError = "TaskGraph Tick Plan does not provide one stable output slot per Sector task.";
				}
			}

			void BeginWaveOnOwner(
				const std::uint32_t waveIndex)
			{
				if (m_finalCompletionPosted.load(std::memory_order_acquire))
				{
					return;
				}

				if (waveIndex >= WorldCore::kSectorTaskWaveCount)
				{
					MarkFailure("TaskGraph tried to start an invalid Wave.");
					PublishFinalCompletionOnOwner();
					return;
				}

				m_currentWave.store(waveIndex, std::memory_order_release);
				m_wavePublicationComplete.store(false, std::memory_order_release);
				m_waveCompletionPosted.store(false, std::memory_order_release);
				m_abortDrainStarted.store(false, std::memory_order_release);
				m_remainingTaskCount.store(static_cast<std::uint32_t>(m_waveTasks[waveIndex].size()), std::memory_order_release);
				m_queuedTaskCount.store(0, std::memory_order_release);
				m_inFlightTaskCount.store(0, std::memory_order_release);
				m_outstandingPumpCallbackCount.store(0, std::memory_order_release);

				for (STaskEntry* const taskEntry : m_waveTasks[waveIndex])
				{
					m_taskQueue.Enqueue(taskEntry);
					m_queuedTaskCount.fetch_add(1, std::memory_order_relaxed);
				}

				const std::size_t pumpCount = std::min(m_waveTasks[waveIndex].size(), m_environment->executorInstanceIds.size());
				for (std::size_t index = 0; index < pumpCount; ++index)
				{
					(void)SchedulePump(m_environment->executorInstanceIds[index], waveIndex);
				}

				m_wavePublicationComplete.store(true, std::memory_order_release);
				if (m_failureDetected.load(std::memory_order_acquire) &&
					m_outstandingPumpCallbackCount.load(std::memory_order_acquire) == 0)
				{
					DrainAbortedTasks();
				}
				TryPostWaveCompletion(true);
			}

			bool SchedulePump(
				const ContentsRuntime::Core::FContentInstanceId executorInstanceId,
				const std::uint32_t waveIndex)
			{
				m_outstandingPumpCallbackCount.fetch_add(1, std::memory_order_acq_rel);
				const std::shared_ptr<FTaskGraphExecutionContext> self = shared_from_this();
				bool enqueued = false;
				try
				{
					enqueued = m_environment->Enqueue(executorInstanceId,
						[self, executorInstanceId, waveIndex]()
						{
							try
							{
								self->PumpTasks(executorInstanceId, waveIndex);
							}
							catch (const std::exception& exception)
							{
								self->MarkFailure(std::format("TaskGraph Pump raised an exception: {}", exception.what()));
							}
							catch (...)
							{
								self->MarkFailure("TaskGraph Pump raised an unknown exception.");
							}
							self->OnPumpCallbackExited(waveIndex);
						});
				}
				catch (const std::exception& exception)
				{
					MarkFailure(std::format("TaskGraph Pump enqueue raised an exception: {}", exception.what()));
				}
				catch (...)
				{
					MarkFailure("TaskGraph Pump enqueue raised an unknown exception.");
				}
				if (!enqueued)
				{
					m_outstandingPumpCallbackCount.fetch_sub(1, std::memory_order_acq_rel);
					MarkFailure(std::format("TaskGraph Pump enqueue failed. executorContentInstanceId={}", executorInstanceId));
				}
				return enqueued;
			}

			void PumpTasks(
				const ContentsRuntime::Core::FContentInstanceId executorInstanceId,
				const std::uint32_t waveIndex)
			{
				if (waveIndex != m_currentWave.load(std::memory_order_acquire) || m_finalCompletionPosted.load(std::memory_order_acquire))
				{
					MarkFailure("TaskGraph Pump observed a stale Wave callback.");
					return;
				}

				std::uint32_t processedCount = 0;
				while (processedCount < m_environment->pumpBatchSize)
				{
					STaskEntry* taskEntry = nullptr;
					if (!m_taskQueue.Dequeue(taskEntry))
					{
						break;
					}

					const std::uint32_t previousQueued = m_queuedTaskCount.fetch_sub(1, std::memory_order_acq_rel);
					if (previousQueued == 0)
					{
						m_queuedTaskCount.store(0, std::memory_order_release);
						MarkFailure("TaskGraph queued task counter underflowed.");
					}

					m_inFlightTaskCount.fetch_add(1, std::memory_order_acq_rel);
					try
					{
						ExecuteTask(taskEntry, waveIndex);
					}
					catch (const std::exception& exception)
					{
						MarkFailure(std::format("Sector task completion raised an exception: {}", exception.what()));
					}
					catch (...)
					{
						MarkFailure("Sector task completion raised an unknown exception.");
					}
					m_inFlightTaskCount.fetch_sub(1, std::memory_order_acq_rel);

					const std::uint32_t previousRemaining = m_remainingTaskCount.fetch_sub(1, std::memory_order_acq_rel);
					if (previousRemaining == 0)
					{
						m_remainingTaskCount.store(0, std::memory_order_release);
						MarkFailure("TaskGraph remaining task counter underflowed.");
						break;
					}
					++processedCount;
				}

				if (processedCount == m_environment->pumpBatchSize && m_queuedTaskCount.load(std::memory_order_acquire) > 0 &&
					m_remainingTaskCount.load(std::memory_order_acquire) > 0)
				{
					(void)SchedulePump(executorInstanceId, waveIndex);
				}
			}

			void ExecuteTask(
				STaskEntry* const taskEntry,
				const std::uint32_t waveIndex)
			{
				if (taskEntry == nullptr || taskEntry->task == nullptr || taskEntry->waveIndex != waveIndex ||
					taskEntry->outputIndex >= m_outputs.size())
				{
					MarkFailure("TaskGraph dequeued an invalid Sector task entry.");
					return;
				}
				if (m_failureDetected.load(std::memory_order_acquire))
				{
					return;
				}

				const std::uint32_t activeTaskCount = m_environment->activeTaskCount.fetch_add(1, std::memory_order_acq_rel) + 1;
				UpdateMaximum(m_environment->maxParallelTaskCount, activeTaskCount);
				const std::uint32_t workerIndex = ContentsRuntime::Threading::FContentThread::GetCurrentWorkerIndex();
				if (workerIndex < 64)
				{
					m_environment->workerMask.fetch_or(1ull << workerIndex, std::memory_order_relaxed);
				}

				WorldCore::SSectorTaskOutput output;
				std::string taskError;
				bool succeeded = false;
				try
				{
					succeeded =
						m_taskProcessor.Execute(*taskEntry->task, m_entityRegistry, m_sectorGrid, m_mapDefinition, output, taskError);
				}
				catch (const std::exception& exception)
				{
					taskError = std::format("Sector task raised an exception: {}", exception.what());
				}
				catch (...)
				{
					taskError = "Sector task raised an unknown exception.";
				}

				m_environment->activeTaskCount.fetch_sub(1, std::memory_order_acq_rel);
				m_environment->executedTaskCount.fetch_add(1, std::memory_order_relaxed);
				if (!succeeded)
				{
					MarkFailure(taskError.empty() ? "Sector task failed without a reason." : std::move(taskError));
					return;
				}

				try
				{
					m_outputs[taskEntry->outputIndex] = std::move(output);
				}
				catch (const std::exception& exception)
				{
					MarkFailure(std::format("Sector task output storage failed: {}", exception.what()));
				}
				catch (...)
				{
					MarkFailure("Sector task output storage failed with an unknown exception.");
				}
			}

			void OnPumpCallbackExited(
				const std::uint32_t waveIndex)
			{
				const std::uint32_t previousOutstanding = m_outstandingPumpCallbackCount.fetch_sub(1, std::memory_order_acq_rel);
				if (previousOutstanding == 0)
				{
					m_outstandingPumpCallbackCount.fetch_add(1, std::memory_order_relaxed);
					MarkFailure("TaskGraph outstanding Pump counter underflowed.");
					return;
				}

				if (previousOutstanding == 1 && m_failureDetected.load(std::memory_order_acquire) &&
					m_queuedTaskCount.load(std::memory_order_acquire) > 0)
				{
					DrainAbortedTasks();
				}
				if (waveIndex == m_currentWave.load(std::memory_order_acquire))
				{
					TryPostWaveCompletion(false);
				}
			}

			void DrainAbortedTasks()
			{
				if (!m_failureDetected.load(std::memory_order_acquire) ||
					m_outstandingPumpCallbackCount.load(std::memory_order_acquire) != 0 ||
					m_abortDrainStarted.exchange(true, std::memory_order_acq_rel))
				{
					return;
				}

				STaskEntry* taskEntry = nullptr;
				while (m_taskQueue.Dequeue(taskEntry))
				{
					(void)taskEntry;
					const std::uint32_t previousQueued = m_queuedTaskCount.fetch_sub(1, std::memory_order_acq_rel);
					const std::uint32_t previousRemaining = m_remainingTaskCount.fetch_sub(1, std::memory_order_acq_rel);
					if (previousQueued == 0 || previousRemaining == 0)
					{
						m_queuedTaskCount.store(0, std::memory_order_release);
						m_remainingTaskCount.store(0, std::memory_order_release);
						MarkFailure("TaskGraph abort drain counter underflowed.");
					}
				}
			}

			void TryPostWaveCompletion(
				const bool callerIsOwner)
			{
				if (!m_wavePublicationComplete.load(std::memory_order_acquire) ||
					m_remainingTaskCount.load(std::memory_order_acquire) != 0 || m_queuedTaskCount.load(std::memory_order_acquire) != 0 ||
					m_inFlightTaskCount.load(std::memory_order_acquire) != 0 ||
					m_outstandingPumpCallbackCount.load(std::memory_order_acquire) != 0)
				{
					return;
				}

				bool expected = false;
				if (!m_waveCompletionPosted.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
				{
					return;
				}

				const std::uint32_t waveIndex = m_currentWave.load(std::memory_order_acquire);
				const std::shared_ptr<FTaskGraphExecutionContext> self = shared_from_this();
				std::function<void()> ownerCallback = [self, waveIndex]()
				{
					self->CompleteWaveOnOwner(waveIndex);
				};
				bool enqueued = false;
				try
				{
					enqueued = m_environment->Enqueue(m_ownerContentInstanceId, ownerCallback);
				}
				catch (...)
				{
					enqueued = false;
				}
				if (enqueued)
				{
					return;
				}

				if (callerIsOwner)
				{
					CompleteWaveOnOwner(waveIndex);
					return;
				}

				const bool shuttingDown = m_environment->shuttingDown.load(std::memory_order_acquire);
				if (!shuttingDown && m_environment->StoreFallbackOwnerCallback(m_ownerContentInstanceId, std::move(ownerCallback)))
				{
					m_environment->Log(Foundation::ELogLevel::Warn,
						"TaskGraph owner completion enqueue failed; the Map Owner will drain the fallback callback. "
						"mapInstanceId={} tickIndex={} generation={}",
						m_ticket.mapInstanceId,
						m_ticket.tickIndex,
						m_ticket.generation);
					return;
				}

				CancelWithoutOwnerCompletion();
				if (shuttingDown)
				{
					m_environment->Log(Foundation::ELogLevel::Warn,
						"TaskGraph owner completion was canceled while the runtime was stopping. mapInstanceId={} tickIndex={} "
						"generation={}",
						m_ticket.mapInstanceId,
						m_ticket.tickIndex,
						m_ticket.generation);
				}
				else
				{
					m_environment->Log(Foundation::ELogLevel::Error,
						"TaskGraph owner completion and fallback registration both failed. mapInstanceId={} tickIndex={} generation={}",
						m_ticket.mapInstanceId,
						m_ticket.tickIndex,
						m_ticket.generation);
				}
			}

			void CancelWithoutOwnerCompletion() noexcept
			{
				bool expected = false;
				if (m_finalCompletionPosted.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
				{
					m_environment->canceledExecutionCount.fetch_add(1, std::memory_order_relaxed);
				}
			}

			void CompleteWaveOnOwner(
				const std::uint32_t waveIndex)
			{
				if (m_finalCompletionPosted.load(std::memory_order_acquire))
				{
					return;
				}
				if (waveIndex != m_currentWave.load(std::memory_order_acquire) ||
					m_remainingTaskCount.load(std::memory_order_acquire) != 0 || m_queuedTaskCount.load(std::memory_order_acquire) != 0 ||
					m_inFlightTaskCount.load(std::memory_order_acquire) != 0 ||
					m_outstandingPumpCallbackCount.load(std::memory_order_acquire) != 0)
				{
					MarkFailure("TaskGraph owner observed an incomplete Wave barrier.");
					PublishFinalCompletionOnOwner();
					return;
				}

				if (m_failureDetected.load(std::memory_order_acquire))
				{
					PublishFinalCompletionOnOwner();
					return;
				}
				if (waveIndex + 1 < WorldCore::kSectorTaskWaveCount)
				{
					BeginWaveOnOwner(waveIndex + 1);
					return;
				}

				PublishFinalCompletionOnOwner();
			}

			void PublishFinalCompletionOnOwner()
			{
				bool expected = false;
				if (!m_finalCompletionPosted.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
				{
					return;
				}

				WorldCore::SMapTickExecutionCompletion completion{};
				completion.ticket = m_ticket;
				if (m_environment->shuttingDown.load(std::memory_order_acquire))
				{
					m_environment->canceledExecutionCount.fetch_add(1, std::memory_order_relaxed);
					return;
				}
				if (m_failureDetected.load(std::memory_order_acquire))
				{
					completion.status = WorldCore::EMapTickCompletionStatus::Failed;
					{
						std::lock_guard lock(m_failureLock);
						completion.failureReason =
							m_failureReason.empty() ? "TaskGraph execution failed without a reason." : m_failureReason;
					}
					m_environment->failedExecutionCount.fetch_add(1, std::memory_order_relaxed);
				}
				else
				{
					completion.status = WorldCore::EMapTickCompletionStatus::Succeeded;
					completion.taskOutputs = std::move(m_outputs);
					m_environment->completedExecutionCount.fetch_add(1, std::memory_order_relaxed);
				}

				try
				{
					m_completionHandler(std::move(completion));
				}
				catch (const std::exception& exception)
				{
					m_environment->Log(Foundation::ELogLevel::Error,
						"TaskGraph owner completion handler raised an exception. mapInstanceId={} error={}",
						m_ticket.mapInstanceId,
						exception.what());
				}
				catch (...)
				{
					m_environment->Log(Foundation::ELogLevel::Error,
						"TaskGraph owner completion handler raised an unknown exception. mapInstanceId={}",
						m_ticket.mapInstanceId);
				}
			}

			void MarkFailure(
				std::string reason)
			{
				bool expected = false;
				if (!m_failureDetected.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
				{
					return;
				}

				std::lock_guard lock(m_failureLock);
				m_failureReason = reason.empty() ? "TaskGraph execution failed without a reason." : std::move(reason);
			}

		private:
			std::shared_ptr<STaskGraphDispatchEnvironment> m_environment;
			ContentsRuntime::Core::FContentInstanceId m_ownerContentInstanceId = ContentsRuntime::Core::kInvalidContentInstanceId;
			WorldCore::SMapTickTicket m_ticket{};
			const WorldCore::FSectorTickPlan& m_tickPlan;
			const WorldCore::FSectorTaskProcessor& m_taskProcessor;
			const WorldCore::FEntityRegistry& m_entityRegistry;
			const WorldCore::FSectorGrid& m_sectorGrid;
			WorldCore::SMapDefinition m_mapDefinition{};
			FTaskGraphSectorExecutionService::FCompletionHandler m_completionHandler;
			std::unique_ptr<STaskEntry[]> m_taskEntries;
			std::array<std::vector<STaskEntry*>, WorldCore::kSectorTaskWaveCount> m_waveTasks;
			std::vector<WorldCore::SSectorTaskOutput> m_outputs;
			NetworkLib::Containers::FLockFreeQueue<STaskEntry*> m_taskQueue;
			std::string m_validationError;
			std::atomic<std::uint32_t> m_currentWave = 0;
			std::atomic<std::uint32_t> m_remainingTaskCount = 0;
			std::atomic<std::uint32_t> m_queuedTaskCount = 0;
			std::atomic<std::uint32_t> m_inFlightTaskCount = 0;
			std::atomic<std::uint32_t> m_outstandingPumpCallbackCount = 0;
			std::atomic<bool> m_wavePublicationComplete = false;
			std::atomic<bool> m_waveCompletionPosted = false;
			std::atomic<bool> m_abortDrainStarted = false;
			std::atomic<bool> m_failureDetected = false;
			std::atomic<bool> m_finalCompletionPosted = false;
			std::atomic<bool> m_started = false;
			std::mutex m_failureLock;
			std::string m_failureReason;
		};
	}

	struct FTaskGraphSectorExecutionService::SImpl final
	{
		std::shared_ptr<STaskGraphDispatchEnvironment> environment;
	};

	FTaskGraphSectorExecutionService::FTaskGraphSectorExecutionService(
		std::shared_ptr<Foundation::ILogger> logger,
		std::vector<ContentsRuntime::Core::FContentInstanceId> executorInstanceIds,
		const std::uint32_t pumpBatchSize)
		: m_impl(std::make_unique<SImpl>())
	{
		m_impl->environment = std::make_shared<STaskGraphDispatchEnvironment>();
		m_impl->environment->logger = std::move(logger);
		m_impl->environment->executorInstanceIds = std::move(executorInstanceIds);
		m_impl->environment->pumpBatchSize = std::max<std::uint32_t>(1u, pumpBatchSize);
	}

	FTaskGraphSectorExecutionService::~FTaskGraphSectorExecutionService() = default;

	void FTaskGraphSectorExecutionService::BindBridge(
		ContentsRuntime::Bridge::IContentBridge& bridge) noexcept
	{
		m_impl->environment->shuttingDown.store(false, std::memory_order_release);
		m_impl->environment->bridge.store(&bridge, std::memory_order_release);
	}

	void FTaskGraphSectorExecutionService::BeginShutdown() noexcept
	{
		m_impl->environment->shuttingDown.store(true, std::memory_order_release);
		m_impl->environment->ClearFallbackOwnerCallbacks();
	}

	void FTaskGraphSectorExecutionService::UnbindBridge(
		ContentsRuntime::Bridge::IContentBridge& bridge) noexcept
	{
		ContentsRuntime::Bridge::IContentBridge* expected = &bridge;
		(void)m_impl->environment->bridge.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel);
	}

	void FTaskGraphSectorExecutionService::DrainOwnerCallbacks(
		const ContentsRuntime::Core::FContentInstanceId ownerContentInstanceId)
	{
		m_impl->environment->DrainFallbackOwnerCallbacks(ownerContentInstanceId);
	}

	bool FTaskGraphSectorExecutionService::IsStopping() const noexcept
	{
		return m_impl->environment->shuttingDown.load(std::memory_order_acquire);
	}

	WorldCore::SSectorExecutionStartResult FTaskGraphSectorExecutionService::BeginExecution(
		const ContentsRuntime::Core::FContentInstanceId ownerContentInstanceId,
		const WorldCore::SMapTickTicket& ticket,
		const WorldCore::FSectorTickPlan& tickPlan,
		const WorldCore::FSectorTaskProcessor& taskProcessor,
		const WorldCore::FEntityRegistry& entityRegistry,
		const WorldCore::FSectorGrid& sectorGrid,
		const WorldCore::SMapDefinition& mapDefinition,
		FCompletionHandler completionHandler)
	{
		WorldCore::SSectorExecutionStartResult result{};
		result.executionResult = WorldCore::ESectorExecutionResult::Failed;
		if (m_impl->environment->shuttingDown.load(std::memory_order_acquire))
		{
			result.failureReason = "TaskGraph execution service is stopping.";
			return result;
		}
		if (m_impl->environment->bridge.load(std::memory_order_acquire) == nullptr)
		{
			result.failureReason = "TaskGraph execution bridge is not bound.";
			return result;
		}
		if (m_impl->environment->executorInstanceIds.empty())
		{
			result.failureReason = "TaskGraph has no Sector Executor Content instances.";
			return result;
		}

		std::shared_ptr<FTaskGraphExecutionContext> context;
		try
		{
			context = std::make_shared<FTaskGraphExecutionContext>(m_impl->environment,
				ownerContentInstanceId,
				ticket,
				tickPlan,
				taskProcessor,
				entityRegistry,
				sectorGrid,
				mapDefinition,
				std::move(completionHandler));
		}
		catch (const std::exception& exception)
		{
			result.failureReason = std::format("TaskGraph execution context allocation failed: {}", exception.what());
			return result;
		}
		if (!context->IsValid())
		{
			result.failureReason = context->GetValidationError();
			return result;
		}

		context->Start();
		result.executionResult = WorldCore::ESectorExecutionResult::Pending;
		return result;
	}

	std::size_t FTaskGraphSectorExecutionService::GetExecutorInstanceCount() const noexcept
	{
		return m_impl->environment->executorInstanceIds.size();
	}

	STaskGraphSectorExecutionStats FTaskGraphSectorExecutionService::GetStatsSnapshot() const noexcept
	{
		STaskGraphSectorExecutionStats stats{};
		stats.startedExecutionCount = m_impl->environment->startedExecutionCount.load(std::memory_order_relaxed);
		stats.completedExecutionCount = m_impl->environment->completedExecutionCount.load(std::memory_order_relaxed);
		stats.failedExecutionCount = m_impl->environment->failedExecutionCount.load(std::memory_order_relaxed);
		stats.canceledExecutionCount = m_impl->environment->canceledExecutionCount.load(std::memory_order_relaxed);
		stats.executedTaskCount = m_impl->environment->executedTaskCount.load(std::memory_order_relaxed);
		stats.activeTaskCount = m_impl->environment->activeTaskCount.load(std::memory_order_relaxed);
		stats.maxParallelTaskCount = m_impl->environment->maxParallelTaskCount.load(std::memory_order_relaxed);
		stats.workerMask = m_impl->environment->workerMask.load(std::memory_order_relaxed);
		return stats;
	}
}
