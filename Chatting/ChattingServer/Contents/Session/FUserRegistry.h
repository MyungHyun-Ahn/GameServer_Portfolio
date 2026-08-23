#pragma once

namespace ChattingServer::Contents
{
	class FUserRegistry
	{
	public:
		void UpsertUser(std::uint64_t sessionId, std::uint32_t userId);
		void RemoveUser(std::uint64_t sessionId);
		std::optional<std::uint32_t> GetUserId(std::uint64_t sessionId) const;
		std::optional<std::uint64_t> GetSessionId(std::uint32_t userId) const;

	private:
		mutable std::mutex m_lock;
		std::unordered_map<std::uint64_t, std::uint32_t> m_sessionUsers;
		std::unordered_map<std::uint32_t, std::uint64_t> m_userSessions;
	};
}
