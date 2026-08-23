#pragma once

namespace Connector::MySql
{
	class FMySqlConnection final
	{
	public:
		explicit FMySqlConnection(SMySqlConnectionConfig config);
		~FMySqlConnection();

		FMySqlConnection(const FMySqlConnection&) = delete;
		FMySqlConnection& operator=(const FMySqlConnection&) = delete;

		bool Connect(std::string& outError);
		void Close() noexcept;
		bool IsConnected() const noexcept;
		bool WasLastFailureConnectionLost() const noexcept;
		bool Ping(std::string& outError);
		bool EscapeString(std::string_view value, std::string& outEscaped, std::string& outError);

		bool Execute(const std::string& sql, std::string& outError);
		bool ExecuteQuery(const std::string& sql, std::vector<SMySqlResultSet>& outResultSets, std::string& outError);
		bool Begin(std::string& outError);
		bool Commit(std::string& outError);
		bool Rollback(std::string& outError) noexcept;

	private:
		struct SImpl;
		std::unique_ptr<SImpl> m_impl;
	};
}
