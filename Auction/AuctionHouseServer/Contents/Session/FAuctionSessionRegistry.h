#pragma once

namespace AuctionHouseServer::Contents
{
	class FAuctionSession;

	class FAuctionSessionRegistry final
	{
	public:
		bool Add(std::uint64_t sessionId);
		bool Bind(std::uint64_t sessionId, std::uint64_t userId, std::string loginId, std::optional<std::uint64_t>& outPreviousSessionId);
		void Remove(std::uint64_t sessionId);
		std::shared_ptr<FAuctionSession> Find(std::uint64_t sessionId) const;
		std::optional<std::uint64_t> GetUserId(std::uint64_t sessionId) const;
		std::optional<std::uint64_t> GetSessionId(std::uint64_t userId) const;
		std::optional<std::string> GetLoginId(std::uint64_t sessionId) const;

	private:
		mutable std::mutex m_lock;
		std::unordered_map<std::uint64_t, std::shared_ptr<FAuctionSession>> m_sessions;
		std::unordered_map<std::uint64_t, std::uint64_t> m_userSessions;
	};
}
