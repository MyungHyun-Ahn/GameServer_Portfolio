#include "CacheServerPch.h"

#include "CacheServer/Database/FContentThreadDbContext.h"

#include "Connector/MySql/FThreadAffinedMySqlCluster.h"
#include "ContentsRuntime/Core/ContentExecutionState.h"
#include "ContentsRuntime/Threading/FContentThread.h"

namespace CacheServer::Database
{
	namespace
	{
		Connector::MySql::SMySqlClusterConfig BuildClusterConfig(
			const SCacheDatabaseConfig& config)
		{
			Connector::MySql::SMySqlClusterConfig clusterConfig;
			clusterConfig.enabled = config.enabled;
			clusterConfig.primary = config.gamePrimary;
			clusterConfig.replicas = config.gameReplicas;
			clusterConfig.replicaReconnectCooldownMilliseconds = config.replicaReconnectCooldownMilliseconds;
			return clusterConfig;
		}
	}

	FContentThreadDbContext::FContentThreadDbContext(
		SCacheDatabaseConfig config,
		const std::uint32_t workerIndex)
		: m_gameCluster(BuildClusterConfig(config), workerIndex)
	{
	}

	FContentThreadDbContext& FContentThreadDbContext::Get(
		const SCacheDatabaseConfig& config)
	{
		thread_local std::unique_ptr<FContentThreadDbContext> context;
		if (context == nullptr)
		{
			std::uint32_t workerIndex = ContentsRuntime::Threading::FContentThread::GetCurrentWorkerIndex();
			if (workerIndex == ContentsRuntime::Core::SContentExecutionState::kInvalidWorkerIndex)
			{
				workerIndex = 0;
			}
			context = std::make_unique<FContentThreadDbContext>(config, workerIndex);
		}
		return *context;
	}

	Connector::MySql::FMySqlConnection* FContentThreadDbContext::GetGamePrimary(
		std::string& outError)
	{
		return m_gameCluster.GetPrimary(outError);
	}

	Connector::MySql::FMySqlConnection* FContentThreadDbContext::GetGameReplica(
		std::string& outError)
	{
		return m_gameCluster.GetReplica(outError);
	}

	std::uint32_t FContentThreadDbContext::GetWorkerIndex() const noexcept
	{
		return static_cast<std::uint32_t>(m_gameCluster.GetAffinityIndex());
	}

	std::size_t FContentThreadDbContext::GetActiveGameReplicaIndex() const noexcept
	{
		return m_gameCluster.GetActiveReplicaIndex();
	}
}
