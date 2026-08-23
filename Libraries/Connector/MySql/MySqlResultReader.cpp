#include "ConnectorPch.h"

#include "Connector/MySql/MySqlResultReader.h"

namespace Connector::MySql
{
	const SMySqlResultSet* FindFirstRows(
		const std::vector<SMySqlResultSet>& resultSets) noexcept
	{
		for (const auto& resultSet : resultSets)
		{
			if (!resultSet.rows.empty())
			{
				return &resultSet;
			}
		}
		return nullptr;
	}
}
