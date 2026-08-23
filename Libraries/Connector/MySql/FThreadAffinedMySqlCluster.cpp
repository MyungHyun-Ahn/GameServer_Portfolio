#include "ConnectorPch.h"

#include "Connector/MySql/FMySqlConnection.h"
#include "Connector/MySql/FThreadAffinedMySqlCluster.h"

namespace Connector::MySql
{
	FThreadAffinedMySqlCluster::FThreadAffinedMySqlCluster(
		SMySqlClusterConfig config,
		const std::size_t affinityIndex)
		: m_config(std::move(config))
		, m_affinityIndex(affinityIndex)
	{
		m_replica.reconnectAfter.resize(m_config.replicas.size());
		if (!m_config.replicas.empty())
			m_replica.preferredIndex = affinityIndex % m_config.replicas.size();
	}

	FMySqlConnection* FThreadAffinedMySqlCluster::GetPrimary(
		std::string& outError)
	{
		return GetOrConnect(m_primary, m_config.primary, outError);
	}

	FMySqlConnection* FThreadAffinedMySqlCluster::GetReplica(
		std::string& outError)
	{
		if (!m_config.enabled)
		{
			outError = "MySQL cluster is disabled.";
			return nullptr;
		}
		if (m_config.replicas.empty())
		{
			outError = "No MySQL replicas are configured.";
			return nullptr;
		}

		const auto now = std::chrono::steady_clock::now();
		for (std::size_t offset = 0; offset < m_config.replicas.size(); ++offset)
		{
			const std::size_t candidateIndex = (m_replica.preferredIndex + offset) % m_config.replicas.size();
			if (now < m_replica.reconnectAfter[candidateIndex])
				continue;
			if (m_replica.activeIndex != candidateIndex)
			{
				m_replica.connection.reset();
				m_replica.activeIndex = candidateIndex;
			}
			auto* connection = GetOrConnect(m_replica.connection, m_config.replicas[candidateIndex], outError);
			if (connection != nullptr)
				return connection;
			MarkReplicaUnavailable(candidateIndex, now);
		}
		return nullptr;
	}

	FMySqlConnection* FThreadAffinedMySqlCluster::GetOrConnect(
		std::unique_ptr<FMySqlConnection>& connection,
		const SMySqlConnectionConfig& config,
		std::string& outError)
	{
		if (!m_config.enabled)
		{
			outError = "MySQL cluster is disabled.";
			return nullptr;
		}

		if (connection == nullptr)
			connection = std::make_unique<FMySqlConnection>(config);
		if (!connection->Connect(outError))
			return nullptr;
		return connection.get();
	}

	void FThreadAffinedMySqlCluster::MarkReplicaUnavailable(
		const std::size_t replicaIndex,
		const std::chrono::steady_clock::time_point now)
	{
		m_replica.reconnectAfter[replicaIndex] = now + std::chrono::milliseconds(m_config.replicaReconnectCooldownMilliseconds);
		m_replica.connection.reset();
	}
}
