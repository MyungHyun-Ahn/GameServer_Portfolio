#include "ConnectorPch.h"

#include "Connector/MySql/FMySqlTransaction.h"

#include "Connector/MySql/FMySqlConnection.h"

namespace Connector::MySql
{
	FMySqlTransaction::FMySqlTransaction(
		FMySqlConnection& connection) noexcept
		: m_connection(connection)
	{
	}

	FMySqlTransaction::~FMySqlTransaction()
	{
		Rollback();
	}

	bool FMySqlTransaction::Begin(
		std::string& outError)
	{
		if (m_active)
		{
			outError = "MySQL transaction is already active.";
			return false;
		}
		m_active = m_connection.Begin(outError);
		return m_active;
	}

	bool FMySqlTransaction::Commit(
		std::string& outError)
	{
		if (!m_active)
		{
			outError = "MySQL transaction is not active.";
			return false;
		}
		if (!m_connection.Commit(outError))
		{
			return false;
		}
		m_active = false;
		return true;
	}

	void FMySqlTransaction::Rollback() noexcept
	{
		if (!m_active)
		{
			return;
		}
		std::string ignored;
		m_connection.Rollback(ignored);
		m_active = false;
	}
}
