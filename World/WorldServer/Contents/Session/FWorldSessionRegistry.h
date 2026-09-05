#pragma once

namespace WorldServer::Contents
{
	class FWorldSession;

	class FWorldSessionRegistry final
	{
	public:
		bool Add(std::uint64_t sessionId);
		void MarkDisconnected(std::uint64_t sessionId);
		void Remove(std::uint64_t sessionId);
		std::shared_ptr<FWorldSession> Find(std::uint64_t sessionId) const;

	private:
		mutable std::mutex m_lock;
		std::unordered_map<std::uint64_t, std::shared_ptr<FWorldSession>> m_sessions;
	};
}
