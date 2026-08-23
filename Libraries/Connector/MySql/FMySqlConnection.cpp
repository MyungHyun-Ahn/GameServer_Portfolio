#include "ConnectorPch.h"

#include "Connector/MySql/FMySqlConnection.h"

#include <mysql.h>

namespace Connector::MySql
{
	struct FMySqlConnection::SImpl
	{
		explicit SImpl(
			SMySqlConnectionConfig connectionConfig)
			: config(std::move(connectionConfig))
			, timingCollector(config.timingConfig != nullptr ? config.timingConfig->timingMetricsRuntime : nullptr)
		{
		}

		SMySqlConnectionConfig config;
		Foundation::Diagnostics::FTimingThreadLocalCollector timingCollector;
		MYSQL* connection = nullptr;
		std::thread::id ownerThreadId{};
		bool lastFailureConnectionLost = false;
	};

	namespace
	{
		bool ValidateOwnerThread(
			const std::thread::id ownerThreadId,
			std::string& outError)
		{
			if (ownerThreadId != std::this_thread::get_id())
			{
				outError = "MySQL connection accessed from a non-owner thread.";
				return false;
			}
			return true;
		}

		std::string BuildMySqlError(
			MYSQL* connection)
		{
			if (connection == nullptr)
			{
				return "MySQL connection is null.";
			}
			std::ostringstream oss;
			oss << "mysqlError=" << mysql_errno(connection) << " sqlState=" << mysql_sqlstate(connection)
				<< " message=" << mysql_error(connection);
			return oss.str();
		}

		bool IsConnectionLostError(
			const unsigned int errorCode) noexcept
		{
			return errorCode == 1053 || errorCode == 2006 || errorCode == 2013 || errorCode == 2055;
		}

		std::optional<Foundation::Diagnostics::FTimingMetricIndex> FindProcedureMetricIndex(
			const SMySqlTimingConfig* const timingConfig,
			const std::string_view sql)
		{
			if (timingConfig == nullptr || timingConfig->timingMetricsRuntime == nullptr)
			{
				return std::nullopt;
			}

			std::size_t cursor = 0;
			while (cursor < sql.size() && std::isspace(static_cast<unsigned char>(sql[cursor])) != 0)
			{
				++cursor;
			}

			constexpr std::string_view kCallPrefix = "CALL ";
			if (sql.substr(cursor, kCallPrefix.size()) != kCallPrefix)
			{
				return std::nullopt;
			}

			cursor += kCallPrefix.size();
			const std::size_t procedureEnd = sql.find_first_of("( \t\r\n;", cursor);
			const std::string procedureName(sql.substr(cursor, procedureEnd - cursor));
			const auto metricIt = timingConfig->procedureMetricIndices.find(procedureName);
			if (metricIt == timingConfig->procedureMetricIndices.end())
			{
				return std::nullopt;
			}

			return static_cast<Foundation::Diagnostics::FTimingMetricIndex>(metricIt->second);
		}
	}

	FMySqlConnection::FMySqlConnection(
		SMySqlConnectionConfig config)
		: m_impl(std::make_unique<SImpl>(std::move(config)))
	{
	}

	FMySqlConnection::~FMySqlConnection()
	{
		Close();
	}

	bool FMySqlConnection::Connect(
		std::string& outError)
	{
		outError.clear();
		m_impl->lastFailureConnectionLost = false;
		if (m_impl->connection != nullptr)
		{
			return ValidateOwnerThread(m_impl->ownerThreadId, outError);
		}

		m_impl->ownerThreadId = std::this_thread::get_id();
		std::optional<Foundation::Diagnostics::FTimingScope> connectTimingScope;
		if (m_impl->config.timingConfig != nullptr && m_impl->config.timingConfig->connectMetricIndex.has_value())
		{
			connectTimingScope.emplace(m_impl->timingCollector,
				static_cast<Foundation::Diagnostics::FTimingMetricIndex>(*m_impl->config.timingConfig->connectMetricIndex),
				m_impl->config.port);
		}

		MYSQL* connection = mysql_init(nullptr);
		if (connection == nullptr)
		{
			outError = "mysql_init failed.";
			return false;
		}

		const unsigned int timeout = m_impl->config.connectTimeoutSeconds;
		mysql_options(connection, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);
		if (mysql_real_connect(connection,
				m_impl->config.host.c_str(),
				m_impl->config.user.c_str(),
				m_impl->config.password.c_str(),
				m_impl->config.database.c_str(),
				m_impl->config.port,
				nullptr,
				CLIENT_MULTI_RESULTS) == nullptr)
		{
			outError = BuildMySqlError(connection);
			m_impl->lastFailureConnectionLost = true;
			mysql_close(connection);
			return false;
		}

		m_impl->connection = connection;
		return true;
	}

	void FMySqlConnection::Close() noexcept
	{
		if (m_impl->connection != nullptr)
		{
			mysql_close(m_impl->connection);
			m_impl->connection = nullptr;
		}
		m_impl->ownerThreadId = {};
	}

	bool FMySqlConnection::IsConnected() const noexcept
	{
		return m_impl->connection != nullptr;
	}

	bool FMySqlConnection::WasLastFailureConnectionLost() const noexcept
	{
		return m_impl->lastFailureConnectionLost;
	}

	bool FMySqlConnection::Ping(
		std::string& outError)
	{
		if (!Connect(outError) || !ValidateOwnerThread(m_impl->ownerThreadId, outError))
		{
			return false;
		}
		if (mysql_ping(m_impl->connection) != 0)
		{
			const unsigned int errorCode = mysql_errno(m_impl->connection);
			outError = BuildMySqlError(m_impl->connection);
			m_impl->lastFailureConnectionLost = IsConnectionLostError(errorCode);
			if (m_impl->lastFailureConnectionLost)
				Close();
			return false;
		}
		return true;
	}

	bool FMySqlConnection::EscapeString(
		const std::string_view value,
		std::string& outEscaped,
		std::string& outError)
	{
		if (!Connect(outError) || !ValidateOwnerThread(m_impl->ownerThreadId, outError))
		{
			return false;
		}
		outEscaped.resize(value.size() * 2 + 1);
		const unsigned long length =
			mysql_real_escape_string(m_impl->connection, outEscaped.data(), value.data(), static_cast<unsigned long>(value.size()));
		outEscaped.resize(length);
		outError.clear();
		return true;
	}

	bool FMySqlConnection::Execute(
		const std::string& sql,
		std::string& outError)
	{
		std::vector<SMySqlResultSet> ignored;
		return ExecuteQuery(sql, ignored, outError);
	}

	bool FMySqlConnection::ExecuteQuery(
		const std::string& sql,
		std::vector<SMySqlResultSet>& outResultSets,
		std::string& outError)
	{
		outResultSets.clear();
		m_impl->lastFailureConnectionLost = false;
		if (!Connect(outError) || !ValidateOwnerThread(m_impl->ownerThreadId, outError))
		{
			return false;
		}

		std::optional<Foundation::Diagnostics::FTimingScope> procedureTimingScope;
		const SMySqlTimingConfig* const timingConfig = m_impl->config.timingConfig.get();
		const auto procedureMetricIndex = FindProcedureMetricIndex(timingConfig, sql);
		if (procedureMetricIndex.has_value())
		{
			procedureTimingScope.emplace(m_impl->timingCollector, *procedureMetricIndex, m_impl->config.port);
		}

		if (mysql_real_query(m_impl->connection, sql.data(), static_cast<unsigned long>(sql.size())) != 0)
		{
			const unsigned int errorCode = mysql_errno(m_impl->connection);
			outError = BuildMySqlError(m_impl->connection);
			m_impl->lastFailureConnectionLost = IsConnectionLostError(errorCode);
			if (m_impl->lastFailureConnectionLost)
				Close();
			return false;
		}

		do
		{
			MYSQL_RES* result = mysql_store_result(m_impl->connection);
			if (result != nullptr)
			{
				SMySqlResultSet resultSet;
				const unsigned int fieldCount = mysql_num_fields(result);
				MYSQL_FIELD* fields = mysql_fetch_fields(result);
				resultSet.columnNames.reserve(fieldCount);
				for (unsigned int index = 0; index < fieldCount; ++index)
				{
					resultSet.columnNames.emplace_back(fields[index].name != nullptr ? fields[index].name : "");
				}

				while (MYSQL_ROW row = mysql_fetch_row(result))
				{
					const unsigned long* lengths = mysql_fetch_lengths(result);
					FMySqlRow outputRow;
					outputRow.reserve(fieldCount);
					for (unsigned int index = 0; index < fieldCount; ++index)
					{
						if (row[index] == nullptr)
						{
							outputRow.emplace_back(std::nullopt);
						}
						else
						{
							outputRow.emplace_back(std::string(row[index], lengths[index]));
						}
					}
					resultSet.rows.push_back(std::move(outputRow));
				}
				mysql_free_result(result);
				outResultSets.push_back(std::move(resultSet));
			}
			else if (mysql_field_count(m_impl->connection) != 0)
			{
				const unsigned int errorCode = mysql_errno(m_impl->connection);
				outError = BuildMySqlError(m_impl->connection);
				m_impl->lastFailureConnectionLost = IsConnectionLostError(errorCode);
				if (m_impl->lastFailureConnectionLost)
					Close();
				return false;
			}
		} while (mysql_next_result(m_impl->connection) == 0);

		if (mysql_errno(m_impl->connection) != 0)
		{
			const unsigned int errorCode = mysql_errno(m_impl->connection);
			outError = BuildMySqlError(m_impl->connection);
			m_impl->lastFailureConnectionLost = IsConnectionLostError(errorCode);
			if (m_impl->lastFailureConnectionLost)
				Close();
			return false;
		}
		return true;
	}

	bool FMySqlConnection::Begin(
		std::string& outError)
	{
		std::optional<Foundation::Diagnostics::FTimingScope> timingScope;
		if (m_impl->config.timingConfig != nullptr && m_impl->config.timingConfig->beginMetricIndex.has_value())
		{
			timingScope.emplace(m_impl->timingCollector,
				static_cast<Foundation::Diagnostics::FTimingMetricIndex>(*m_impl->config.timingConfig->beginMetricIndex),
				m_impl->config.port);
		}
		return Execute("START TRANSACTION", outError);
	}

	bool FMySqlConnection::Commit(
		std::string& outError)
	{
		std::optional<Foundation::Diagnostics::FTimingScope> timingScope;
		if (m_impl->config.timingConfig != nullptr && m_impl->config.timingConfig->commitMetricIndex.has_value())
		{
			timingScope.emplace(m_impl->timingCollector,
				static_cast<Foundation::Diagnostics::FTimingMetricIndex>(*m_impl->config.timingConfig->commitMetricIndex),
				m_impl->config.port);
		}
		return Execute("COMMIT", outError);
	}

	bool FMySqlConnection::Rollback(
		std::string& outError) noexcept
	{
		std::optional<Foundation::Diagnostics::FTimingScope> timingScope;
		if (m_impl->config.timingConfig != nullptr && m_impl->config.timingConfig->rollbackMetricIndex.has_value())
		{
			timingScope.emplace(m_impl->timingCollector,
				static_cast<Foundation::Diagnostics::FTimingMetricIndex>(*m_impl->config.timingConfig->rollbackMetricIndex),
				m_impl->config.port);
		}

		try
		{
			return Execute("ROLLBACK", outError);
		}
		catch (...)
		{
			outError = "Unexpected exception while rolling back MySQL transaction.";
			return false;
		}
	}
}
