#pragma once

namespace AuctionHouseServer::Contents
{
	class FAuctionUserRegistry final
	{
	public:
		std::optional<std::uint64_t> Bind(std::uint64_t sessionId, std::uint64_t userId, std::string loginId = {});
		void Remove(std::uint64_t sessionId);
		std::optional<std::uint64_t> GetUserId(std::uint64_t sessionId) const;
		std::optional<std::uint64_t> GetSessionId(std::uint64_t userId) const;
		std::optional<std::string> GetLoginId(std::uint64_t sessionId) const;

	private:
		mutable std::mutex m_lock;
		std::unordered_map<std::uint64_t, std::uint64_t> m_sessionUsers;
		std::unordered_map<std::uint64_t, std::uint64_t> m_userSessions;
		std::unordered_map<std::uint64_t, std::string> m_sessionLoginIds;
	};
}
