#pragma once

namespace Connector::MySql
{
	struct SMySqlClusterConfig
	{
		bool enabled = false;
		SMySqlConnectionConfig primary;
		std::vector<SMySqlConnectionConfig> replicas;
		std::uint32_t replicaReconnectCooldownMilliseconds = 60000;
	};

	class FThreadAffinedMySqlCluster final
	{
	public:
		static constexpr std::size_t kInvalidReplicaIndex = std::numeric_limits<std::size_t>::max();

		FThreadAffinedMySqlCluster(SMySqlClusterConfig config, std::size_t affinityIndex);

		FMySqlConnection* GetPrimary(std::string& outError);
		FMySqlConnection* GetReplica(std::string& outError);

		std::size_t GetAffinityIndex() const noexcept
		{
			return m_affinityIndex;
		}
		std::size_t GetPreferredReplicaIndex() const noexcept
		{
			return m_replica.preferredIndex;
		}
		std::size_t GetActiveReplicaIndex() const noexcept
		{
			return m_replica.activeIndex;
		}

		template <typename TOperation>
		bool ExecuteReadWithPrimaryFallback(
			const bool requirePrimary,
			TOperation&& operation,
			bool& outUsedPrimary,
			std::string& outError)
		{
			outUsedPrimary = requirePrimary;
			if (requirePrimary)
			{
				auto* primary = GetPrimary(outError);
				return primary != nullptr && operation(*primary, outError);
			}

			const auto now = std::chrono::steady_clock::now();
			if (!m_config.replicas.empty())
			{
				std::size_t startIndex = m_replica.activeIndex;
				if (startIndex >= m_config.replicas.size() ||
					(startIndex != m_replica.preferredIndex && now >= m_replica.reconnectAfter[m_replica.preferredIndex]))
				{
					startIndex = m_replica.preferredIndex;
				}

				for (std::size_t offset = 0; offset < m_config.replicas.size(); ++offset)
				{
					const std::size_t candidateIndex = (startIndex + offset) % m_config.replicas.size();
					if (now < m_replica.reconnectAfter[candidateIndex])
						continue;

					if (m_replica.activeIndex != candidateIndex)
					{
						m_replica.connection.reset();
						m_replica.activeIndex = candidateIndex;
					}
					auto* replica = GetOrConnect(m_replica.connection, m_config.replicas[candidateIndex], outError);
					if (replica == nullptr)
					{
						MarkReplicaUnavailable(candidateIndex, now);
						continue;
					}
					if (operation(*replica, outError))
						return true;
					if (!replica->WasLastFailureConnectionLost())
						return false;

					MarkReplicaUnavailable(candidateIndex, now);
				}
			}

			outUsedPrimary = true;
			auto* primary = GetPrimary(outError);
			return primary != nullptr && operation(*primary, outError);
		}

	private:
		struct SReplicaState
		{
			std::unique_ptr<FMySqlConnection> connection;
			std::vector<std::chrono::steady_clock::time_point> reconnectAfter;
			std::size_t preferredIndex = 0;
			std::size_t activeIndex = kInvalidReplicaIndex;
		};

		FMySqlConnection* GetOrConnect(std::unique_ptr<FMySqlConnection>& connection,
			const SMySqlConnectionConfig& config,
			std::string& outError);
		void MarkReplicaUnavailable(std::size_t replicaIndex, std::chrono::steady_clock::time_point now);

	private:
		SMySqlClusterConfig m_config;
		std::size_t m_affinityIndex = 0;
		std::unique_ptr<FMySqlConnection> m_primary;
		SReplicaState m_replica;
	};
}
