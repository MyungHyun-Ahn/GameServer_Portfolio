#pragma once

namespace AuctionHouseServer::Contents
{
	class FAuctionSession final : public ContentsRuntime::Session::FContentSession
	{
	public:
		explicit FAuctionSession(std::uint64_t sessionId) noexcept;

		void Authenticate(std::uint64_t userId, std::string loginId);
		bool IsAuthenticated() const noexcept;
		std::uint64_t GetUserId() const noexcept;
		const std::string& GetLoginId() const noexcept;

	private:
		std::uint64_t m_userId = 0;
		std::string m_loginId;
	};
}
