#pragma once

namespace AuctionDummyClient::LoadTest
{
	class FAuctionLoadTestRunner final
	{
	public:
		explicit FAuctionLoadTestRunner(SAuctionLoadTestConfig config);

		bool Run(std::string& outError);

	private:
		bool LoadTickets(std::string& outError);
		bool ConnectNextUser(std::size_t userIndex, std::string& outError);
		bool SendAuthentication(FVirtualAuctionUser& user, std::string& outError);
		bool SendSearch(FVirtualAuctionUser& user, std::chrono::steady_clock::time_point now, std::string& outError);
		bool SendMyListings(FVirtualAuctionUser& user, std::chrono::steady_clock::time_point now, std::string& outError);
		bool SendMyBids(FVirtualAuctionUser& user, std::chrono::steady_clock::time_point now, std::string& outError);
		bool SendInventoryList(FVirtualAuctionUser& user, std::chrono::steady_clock::time_point now, std::string& outError);
		bool SendGoldCheat(FVirtualAuctionUser& user, std::chrono::steady_clock::time_point now, std::string& outError);
		bool SendItemCheat(FVirtualAuctionUser& user, std::chrono::steady_clock::time_point now, std::string& outError);
		bool SendListingRegister(FVirtualAuctionUser& user, std::chrono::steady_clock::time_point now, std::string& outError);
		bool SendListingDetail(FVirtualAuctionUser& user, std::chrono::steady_clock::time_point now, std::string& outError);
		bool SendBid(FVirtualAuctionUser& user, std::chrono::steady_clock::time_point now, std::string& outError);
		bool SendBidRefund(FVirtualAuctionUser& user, std::chrono::steady_clock::time_point now, std::string& outError);
		bool SendMailDetail(FVirtualAuctionUser& user, std::chrono::steady_clock::time_point now, std::string& outError);
		bool SendMailClaim(FVirtualAuctionUser& user, std::chrono::steady_clock::time_point now, std::string& outError);
		void ProcessEvents(std::chrono::steady_clock::time_point now);
		void ProcessPacketEvent(const ClientNetworkLib::FClientEvent& event, std::chrono::steady_clock::time_point now);
		void ProcessTimeouts(std::chrono::steady_clock::time_point now);
		void ScheduleReadyUsers(std::chrono::steady_clock::time_point now, bool allowNewRequests);
		void MarkUserFailed(FVirtualAuctionUser& user, std::string_view reason, bool networkFailure);
		std::chrono::milliseconds MakeSearchInterval();
		bool IsAuthenticatedLoadUser(std::uint64_t userId) const noexcept;
		void CacheBidCandidates(FVirtualAuctionUser& user, const Generated::Auction::FListingSearchRp& response);
		std::size_t CountConnectedUsers() const noexcept;
		std::size_t CountAuthenticatedUsers() const noexcept;
		bool HasPendingRequests() const noexcept;

	private:
		SAuctionLoadTestConfig m_config;
		std::vector<std::string> m_tickets;
		std::vector<FVirtualAuctionUser> m_users;
		std::unordered_map<ClientNetworkLib::FClientSessionId, std::size_t> m_userIndexBySession;
		std::unordered_set<std::uint64_t> m_authenticatedUserIds;
		std::unique_ptr<ClientNetworkLib::FClientNetwork> m_client;
		FLoadTestMetrics m_metrics;
		std::mt19937 m_random;
	};

	bool LoadAuctionLoadTestConfig(const std::filesystem::path& configPath, SAuctionLoadTestConfig& outConfig, std::string& outError);
	int RunAuctionLoadTest(const std::filesystem::path& configPath);
}
