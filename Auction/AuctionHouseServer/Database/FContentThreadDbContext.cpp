#include "AuctionHouseServerPch.h"

#include "AuctionHouseServer/Database/FContentThreadDbContext.h"

#include "ContentsRuntime/Core/ContentExecutionState.h"
#include "ContentsRuntime/Threading/FContentThread.h"

namespace AuctionHouseServer::Database
{
	namespace
	{
		Connector::MySql::SMySqlClusterConfig BuildClusterConfig(
			const bool enabled,
			const Connector::MySql::SMySqlConnectionConfig& primary,
			const std::vector<Connector::MySql::SMySqlConnectionConfig>& replicas,
			const std::uint32_t reconnectCooldownMilliseconds)
		{
			Connector::MySql::SMySqlClusterConfig clusterConfig;
			clusterConfig.enabled = enabled;
			clusterConfig.primary = primary;
			clusterConfig.replicas = replicas;
			clusterConfig.replicaReconnectCooldownMilliseconds = reconnectCooldownMilliseconds;
			return clusterConfig;
		}
	}

	FContentThreadDbContext::FContentThreadDbContext(
		SAuctionDatabaseConfig config,
		const std::uint32_t workerIndex)
		: m_gameCluster(
			  BuildClusterConfig(config.enabled, config.gamePrimary, config.gameReplicas, config.replicaReconnectCooldownMilliseconds),
			  workerIndex)
		, m_auctionCluster(BuildClusterConfig(config.enabled,
							   config.auctionPrimary,
							   config.auctionReplicas,
							   config.replicaReconnectCooldownMilliseconds),
			  workerIndex)
	{
	}

	FContentThreadDbContext& FContentThreadDbContext::Get(
		const SAuctionDatabaseConfig& config)
	{
		thread_local std::unique_ptr<FContentThreadDbContext> context;
		if (context == nullptr)
		{
			std::uint32_t workerIndex = ContentsRuntime::Threading::FContentThread::GetCurrentWorkerIndex();
			if (workerIndex == ContentsRuntime::Core::SContentExecutionState::kInvalidWorkerIndex)
				workerIndex = 0;
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

	Connector::MySql::FMySqlConnection* FContentThreadDbContext::GetAuctionPrimary(
		std::string& outError)
	{
		return m_auctionCluster.GetPrimary(outError);
	}

	Connector::MySql::FMySqlConnection* FContentThreadDbContext::GetAuctionReplica(
		std::string& outError)
	{
		return m_auctionCluster.GetReplica(outError);
	}
}
