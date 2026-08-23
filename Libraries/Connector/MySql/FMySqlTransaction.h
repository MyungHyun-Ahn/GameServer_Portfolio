#pragma once

namespace Connector::MySql
{
	class FMySqlConnection;

	class FMySqlTransaction final
	{
	public:
		explicit FMySqlTransaction(FMySqlConnection& connection) noexcept;
		~FMySqlTransaction();

		FMySqlTransaction(const FMySqlTransaction&) = delete;
		FMySqlTransaction& operator=(const FMySqlTransaction&) = delete;

		bool Begin(std::string& outError);
		bool Commit(std::string& outError);
		void Rollback() noexcept;
		bool IsActive() const noexcept
		{
			return m_active;
		}

	private:
		FMySqlConnection& m_connection;
		bool m_active = false;
	};
}
