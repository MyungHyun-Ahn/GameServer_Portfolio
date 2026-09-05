#include "ConnectorPch.h"

#include "Connector/Redis/FRedisChatTicketStore.h"

#include <cpp_redis/core/client.hpp>

namespace Connector
{
	namespace
	{
		bool TryParseUserId(
			std::string_view value,
			std::uint32_t& outUserId) noexcept
		{
			outUserId = 0;
			if (value.empty())
			{
				return false;
			}

			const char* const begin = value.data();
			const char* const end = value.data() + value.size();
			const auto [parseEnd, parseError] = std::from_chars(begin, end, outUserId);
			return parseError == std::errc() && parseEnd == end && outUserId != 0;
		}

		bool TryParseUInt64(
			std::string_view value,
			std::uint64_t& outValue) noexcept
		{
			outValue = 0;
			if (value.empty())
			{
				return false;
			}

			const char* const begin = value.data();
			const char* const end = value.data() + value.size();
			const auto [parseEnd, parseError] = std::from_chars(begin, end, outValue);
			return parseError == std::errc() && parseEnd == end;
		}

		bool TryParseTicketPayload(
			const std::string_view value,
			std::uint32_t& outUserId,
			std::uint64_t& outLoginVersion,
			std::uint32_t& outTargetServerInstanceId,
			std::string& outLoginId) noexcept
		{
			outUserId = 0;
			outLoginVersion = 0;
			outTargetServerInstanceId = 0;
			outLoginId.clear();

			const std::size_t firstDelimiter = value.find(':');
			if (firstDelimiter == std::string_view::npos)
			{
				return false;
			}

			const std::size_t secondDelimiter = value.find(':', firstDelimiter + 1);
			const std::string_view userIdText = value.substr(0, firstDelimiter);
			const std::string_view loginVersionText = secondDelimiter == std::string_view::npos
														  ? value.substr(firstDelimiter + 1)
														  : value.substr(firstDelimiter + 1, secondDelimiter - firstDelimiter - 1);
			if (!TryParseUserId(userIdText, outUserId) || !TryParseUInt64(loginVersionText, outLoginVersion) || outLoginVersion == 0)
			{
				return false;
			}

			if (secondDelimiter == std::string_view::npos)
			{
				return true;
			}

			const std::size_t thirdDelimiter = value.find(':', secondDelimiter + 1);
			std::string_view encodedLoginId;
			if (thirdDelimiter == std::string_view::npos)
			{
				// Legacy Chat/Auction payload: userId:loginVersion:hexLoginId.
				encodedLoginId = value.substr(secondDelimiter + 1);
			}
			else
			{
				// Targeted payload: userId:loginVersion:targetServerInstanceId:hexLoginId.
				const std::string_view targetServerInstanceIdText = value.substr(secondDelimiter + 1, thirdDelimiter - secondDelimiter - 1);
				if (!TryParseUserId(targetServerInstanceIdText, outTargetServerInstanceId))
				{
					return false;
				}
				encodedLoginId = value.substr(thirdDelimiter + 1);
			}

			if (encodedLoginId.empty() || encodedLoginId.size() > 128 || encodedLoginId.size() % 2 != 0)
			{
				return false;
			}
			auto HexValue = [](const char value) -> int
			{
				if (value >= '0' && value <= '9')
					return value - '0';
				if (value >= 'a' && value <= 'f')
					return value - 'a' + 10;
				if (value >= 'A' && value <= 'F')
					return value - 'A' + 10;
				return -1;
			};
			outLoginId.reserve(encodedLoginId.size() / 2);
			for (std::size_t index = 0; index < encodedLoginId.size(); index += 2)
			{
				const int high = HexValue(encodedLoginId[index]);
				const int low = HexValue(encodedLoginId[index + 1]);
				if (high < 0 || low < 0)
				{
					outLoginId.clear();
					return false;
				}
				outLoginId.push_back(static_cast<char>((high << 4) | low));
			}
			return !outLoginId.empty();
		}
	}

	FRedisLoginTicketStore::FRedisLoginTicketStore(
		SRedisLoginTicketStoreConfig config)
		: m_config(std::move(config))
	{
	}

	FRedisLoginTicketStore::~FRedisLoginTicketStore()
	{
		std::scoped_lock lock(m_mutex);
		if (m_client != nullptr && m_client->is_connected())
		{
			m_client->disconnect(true);
		}
	}

	bool FRedisLoginTicketStore::TryConsumeLoginTicket(
		const std::string_view ticket,
		SConsumedLoginTicket& outTicket,
		std::string& outError)
	{
		std::scoped_lock lock(m_mutex);
		outTicket = {};
		outError.clear();

		if (ticket.empty())
		{
			outError = "login ticket is empty.";
			return false;
		}

		if (!EnsureConnected(outError))
		{
			return false;
		}

		try
		{
			auto replyFuture = m_client->send({"GETDEL", BuildTicketKey(ticket)});
			m_client->sync_commit();
			cpp_redis::reply reply = replyFuture.get();
			if (reply.is_null())
			{
				outError = "login ticket not found.";
				return false;
			}

			if (reply.is_error())
			{
				outError = reply.error();
				return false;
			}

			if (!reply.is_string())
			{
				outError = "login ticket reply type is invalid.";
				return false;
			}

			if (!TryParseTicketPayload(
					reply.as_string(), outTicket.userId, outTicket.loginVersion, outTicket.targetServerInstanceId, outTicket.loginId))
			{
				outError = "login ticket payload is invalid.";
				return false;
			}

			auto activeLoginReplyFuture = m_client->send({"GET", BuildActiveLoginKey(outTicket.userId)});
			m_client->sync_commit();
			cpp_redis::reply activeLoginReply = activeLoginReplyFuture.get();
			if (activeLoginReply.is_null())
			{
				outError = "active login version not found.";
				return false;
			}

			if (activeLoginReply.is_error())
			{
				outError = activeLoginReply.error();
				return false;
			}

			if (!activeLoginReply.is_string())
			{
				outError = "active login reply type is invalid.";
				return false;
			}

			std::uint64_t activeLoginVersion = 0;
			if (!TryParseUInt64(activeLoginReply.as_string(), activeLoginVersion) || activeLoginVersion == 0)
			{
				outError = "active login version payload is invalid.";
				return false;
			}

			if (activeLoginVersion != outTicket.loginVersion)
			{
				outError = "login ticket is stale.";
				return false;
			}

			outTicket.valid = true;
			return true;
		}
		catch (const std::exception& exception)
		{
			outError = exception.what();
			return false;
		}
	}

	bool FRedisLoginTicketStore::EnsureConnected(
		std::string& outError)
	{
		outError.clear();

		try
		{
			if (m_client == nullptr)
			{
				m_client = std::make_unique<cpp_redis::client>();
			}

			if (!m_client->is_connected())
			{
				m_client->connect(m_config.connection.host, m_config.connection.port, nullptr, m_config.connection.connectTimeoutMs);
				m_authenticated = false;
				m_selectedDatabase = false;
			}

			if (!m_authenticated && !m_config.connection.password.empty())
			{
				auto authReplyFuture = m_client->auth(m_config.connection.password);
				m_client->sync_commit();
				cpp_redis::reply authReply = authReplyFuture.get();
				if (authReply.is_error())
				{
					outError = authReply.error();
					return false;
				}

				m_authenticated = true;
			}

			if (!m_selectedDatabase && m_config.connection.database != 0)
			{
				auto selectReplyFuture = m_client->select(m_config.connection.database);
				m_client->sync_commit();
				cpp_redis::reply selectReply = selectReplyFuture.get();
				if (selectReply.is_error())
				{
					outError = selectReply.error();
					return false;
				}

				m_selectedDatabase = true;
			}

			return true;
		}
		catch (const std::exception& exception)
		{
			outError = exception.what();
			return false;
		}
	}

	std::string FRedisLoginTicketStore::BuildTicketKey(
		const std::string_view ticket) const
	{
		std::string key = m_config.keyPrefix;
		key.append(ticket.data(), ticket.size());
		return key;
	}

	std::string FRedisLoginTicketStore::BuildActiveLoginKey(
		const std::uint32_t userId) const
	{
		std::string key = m_config.activeLoginKeyPrefix;
		key.append(std::to_string(userId));
		return key;
	}
}
