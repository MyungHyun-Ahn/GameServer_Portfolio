#pragma once

namespace cpp_redis
{
	class client;
}

namespace Connector
{
	class FRedisLoginTicketStore final : public ILoginTicketStore
	{
	public:
		explicit FRedisLoginTicketStore(SRedisLoginTicketStoreConfig config);
		~FRedisLoginTicketStore() override;

		bool TryConsumeLoginTicket(std::string_view ticket, SConsumedLoginTicket& outTicket, std::string& outError) override;

	private:
		bool EnsureConnected(std::string& outError);
		std::string BuildTicketKey(std::string_view ticket) const;
		std::string BuildActiveLoginKey(std::uint32_t userId) const;

	private:
		mutable std::mutex m_mutex;
		SRedisLoginTicketStoreConfig m_config;
		std::unique_ptr<cpp_redis::client> m_client;
		bool m_authenticated = false;
		bool m_selectedDatabase = false;
	};

	using FRedisChatTicketStore = FRedisLoginTicketStore;
}
