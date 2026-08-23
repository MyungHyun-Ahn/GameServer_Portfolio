#pragma once

namespace Foundation::Diagnostics
{
	class FTimingMetricsRuntime;
}

namespace Connector::MySql
{
	struct SMySqlTimingConfig
	{
		Foundation::Diagnostics::FTimingMetricsRuntime* timingMetricsRuntime = nullptr;
		std::unordered_map<std::string, std::uint16_t> procedureMetricIndices;
		std::optional<std::uint16_t> connectMetricIndex;
		std::optional<std::uint16_t> beginMetricIndex;
		std::optional<std::uint16_t> commitMetricIndex;
		std::optional<std::uint16_t> rollbackMetricIndex;
	};

	struct SMySqlConnectionConfig
	{
		std::string host = "127.0.0.1";
		std::uint16_t port = 3306;
		std::string user;
		std::string password;
		std::string database;
		std::uint32_t connectTimeoutSeconds = 3;
		std::shared_ptr<const SMySqlTimingConfig> timingConfig;
	};

	using FMySqlCell = std::optional<std::string>;
	using FMySqlRow = std::vector<FMySqlCell>;

	struct SMySqlResultSet
	{
		std::vector<std::string> columnNames;
		std::vector<FMySqlRow> rows;
	};
}
