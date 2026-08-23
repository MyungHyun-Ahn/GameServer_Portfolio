#pragma once

namespace AuctionHouseServer::Database
{
	class FContentThreadDbContext final
	{
	public:
		FContentThreadDbContext(SAuctionDatabaseConfig config, std::uint32_t workerIndex);

		static FContentThreadDbContext& Get(const SAuctionDatabaseConfig& config);

		Connector::MySql::FMySqlConnection* GetGamePrimary(std::string& outError);
		Connector::MySql::FMySqlConnection* GetGameReplica(std::string& outError);
		Connector::MySql::FMySqlConnection* GetAuctionPrimary(std::string& outError);
		Connector::MySql::FMySqlConnection* GetAuctionReplica(std::string& outError);
		std::uint32_t GetWorkerIndex() const noexcept
		{
			return static_cast<std::uint32_t>(m_gameCluster.GetAffinityIndex());
		}
		std::size_t GetActiveGameReplicaIndex() const noexcept
		{
			return m_gameCluster.GetActiveReplicaIndex();
		}
		std::size_t GetActiveAuctionReplicaIndex() const noexcept
		{
			return m_auctionCluster.GetActiveReplicaIndex();
		}

		template <typename TOperation>
		bool ExecuteGameReadWithPrimaryFallback(
			const bool requirePrimary,
			TOperation&& operation,
			bool& outUsedPrimary,
			std::string& outError)
		{
			return m_gameCluster.ExecuteReadWithPrimaryFallback(
				requirePrimary, std::forward<TOperation>(operation), outUsedPrimary, outError);
		}

		template <typename TOperation>
		bool ExecuteAuctionReadWithPrimaryFallback(
			const bool requirePrimary,
			TOperation&& operation,
			bool& outUsedPrimary,
			std::string& outError)
		{
			return m_auctionCluster.ExecuteReadWithPrimaryFallback(
				requirePrimary, std::forward<TOperation>(operation), outUsedPrimary, outError);
		}

	private:
		Connector::MySql::FThreadAffinedMySqlCluster m_gameCluster;
		Connector::MySql::FThreadAffinedMySqlCluster m_auctionCluster;
	};
}
