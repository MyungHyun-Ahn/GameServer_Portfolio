#pragma once

namespace CacheServer::Database
{
	struct SCacheDatabaseConfig;

	class FContentThreadDbContext final
	{
	public:
		FContentThreadDbContext(SCacheDatabaseConfig config, std::uint32_t workerIndex);

		static FContentThreadDbContext& Get(const SCacheDatabaseConfig& config);

		Connector::MySql::FMySqlConnection* GetGamePrimary(std::string& outError);
		Connector::MySql::FMySqlConnection* GetGameReplica(std::string& outError);
		std::uint32_t GetWorkerIndex() const noexcept;
		std::size_t GetActiveGameReplicaIndex() const noexcept;

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

	private:
		Connector::MySql::FThreadAffinedMySqlCluster m_gameCluster;
	};
}
