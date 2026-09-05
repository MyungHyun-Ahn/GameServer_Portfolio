#include "AuctionDummyClientPch.h"

#include "LoadTest/FAuctionLoadTestRunner.h"

#include "Generated/Config/AuctionDummyClient/AuctionDummyClientConfig.h"
#include "Generated/Packets/Cpp/Auction/AuctionPackets.h"

namespace
{
	constexpr std::uint8_t kRandomKey = 0x5A;
	constexpr std::uint16_t kSuccess = 0;
	constexpr std::uint16_t kListingNotFound = 15;
	constexpr std::uint16_t kBidTooLow = 16;
	constexpr std::uint16_t kInsufficientCurrency = 17;
	constexpr std::uint16_t kListingVersionMismatch = 19;
	constexpr std::uint16_t kBidNotClaimable = 4;
	constexpr std::uint16_t kMailNotFound = 22;
	constexpr std::uint16_t kMailAttachmentNotClaimable = 23;
	constexpr std::uint16_t kBidStateInvalid = 31;
	constexpr std::uint8_t kOutbidClaimableState = 3;
	constexpr std::uint8_t kClaimableAttachmentState = 1;
	constexpr std::uint8_t kMailDetailMaxNotFoundRetries = 2;
	constexpr std::chrono::milliseconds kMailDetailFirstRetryDelay{500};
	constexpr std::chrono::milliseconds kMailDetailSecondRetryDelay{1000};
	constexpr std::string_view kAuthOperation = "AuctionAuth";
	constexpr std::string_view kSearchOperation = "ListingSearch";
	constexpr std::string_view kMyListingsOperation = "MyListings";
	constexpr std::string_view kMyBidsOperation = "MyBids";
	constexpr std::string_view kInventoryOperation = "InventoryList";
	constexpr std::string_view kCheatGoldOperation = "CheatGold";
	constexpr std::string_view kCheatItemOperation = "CheatItem";
	constexpr std::string_view kListingRegisterOperation = "ListingRegister";
	constexpr std::string_view kListingDetailOperation = "ListingDetail";
	constexpr std::string_view kBidOperation = "Bid";
	constexpr std::string_view kBidRefundOperation = "BidRefund";
	constexpr std::string_view kMailDetailOperation = "MailDetail";
	constexpr std::string_view kMailClaimOperation = "MailClaim";

	std::string Trim(
		std::string value)
	{
		const auto isNotSpace = [](const unsigned char character)
		{
			return std::isspace(character) == 0;
		};
		value.erase(value.begin(), std::find_if(value.begin(), value.end(), isNotSpace));
		value.erase(std::find_if(value.rbegin(), value.rend(), isNotSpace).base(), value.end());
		return value;
	}

	bool TryParseItemDataIds(
		const std::string_view text,
		std::vector<std::uint32_t>& outValues)
	{
		std::size_t begin = 0;
		while (begin <= text.size())
		{
			const std::size_t end = text.find(',', begin);
			const std::string token =
				Trim(std::string(text.substr(begin, end == std::string_view::npos ? text.size() - begin : end - begin)));
			std::uint32_t value = 0;
			const auto parseResult = std::from_chars(token.data(), token.data() + token.size(), value);
			if (token.empty() || parseResult.ec != std::errc{} || parseResult.ptr != token.data() + token.size() || value == 0)
				return false;
			outValues.push_back(value);
			if (end == std::string_view::npos)
				break;
			begin = end + 1;
		}
		return !outValues.empty();
	}

	std::string_view GetOperationName(
		const AuctionDummyClient::LoadTest::ELoadTestOperation operation) noexcept
	{
		using AuctionDummyClient::LoadTest::ELoadTestOperation;
		switch (operation)
		{
			case ELoadTestOperation::AuctionAuth:
				return kAuthOperation;
			case ELoadTestOperation::ListingSearch:
				return kSearchOperation;
			case ELoadTestOperation::MyListings:
				return kMyListingsOperation;
			case ELoadTestOperation::MyBids:
				return kMyBidsOperation;
			case ELoadTestOperation::InventoryList:
				return kInventoryOperation;
			case ELoadTestOperation::CheatGold:
				return kCheatGoldOperation;
			case ELoadTestOperation::CheatItem:
				return kCheatItemOperation;
			case ELoadTestOperation::ListingRegister:
				return kListingRegisterOperation;
			case ELoadTestOperation::ListingDetail:
				return kListingDetailOperation;
			case ELoadTestOperation::Bid:
				return kBidOperation;
			case ELoadTestOperation::BidRefund:
				return kBidRefundOperation;
			case ELoadTestOperation::MailDetail:
				return kMailDetailOperation;
			case ELoadTestOperation::MailClaim:
				return kMailClaimOperation;
		}
		return "Unknown";
	}
}

namespace AuctionDummyClient::LoadTest
{
	FAuctionLoadTestRunner::FAuctionLoadTestRunner(
		SAuctionLoadTestConfig config)
		: m_config(std::move(config))
		, m_random(m_config.randomSeed)
	{
	}

	bool FAuctionLoadTestRunner::LoadTickets(
		std::string& outError)
	{
		std::ifstream input(m_config.ticketFilePath);
		if (!input)
		{
			outError = "failed to open ticket file: " + m_config.ticketFilePath.string();
			return false;
		}

		std::string line;
		while (std::getline(input, line))
		{
			line = Trim(std::move(line));
			if (!line.empty() && line.front() != '#')
				m_tickets.push_back(std::move(line));
		}
		if (m_tickets.size() < m_config.virtualUserCount)
		{
			outError = "ticket file contains fewer tickets than VirtualUserCount. tickets=" + std::to_string(m_tickets.size()) +
					   " users=" + std::to_string(m_config.virtualUserCount);
			return false;
		}
		return true;
	}

	bool FAuctionLoadTestRunner::ConnectNextUser(
		const std::size_t userIndex,
		std::string& outError)
	{
		FVirtualAuctionUser& user = m_users[userIndex];
		ClientNetworkLib::FClientSessionId sessionId = 0;
		if (!m_client->ConnectSession(sessionId, outError))
		{
			user.state = EVirtualUserState::Failed;
			m_metrics.RecordConnectFailure();
			return false;
		}

		user.sessionId = sessionId;
		m_userIndexBySession.emplace(sessionId, userIndex);
		return SendAuthentication(user, outError);
	}

	bool FAuctionLoadTestRunner::SendAuthentication(
		FVirtualAuctionUser& user,
		std::string& outError)
	{
		Generated::Auction::FAuctionAuthRq request;
		request.requestId = user.IssueRequestId();
		request.ticket = user.ticket;
		const auto now = std::chrono::steady_clock::now();
		if (!m_client->SendPacket(user.sessionId, request, kRandomKey, outError))
		{
			MarkUserFailed(user, outError, true);
			return false;
		}

		user.state = EVirtualUserState::Authenticating;
		user.pendingRequest = SPendingRequest{request.requestId,
			Generated::Auction::FAuctionAuthRp::kOpcode,
			ELoadTestOperation::AuctionAuth,
			now,
			now + m_config.responseTimeout};
		m_metrics.RecordSent(kAuthOperation);
		return true;
	}

	bool FAuctionLoadTestRunner::SendSearch(
		FVirtualAuctionUser& user,
		const std::chrono::steady_clock::time_point now,
		std::string& outError)
	{
		std::uniform_int_distribution<int> categoryDistribution(0, 3);
		std::uniform_int_distribution<int> sortDistribution(1, 4);
		std::uniform_int_distribution<int> statSelectionDistribution(0, 4);
		std::uniform_int_distribution<std::uint32_t> statValueDistribution(0, m_config.randomStatMaximum);

		Generated::Auction::FListingSearchRq request;
		request.requestId = user.IssueRequestId();
		std::uniform_int_distribution<int> broadSearchDistribution(0, 1);
		const bool broadSearch = broadSearchDistribution(m_random) == 0;
		request.itemCategory = broadSearch ? 0 : static_cast<std::uint8_t>(categoryDistribution(m_random));
		request.sortType = static_cast<std::uint8_t>(sortDistribution(m_random));
		request.limit = user.searchPageSize;
		const std::uint32_t statValue = statValueDistribution(m_random);
		switch (broadSearch ? 0 : statSelectionDistribution(m_random))
		{
			case 1:
				request.minStr = statValue;
				break;
			case 2:
				request.minDex = statValue;
				break;
			case 3:
				request.minInt = statValue;
				break;
			case 4:
				request.minLuk = statValue;
				break;
			default:
				break;
		}

		if (!m_client->SendPacket(user.sessionId, request, kRandomKey, outError))
		{
			MarkUserFailed(user, outError, true);
			return false;
		}

		user.state = EVirtualUserState::SearchPending;
		user.pendingRequest = SPendingRequest{request.requestId,
			Generated::Auction::FListingSearchRp::kOpcode,
			ELoadTestOperation::ListingSearch,
			now,
			now + m_config.responseTimeout};
		m_metrics.RecordSent(kSearchOperation);
		return true;
	}

	bool FAuctionLoadTestRunner::SendMyListings(
		FVirtualAuctionUser& user,
		const std::chrono::steady_clock::time_point now,
		std::string& outError)
	{
		Generated::Auction::FListingSearchRq request;
		request.requestId = user.IssueRequestId();
		request.sellerOnly = 1;
		request.sortType = 1;
		request.limit = user.maxActiveListings + 1;
		if (!m_client->SendPacket(user.sessionId, request, kRandomKey, outError))
		{
			MarkUserFailed(user, outError, true);
			return false;
		}

		user.needsMyListingsRefresh = false;
		user.state = EVirtualUserState::MyListingsPending;
		user.pendingRequest = SPendingRequest{request.requestId,
			Generated::Auction::FListingSearchRp::kOpcode,
			ELoadTestOperation::MyListings,
			now,
			now + m_config.responseTimeout};
		m_metrics.RecordSent(kMyListingsOperation);
		return true;
	}

	bool FAuctionLoadTestRunner::SendInventoryList(
		FVirtualAuctionUser& user,
		const std::chrono::steady_clock::time_point now,
		std::string& outError)
	{
		Generated::Auction::FInventoryListRq request;
		request.requestId = user.IssueRequestId();
		request.limit = std::min(m_config.inventoryListLimit, user.inventoryListPageSize);
		if (!m_client->SendPacket(user.sessionId, request, kRandomKey, outError))
		{
			MarkUserFailed(user, outError, true);
			return false;
		}

		user.needsInventoryRefresh = false;
		user.state = EVirtualUserState::InventoryPending;
		user.pendingRequest = SPendingRequest{request.requestId,
			Generated::Auction::FInventoryListRp::kOpcode,
			ELoadTestOperation::InventoryList,
			now,
			now + m_config.responseTimeout};
		m_metrics.RecordSent(kInventoryOperation);
		return true;
	}

	bool FAuctionLoadTestRunner::SendMyBids(
		FVirtualAuctionUser& user,
		const std::chrono::steady_clock::time_point now,
		std::string& outError)
	{
		Generated::Auction::FMyBidListRq request;
		request.requestId = user.IssueRequestId();
		request.limit = user.searchPageSize;
		if (!m_client->SendPacket(user.sessionId, request, kRandomKey, outError))
		{
			MarkUserFailed(user, outError, true);
			return false;
		}

		user.needsMyBidsRefresh = false;
		user.state = EVirtualUserState::MyBidsPending;
		user.pendingRequest = SPendingRequest{
			request.requestId, Generated::Auction::FMyBidListRp::kOpcode, ELoadTestOperation::MyBids, now, now + m_config.responseTimeout};
		m_metrics.RecordSent(kMyBidsOperation);
		return true;
	}

	bool FAuctionLoadTestRunner::SendItemCheat(
		FVirtualAuctionUser& user,
		const std::chrono::steady_clock::time_point now,
		std::string& outError)
	{
		std::uniform_int_distribution<std::size_t> itemDistribution(0, m_config.cheatItemDataIds.size() - 1);
		std::uniform_int_distribution<std::uint32_t> statDistribution(0, m_config.randomStatMaximum);
		Generated::Auction::FDebugCheatRq request;
		request.requestId = user.IssueRequestId();
		request.cheatType = 2;
		request.itemDataId = m_config.cheatItemDataIds[itemDistribution(m_random)];
		if (request.itemDataId >= 1000 && request.itemDataId < 2000)
		{
			request.strStat = statDistribution(m_random);
			request.dexStat = statDistribution(m_random);
			request.intStat = statDistribution(m_random);
			request.lukStat = statDistribution(m_random);
		}
		if (!m_client->SendPacket(user.sessionId, request, kRandomKey, outError))
		{
			MarkUserFailed(user, outError, true);
			return false;
		}

		user.state = EVirtualUserState::CheatItemPending;
		user.pendingRequest = SPendingRequest{request.requestId,
			Generated::Auction::FDebugCheatRp::kOpcode,
			ELoadTestOperation::CheatItem,
			now,
			now + m_config.responseTimeout};
		m_metrics.RecordSent(kCheatItemOperation);
		return true;
	}

	bool FAuctionLoadTestRunner::SendGoldCheat(
		FVirtualAuctionUser& user,
		const std::chrono::steady_clock::time_point now,
		std::string& outError)
	{
		Generated::Auction::FDebugCheatRq request;
		request.requestId = user.IssueRequestId();
		request.cheatType = 1;
		request.amount = m_config.initialGoldAmount;
		if (!m_client->SendPacket(user.sessionId, request, kRandomKey, outError))
		{
			MarkUserFailed(user, outError, true);
			return false;
		}

		user.needsInitialGoldCheat = false;
		user.state = EVirtualUserState::CheatGoldPending;
		user.pendingRequest = SPendingRequest{request.requestId,
			Generated::Auction::FDebugCheatRp::kOpcode,
			ELoadTestOperation::CheatGold,
			now,
			now + m_config.responseTimeout};
		m_metrics.RecordSent(kCheatGoldOperation);
		return true;
	}

	bool FAuctionLoadTestRunner::SendListingRegister(
		FVirtualAuctionUser& user,
		const std::chrono::steady_clock::time_point now,
		std::string& outError)
	{
		if (user.inventoryItems.empty())
			return SendItemCheat(user, now, outError);

		std::uniform_int_distribution<std::size_t> itemDistribution(0, user.inventoryItems.size() - 1);
		const std::uint64_t minimumStartPrice =
			std::clamp<std::uint64_t>(m_config.listingStartPriceMinimum, user.minimumListingPrice, user.maximumListingPrice);
		const std::uint64_t maximumStartPrice =
			std::clamp<std::uint64_t>(m_config.listingStartPriceMaximum, minimumStartPrice, user.maximumListingPrice);
		std::uniform_int_distribution<std::uint64_t> startPriceDistribution(minimumStartPrice, maximumStartPrice);
		std::uniform_int_distribution<std::uint64_t> markupDistribution(
			m_config.listingBuyoutMarkupMinimum, m_config.listingBuyoutMarkupMaximum);
		const SInventoryItem& item = user.inventoryItems[itemDistribution(m_random)];
		const std::uint64_t startPrice = startPriceDistribution(m_random);
		const std::uint64_t markup = markupDistribution(m_random);
		const std::uint64_t buyoutPrice = startPrice + std::min(markup, user.maximumListingPrice - startPrice);

		Generated::Auction::FListingRegisterRq request;
		request.requestId = user.IssueRequestId();
		request.itemInstanceId = item.itemInstanceId;
		request.expectedItemVersion = item.version;
		request.currencyId = user.defaultCurrencyId;
		request.startPrice = startPrice;
		request.buyoutPrice = buyoutPrice;
		request.durationSeconds = user.defaultListingDurationSeconds;
		if (!m_client->SendPacket(user.sessionId, request, kRandomKey, outError))
		{
			MarkUserFailed(user, outError, true);
			return false;
		}

		user.pendingRegistrationItemInstanceId = item.itemInstanceId;
		user.state = EVirtualUserState::ListingRegisterPending;
		user.pendingRequest = SPendingRequest{request.requestId,
			Generated::Auction::FListingRegisterRp::kOpcode,
			ELoadTestOperation::ListingRegister,
			now,
			now + m_config.responseTimeout};
		m_metrics.RecordSent(kListingRegisterOperation);
		return true;
	}

	bool FAuctionLoadTestRunner::SendListingDetail(
		FVirtualAuctionUser& user,
		const std::chrono::steady_clock::time_point now,
		std::string& outError)
	{
		if (user.outbidListingIds.empty())
			return false;
		Generated::Auction::FListingDetailRq request;
		request.requestId = user.IssueRequestId();
		request.listingId = user.outbidListingIds.front();
		user.outbidListingIds.pop_front();
		if (!m_client->SendPacket(user.sessionId, request, kRandomKey, outError))
		{
			MarkUserFailed(user, outError, true);
			return false;
		}

		user.state = EVirtualUserState::ListingDetailPending;
		user.pendingRequest = SPendingRequest{request.requestId,
			Generated::Auction::FListingDetailRp::kOpcode,
			ELoadTestOperation::ListingDetail,
			now,
			now + m_config.responseTimeout};
		m_metrics.RecordSent(kListingDetailOperation);
		return true;
	}

	bool FAuctionLoadTestRunner::SendBid(
		FVirtualAuctionUser& user,
		const std::chrono::steady_clock::time_point now,
		std::string& outError)
	{
		if (!user.pendingBidCandidate)
			return false;
		SListingCandidate& candidate = *user.pendingBidCandidate;
		const std::uint64_t minimumIncrement = std::max<std::uint64_t>(m_config.bidIncrementMinimum, user.minimumBidIncrement);
		const std::uint64_t maximumIncrement = std::max<std::uint64_t>(m_config.bidIncrementMaximum, minimumIncrement);
		std::uniform_int_distribution<std::uint64_t> incrementDistribution(minimumIncrement, maximumIncrement);
		const std::uint64_t increment = incrementDistribution(m_random);
		if (candidate.currentBidPrice != 0 && candidate.currentBidPrice > std::numeric_limits<std::uint64_t>::max() - increment)
		{
			user.pendingBidCandidate.reset();
			return false;
		}
		const std::uint64_t minimumBid =
			candidate.currentBidPrice == 0 ? candidate.startPrice : candidate.currentBidPrice + user.minimumBidIncrement;
		const std::uint64_t bidAmountBase =
			candidate.currentBidPrice == 0 ? std::max(candidate.startPrice, increment) : candidate.currentBidPrice + increment;
		std::uint64_t bidAmount = std::max(minimumBid, bidAmountBase);
		if (candidate.buyoutPrice != 0)
		{
			if (minimumBid >= candidate.buyoutPrice)
			{
				user.pendingBidCandidate.reset();
				return false;
			}
			bidAmount = std::min(bidAmount, candidate.buyoutPrice - 1);
		}

		Generated::Auction::FBidRq request;
		request.requestId = user.IssueRequestId();
		request.listingId = candidate.listingId;
		request.bidAmount = bidAmount;
		request.expectedListingVersion = candidate.version;
		if (!m_client->SendPacket(user.sessionId, request, kRandomKey, outError))
		{
			MarkUserFailed(user, outError, true);
			return false;
		}

		user.state = EVirtualUserState::BidPending;
		user.pendingRequest = SPendingRequest{
			request.requestId, Generated::Auction::FBidRp::kOpcode, ELoadTestOperation::Bid, now, now + m_config.responseTimeout};
		m_metrics.RecordSent(kBidOperation);
		return true;
	}

	bool FAuctionLoadTestRunner::SendBidRefund(
		FVirtualAuctionUser& user,
		const std::chrono::steady_clock::time_point now,
		std::string& outError)
	{
		if (user.bidRefundCandidates.empty())
			return false;
		user.pendingBidRefundCandidate = user.bidRefundCandidates.front();
		user.bidRefundCandidates.pop_front();
		const SBidRefundCandidate& candidate = *user.pendingBidRefundCandidate;
		Generated::Auction::FBidRefundRq request;
		request.requestId = user.IssueRequestId();
		request.listingId = candidate.listingId;
		request.bidId = candidate.bidId;
		request.expectedBidVersion = candidate.version;
		if (!m_client->SendPacket(user.sessionId, request, kRandomKey, outError))
		{
			MarkUserFailed(user, outError, true);
			return false;
		}

		user.state = EVirtualUserState::BidRefundPending;
		user.pendingRequest = SPendingRequest{request.requestId,
			Generated::Auction::FBidRefundRp::kOpcode,
			ELoadTestOperation::BidRefund,
			now,
			now + m_config.responseTimeout};
		m_metrics.RecordSent(kBidRefundOperation);
		return true;
	}

	bool FAuctionLoadTestRunner::SendMailDetail(
		FVirtualAuctionUser& user,
		const std::chrono::steady_clock::time_point now,
		std::string& outError)
	{
		if (user.pendingMailId == 0)
		{
			if (user.wonMailIds.empty())
				return false;
			user.pendingMailId = user.wonMailIds.front();
			user.wonMailIds.pop_front();
			user.mailDetailNotFoundRetryCount = 0;
		}
		Generated::Auction::FMailDetailRq request;
		request.requestId = user.IssueRequestId();
		request.mailId = user.pendingMailId;
		if (!m_client->SendPacket(user.sessionId, request, kRandomKey, outError))
		{
			MarkUserFailed(user, outError, true);
			return false;
		}

		user.state = EVirtualUserState::MailDetailPending;
		user.pendingRequest = SPendingRequest{request.requestId,
			Generated::Auction::FMailDetailRp::kOpcode,
			ELoadTestOperation::MailDetail,
			now,
			now + m_config.responseTimeout};
		m_metrics.RecordSent(kMailDetailOperation);
		return true;
	}

	bool FAuctionLoadTestRunner::SendMailClaim(
		FVirtualAuctionUser& user,
		const std::chrono::steady_clock::time_point now,
		std::string& outError)
	{
		if (user.mailAttachmentCandidates.empty())
			return false;
		user.pendingMailAttachment = user.mailAttachmentCandidates.front();
		user.mailAttachmentCandidates.pop_front();
		Generated::Auction::FMailClaimRq request;
		request.requestId = user.IssueRequestId();
		request.mailId = user.pendingMailAttachment.mailId;
		request.attachmentId = user.pendingMailAttachment.attachmentId;
		if (!m_client->SendPacket(user.sessionId, request, kRandomKey, outError))
		{
			MarkUserFailed(user, outError, true);
			return false;
		}

		user.state = EVirtualUserState::MailClaimPending;
		user.pendingRequest = SPendingRequest{request.requestId,
			Generated::Auction::FMailClaimRp::kOpcode,
			ELoadTestOperation::MailClaim,
			now,
			now + m_config.responseTimeout};
		m_metrics.RecordSent(kMailClaimOperation);
		return true;
	}

	void FAuctionLoadTestRunner::ProcessPacketEvent(
		const ClientNetworkLib::FClientEvent& event,
		const std::chrono::steady_clock::time_point now)
	{
		const auto userIt = m_userIndexBySession.find(event.SessionId);
		if (userIt == m_userIndexBySession.end())
		{
			m_metrics.RecordUnexpectedPacket();
			return;
		}
		FVirtualAuctionUser& user = m_users[userIt->second];
		if (event.Packet.Opcode == Generated::Auction::FAuctionOutbidNoti::kOpcode)
		{
			Generated::Auction::FAuctionOutbidNoti notification;
			if (!ClientNetworkLib::TryDeserializePacketEvent(event, notification) || notification.listingId == 0)
			{
				m_metrics.RecordUnexpectedPacket();
				return;
			}
			m_metrics.RecordNotification("AuctionOutbid");
			if (user.refundedBidIds.contains(notification.bidId))
				return;
			user.highestBidListingIds.erase(notification.listingId);
			user.needsMyBidsRefresh = true;
			if (std::find(user.outbidListingIds.begin(), user.outbidListingIds.end(), notification.listingId) ==
				user.outbidListingIds.end())
			{
				user.outbidListingIds.push_back(notification.listingId);
			}
			if (user.state == EVirtualUserState::Ready)
				user.nextActionAt = now;
			return;
		}
		if (event.Packet.Opcode == Generated::Auction::FAuctionWonNoti::kOpcode)
		{
			Generated::Auction::FAuctionWonNoti notification;
			if (!ClientNetworkLib::TryDeserializePacketEvent(event, notification) || notification.listingId == 0 ||
				notification.itemMailId == 0)
			{
				m_metrics.RecordUnexpectedPacket();
				return;
			}
			m_metrics.RecordNotification("AuctionWon");
			user.highestBidListingIds.erase(notification.listingId);
			if (std::find(user.wonMailIds.begin(), user.wonMailIds.end(), notification.itemMailId) == user.wonMailIds.end())
				user.wonMailIds.push_back(notification.itemMailId);
			if (user.state == EVirtualUserState::Ready)
				user.nextActionAt = now;
			return;
		}
		if (!user.pendingRequest || user.pendingRequest->responseOpcode != event.Packet.Opcode)
		{
			m_metrics.RecordUnexpectedPacket();
			return;
		}

		const SPendingRequest pending = *user.pendingRequest;
		const std::string_view operationName = GetOperationName(pending.operation);
		if (pending.operation == ELoadTestOperation::AuctionAuth)
		{
			Generated::Auction::FAuctionAuthRp response;
			if (!ClientNetworkLib::TryDeserializePacketEvent(event, response) || response.requestId != pending.requestId)
			{
				m_metrics.RecordResponse(operationName, false, now - pending.sentAt);
				MarkUserFailed(user, "invalid AuctionAuth response", false);
				return;
			}
			const bool succeeded = response.resultCode == kSuccess && response.userId != 0 && response.maxActiveListings != 0 &&
								   response.searchPageSize != 0 && response.searchPageSize < 100 && response.inventoryListPageSize != 0 &&
								   response.mailListPageSize != 0 && response.defaultListingDurationSeconds != 0 &&
								   response.defaultCurrencyId != 0 && response.minimumBidIncrement != 0 &&
								   response.minimumListingPrice != 0 && response.minimumListingPrice <= response.maximumListingPrice;
			m_metrics.RecordResponse(operationName, succeeded, now - pending.sentAt);
			user.pendingRequest.reset();
			if (!succeeded)
			{
				MarkUserFailed(user, "AuctionAuth rejected. resultCode=" + std::to_string(response.resultCode), false);
				return;
			}
			user.authenticatedUserId = response.userId;
			user.maxActiveListings = response.maxActiveListings;
			user.searchPageSize = response.searchPageSize;
			user.inventoryListPageSize = response.inventoryListPageSize;
			user.mailListPageSize = response.mailListPageSize;
			user.defaultListingDurationSeconds = response.defaultListingDurationSeconds;
			user.defaultCurrencyId = response.defaultCurrencyId;
			user.minimumBidIncrement = response.minimumBidIncrement;
			user.minimumListingPrice = response.minimumListingPrice;
			user.maximumListingPrice = response.maximumListingPrice;
			m_authenticatedUserIds.insert(response.userId);
			user.needsInitialGoldCheat = true;
			user.needsMyListingsRefresh = true;
			user.needsInventoryRefresh = true;
			user.state = EVirtualUserState::Ready;
			user.nextActionAt = now;
			return;
		}

		if (pending.operation == ELoadTestOperation::ListingSearch || pending.operation == ELoadTestOperation::MyListings)
		{
			Generated::Auction::FListingSearchRp response;
			if (!ClientNetworkLib::TryDeserializePacketEvent(event, response) || response.requestId != pending.requestId)
			{
				m_metrics.RecordResponse(operationName, false, now - pending.sentAt);
				MarkUserFailed(user, "invalid ListingSearch response", false);
				return;
			}
			const bool valid =
				response.listingIds.size() == response.sellerUserIds.size() && response.listingIds.size() == response.startPrices.size() &&
				response.listingIds.size() == response.currentBidPrices.size() &&
				response.listingIds.size() == response.buyoutPrices.size() && response.listingIds.size() == response.versions.size();
			const bool succeeded = valid && response.resultCode == kSuccess;
			m_metrics.RecordResponse(operationName, succeeded, now - pending.sentAt);
			user.pendingRequest.reset();
			if (!succeeded)
			{
				MarkUserFailed(user, "ListingSearch rejected. resultCode=" + std::to_string(response.resultCode), false);
				return;
			}
			if (pending.operation == ELoadTestOperation::MyListings)
				user.activeListingCount = static_cast<std::uint32_t>(response.listingIds.size());
			else
				CacheBidCandidates(user, response);
			user.state = EVirtualUserState::Ready;
			user.nextActionAt = pending.operation == ELoadTestOperation::MyListings ? now : now + MakeSearchInterval();
			return;
		}

		if (pending.operation == ELoadTestOperation::InventoryList)
		{
			Generated::Auction::FInventoryListRp response;
			const bool valid = ClientNetworkLib::TryDeserializePacketEvent(event, response) && response.requestId == pending.requestId &&
							   response.itemInstanceIds.size() == response.itemDataIds.size() &&
							   response.itemInstanceIds.size() == response.quantities.size() &&
							   response.itemInstanceIds.size() == response.equippedStates.size() &&
							   response.itemInstanceIds.size() == response.tradableStates.size() &&
							   response.itemInstanceIds.size() == response.versions.size();
			if (!valid)
			{
				m_metrics.RecordResponse(operationName, false, now - pending.sentAt);
				MarkUserFailed(user, "invalid InventoryList response", false);
				return;
			}
			const bool succeeded = response.resultCode == kSuccess;
			m_metrics.RecordResponse(operationName, succeeded, now - pending.sentAt);
			user.pendingRequest.reset();
			if (!succeeded)
			{
				MarkUserFailed(user, "InventoryList rejected. resultCode=" + std::to_string(response.resultCode), false);
				return;
			}
			user.inventoryItems.clear();
			for (std::size_t index = 0; index < response.itemInstanceIds.size(); ++index)
			{
				if (response.equippedStates[index] == 0 && response.tradableStates[index] != 0)
				{
					user.inventoryItems.push_back({response.itemInstanceIds[index],
						response.itemDataIds[index],
						response.quantities[index],
						response.versions[index]});
				}
			}
			user.state = EVirtualUserState::Ready;
			user.nextActionAt = now;
			return;
		}

		if (pending.operation == ELoadTestOperation::MyBids)
		{
			Generated::Auction::FMyBidListRp response;
			const bool valid = ClientNetworkLib::TryDeserializePacketEvent(event, response) && response.requestId == pending.requestId &&
							   response.bidIds.size() == response.listingIds.size() &&
							   response.bidIds.size() == response.bidStates.size() && response.bidIds.size() == response.bidVersions.size();
			const bool succeeded = valid && response.resultCode == kSuccess;
			m_metrics.RecordResponse(operationName, succeeded, now - pending.sentAt);
			user.pendingRequest.reset();
			if (!succeeded)
			{
				MarkUserFailed(
					user, valid ? "MyBids rejected. resultCode=" + std::to_string(response.resultCode) : "invalid MyBids response", false);
				return;
			}

			for (std::size_t index = 0; index < response.bidIds.size(); ++index)
			{
				if (response.bidStates[index] != kOutbidClaimableState)
					continue;
				const std::uint64_t listingId = response.listingIds[index];
				std::uniform_int_distribution<std::uint32_t> refundDistribution(1, 100);
				if (refundDistribution(m_random) <= m_config.outbidRefundPercent)
				{
					std::erase(user.outbidListingIds, listingId);
					const bool alreadyQueued = std::any_of(user.bidRefundCandidates.begin(),
						user.bidRefundCandidates.end(),
						[&](const SBidRefundCandidate& candidate)
						{
							return candidate.bidId == response.bidIds[index];
						});
					if (!alreadyQueued &&
						(!user.pendingBidRefundCandidate || user.pendingBidRefundCandidate->bidId != response.bidIds[index]))
					{
						user.bidRefundCandidates.push_back({listingId, response.bidIds[index], response.bidVersions[index]});
					}
				}
				else if (std::find(user.outbidListingIds.begin(), user.outbidListingIds.end(), listingId) == user.outbidListingIds.end())
				{
					user.outbidListingIds.push_back(listingId);
				}
			}
			user.state = EVirtualUserState::Ready;
			user.nextActionAt = now;
			return;
		}

		if (pending.operation == ELoadTestOperation::CheatGold || pending.operation == ELoadTestOperation::CheatItem)
		{
			Generated::Auction::FDebugCheatRp response;
			const bool valid = ClientNetworkLib::TryDeserializePacketEvent(event, response) && response.requestId == pending.requestId;
			const bool isGold = pending.operation == ELoadTestOperation::CheatGold;
			const bool succeeded = valid && response.resultCode == kSuccess && (isGold || response.itemInstanceId != 0);
			m_metrics.RecordResponse(operationName, succeeded, now - pending.sentAt);
			user.pendingRequest.reset();
			if (!succeeded)
			{
				MarkUserFailed(user,
					valid ? std::string(isGold ? "CheatGold" : "CheatItem") + " rejected. resultCode=" + std::to_string(response.resultCode)
						  : std::string("invalid ") + (isGold ? "CheatGold" : "CheatItem") + " response",
					false);
				return;
			}
			if (isGold)
				user.currencyBalance = response.currencyBalance;
			else
				user.needsInventoryRefresh = true;
			user.state = EVirtualUserState::Ready;
			user.nextActionAt = now;
			return;
		}

		if (pending.operation == ELoadTestOperation::ListingDetail)
		{
			Generated::Auction::FListingDetailRp response;
			const bool valid = ClientNetworkLib::TryDeserializePacketEvent(event, response) && response.requestId == pending.requestId;
			user.pendingRequest.reset();
			if (!valid)
			{
				m_metrics.RecordResponse(operationName, false, now - pending.sentAt);
				MarkUserFailed(user, "invalid ListingDetail response", false);
				return;
			}
			if (response.resultCode == kListingNotFound)
			{
				m_metrics.RecordExpectedRejection(operationName, now - pending.sentAt);
				user.state = EVirtualUserState::Ready;
				user.nextActionAt = now;
				return;
			}
			const bool succeeded = response.resultCode == kSuccess && response.listingId != 0;
			m_metrics.RecordResponse(operationName, succeeded, now - pending.sentAt);
			if (!succeeded)
			{
				MarkUserFailed(user, "ListingDetail rejected. resultCode=" + std::to_string(response.resultCode), false);
				return;
			}
			if (response.sellerUserId != user.authenticatedUserId && response.highestBidderUserId != user.authenticatedUserId)
			{
				user.pendingBidCandidate = SListingCandidate{response.listingId,
					response.sellerUserId,
					response.startPrice,
					response.currentBidPrice,
					response.buyoutPrice,
					response.version};
			}
			user.state = EVirtualUserState::Ready;
			user.nextActionAt = now;
			return;
		}

		if (pending.operation == ELoadTestOperation::Bid)
		{
			Generated::Auction::FBidRp response;
			const bool valid = ClientNetworkLib::TryDeserializePacketEvent(event, response) && response.requestId == pending.requestId;
			const std::uint64_t listingId = user.pendingBidCandidate ? user.pendingBidCandidate->listingId : 0;
			user.pendingRequest.reset();
			user.pendingBidCandidate.reset();
			if (!valid)
			{
				m_metrics.RecordResponse(operationName, false, now - pending.sentAt);
				MarkUserFailed(user, "invalid Bid response", false);
				return;
			}
			if (response.resultCode == kBidTooLow || response.resultCode == kListingVersionMismatch ||
				response.resultCode == kListingNotFound || response.resultCode == kInsufficientCurrency)
			{
				m_metrics.RecordExpectedRejection(operationName, now - pending.sentAt);
				if (response.resultCode == kInsufficientCurrency)
					user.needsInitialGoldCheat = true;
				else if (response.resultCode != kListingNotFound && listingId != 0 &&
						 std::find(user.outbidListingIds.begin(), user.outbidListingIds.end(), listingId) == user.outbidListingIds.end())
					user.outbidListingIds.push_back(listingId);
				user.state = EVirtualUserState::Ready;
				user.nextActionAt = now;
				return;
			}
			if (response.resultCode == kBidStateInvalid)
			{
				m_metrics.RecordExpectedRejection(operationName, now - pending.sentAt);
				user.needsMyBidsRefresh = true;
				user.state = EVirtualUserState::Ready;
				user.nextActionAt = now;
				return;
			}
			const bool succeeded = response.resultCode == kSuccess && response.listingId != 0;
			m_metrics.RecordResponse(operationName, succeeded, now - pending.sentAt);
			if (!succeeded)
			{
				MarkUserFailed(user, "Bid rejected. resultCode=" + std::to_string(response.resultCode), false);
				return;
			}
			user.currencyBalance = response.currencyBalance;
			user.refundedBidIds.erase(response.bidId);
			user.highestBidListingIds.insert(response.listingId);
			user.state = EVirtualUserState::Ready;
			user.nextActionAt = now + MakeSearchInterval();
			return;
		}

		if (pending.operation == ELoadTestOperation::BidRefund)
		{
			Generated::Auction::FBidRefundRp response;
			const bool valid = ClientNetworkLib::TryDeserializePacketEvent(event, response) && response.requestId == pending.requestId;
			const std::uint64_t refundedListingId = user.pendingBidRefundCandidate ? user.pendingBidRefundCandidate->listingId : 0;
			user.pendingRequest.reset();
			user.pendingBidRefundCandidate.reset();
			if (!valid)
			{
				m_metrics.RecordResponse(operationName, false, now - pending.sentAt);
				MarkUserFailed(user, "invalid BidRefund response", false);
				return;
			}
			if (response.resultCode == kBidNotClaimable)
			{
				m_metrics.RecordExpectedRejection(operationName, now - pending.sentAt);
				user.state = EVirtualUserState::Ready;
				user.nextActionAt = now;
				return;
			}
			const bool succeeded = response.resultCode == kSuccess && response.refundedAmount != 0;
			m_metrics.RecordResponse(operationName, succeeded, now - pending.sentAt);
			if (!succeeded)
			{
				MarkUserFailed(user, "BidRefund rejected. resultCode=" + std::to_string(response.resultCode), false);
				return;
			}
			user.currencyBalance = response.currencyBalance;
			if (refundedListingId != 0)
			{
				user.refundedBidIds.insert(response.bidId);
				std::erase_if(user.listingCandidates,
					[refundedListingId](const SListingCandidate& candidate)
					{
						return candidate.listingId == refundedListingId;
					});
			}
			user.state = EVirtualUserState::Ready;
			user.nextActionAt = now;
			return;
		}

		if (pending.operation == ELoadTestOperation::MailDetail)
		{
			Generated::Auction::FMailDetailRp response;
			const bool valid = ClientNetworkLib::TryDeserializePacketEvent(event, response) && response.requestId == pending.requestId &&
							   response.attachmentIds.size() == response.attachmentStates.size();
			user.pendingRequest.reset();
			if (!valid)
			{
				user.pendingMailId = 0;
				user.mailDetailNotFoundRetryCount = 0;
				m_metrics.RecordResponse(operationName, false, now - pending.sentAt);
				MarkUserFailed(user, "invalid MailDetail response", false);
				return;
			}
			if (response.resultCode == kMailNotFound)
			{
				m_metrics.RecordExpectedRejection(operationName, now - pending.sentAt);
				user.state = EVirtualUserState::Ready;
				if (user.mailDetailNotFoundRetryCount < kMailDetailMaxNotFoundRetries)
				{
					++user.mailDetailNotFoundRetryCount;
					user.nextActionAt =
						now + (user.mailDetailNotFoundRetryCount == 1 ? kMailDetailFirstRetryDelay : kMailDetailSecondRetryDelay);
				}
				else
				{
					user.pendingMailId = 0;
					user.mailDetailNotFoundRetryCount = 0;
					user.nextActionAt = now;
				}
				return;
			}
			const bool succeeded = response.resultCode == kSuccess && response.mailId != 0;
			m_metrics.RecordResponse(operationName, succeeded, now - pending.sentAt);
			if (!succeeded)
			{
				user.pendingMailId = 0;
				user.mailDetailNotFoundRetryCount = 0;
				MarkUserFailed(user, "MailDetail rejected. resultCode=" + std::to_string(response.resultCode), false);
				return;
			}
			user.pendingMailId = 0;
			user.mailDetailNotFoundRetryCount = 0;
			for (std::size_t index = 0; index < response.attachmentIds.size(); ++index)
			{
				if (response.attachmentStates[index] == kClaimableAttachmentState)
					user.mailAttachmentCandidates.push_back({response.mailId, response.attachmentIds[index]});
			}
			user.state = EVirtualUserState::Ready;
			user.nextActionAt = now;
			return;
		}

		if (pending.operation == ELoadTestOperation::MailClaim)
		{
			Generated::Auction::FMailClaimRp response;
			const bool valid = ClientNetworkLib::TryDeserializePacketEvent(event, response) && response.requestId == pending.requestId;
			user.pendingRequest.reset();
			user.pendingMailAttachment = {};
			if (!valid)
			{
				m_metrics.RecordResponse(operationName, false, now - pending.sentAt);
				MarkUserFailed(user, "invalid MailClaim response", false);
				return;
			}
			if (response.resultCode == kMailAttachmentNotClaimable)
			{
				m_metrics.RecordExpectedRejection(operationName, now - pending.sentAt);
				user.state = EVirtualUserState::Ready;
				user.nextActionAt = now;
				return;
			}
			const bool succeeded = response.resultCode == kSuccess && response.attachmentId != 0;
			m_metrics.RecordResponse(operationName, succeeded, now - pending.sentAt);
			if (!succeeded)
			{
				MarkUserFailed(user, "MailClaim rejected. resultCode=" + std::to_string(response.resultCode), false);
				return;
			}
			if (response.attachmentType == 1)
				user.needsInventoryRefresh = true;
			else if (response.attachmentType == 2)
				user.currencyBalance = response.currencyBalance;
			user.state = EVirtualUserState::Ready;
			user.nextActionAt = now;
			return;
		}

		Generated::Auction::FListingRegisterRp response;
		const bool valid = ClientNetworkLib::TryDeserializePacketEvent(event, response) && response.requestId == pending.requestId;
		const bool succeeded = valid && response.resultCode == kSuccess && response.listingId != 0;
		m_metrics.RecordResponse(operationName, succeeded, now - pending.sentAt);
		user.pendingRequest.reset();
		if (!succeeded)
		{
			MarkUserFailed(user,
				valid ? "ListingRegister rejected. resultCode=" + std::to_string(response.resultCode) : "invalid ListingRegister response",
				false);
			return;
		}
		++user.activeListingCount;
		std::erase_if(user.inventoryItems,
			[&user](const SInventoryItem& item)
			{
				return item.itemInstanceId == user.pendingRegistrationItemInstanceId;
			});
		user.pendingRegistrationItemInstanceId = 0;
		user.state = EVirtualUserState::Ready;
		user.nextActionAt = now + MakeSearchInterval();
	}

	void FAuctionLoadTestRunner::ProcessEvents(
		const std::chrono::steady_clock::time_point now)
	{
		std::vector<ClientNetworkLib::FClientEvent> events;
		m_client->PollEvents(events, m_config.eventPollMaxCount);
		for (const ClientNetworkLib::FClientEvent& event : events)
		{
			if (event.Type == ClientNetworkLib::EClientEventType::PacketReceived)
			{
				ProcessPacketEvent(event, now);
				continue;
			}
			if (event.Type == ClientNetworkLib::EClientEventType::Connected)
				continue;

			const auto userIt = m_userIndexBySession.find(event.SessionId);
			if (userIt != m_userIndexBySession.end())
				MarkUserFailed(m_users[userIt->second], event.Message, true);
			else if (event.Type == ClientNetworkLib::EClientEventType::ConnectFailed)
				m_metrics.RecordConnectFailure();
		}
	}

	void FAuctionLoadTestRunner::ProcessTimeouts(
		const std::chrono::steady_clock::time_point now)
	{
		for (FVirtualAuctionUser& user : m_users)
		{
			if (!user.pendingRequest || now < user.pendingRequest->deadline)
				continue;
			m_metrics.RecordTimeout(GetOperationName(user.pendingRequest->operation));
			MarkUserFailed(user, "response timeout", false);
		}
	}

	void FAuctionLoadTestRunner::ScheduleReadyUsers(
		const std::chrono::steady_clock::time_point now,
		const bool allowNewRequests)
	{
		if (!allowNewRequests)
			return;
		for (FVirtualAuctionUser& user : m_users)
		{
			if (user.state != EVirtualUserState::Ready || user.HasPendingRequest() || now < user.nextActionAt)
				continue;
			std::string error;
			if (user.needsMyListingsRefresh)
				SendMyListings(user, now, error);
			else if (user.needsInventoryRefresh)
				SendInventoryList(user, now, error);
			else if (user.needsInitialGoldCheat)
				SendGoldCheat(user, now, error);
			else if (!user.mailAttachmentCandidates.empty())
				SendMailClaim(user, now, error);
			else if (user.pendingMailId != 0 || !user.wonMailIds.empty())
				SendMailDetail(user, now, error);
			else if (user.needsMyBidsRefresh)
				SendMyBids(user, now, error);
			else if (!user.bidRefundCandidates.empty())
				SendBidRefund(user, now, error);
			else if (!user.outbidListingIds.empty())
				SendListingDetail(user, now, error);
			else if (user.pendingBidCandidate)
			{
				if (!SendBid(user, now, error))
				{
					user.state = EVirtualUserState::Ready;
					user.nextActionAt = now + MakeSearchInterval();
				}
			}
			else
			{
				const bool canRegister = user.activeListingCount < user.maxActiveListings && m_config.registerWeight != 0;
				const bool canBid = !user.listingCandidates.empty() && m_config.bidWeight != 0;
				const std::uint32_t totalWeight =
					m_config.searchWeight + (canRegister ? m_config.registerWeight : 0) + (canBid ? m_config.bidWeight : 0);
				std::uniform_int_distribution<std::uint32_t> actionDistribution(1, totalWeight);
				const std::uint32_t action = actionDistribution(m_random);
				if (action <= m_config.searchWeight)
					SendSearch(user, now, error);
				else if (canRegister && action <= m_config.searchWeight + m_config.registerWeight)
					SendListingRegister(user, now, error);
				else
				{
					std::uniform_int_distribution<std::uint32_t> hotspotDistribution(1, 100);
					if (hotspotDistribution(m_random) <= m_config.bidHotspotPercent)
						user.pendingBidCandidate = user.listingCandidates.front();
					else
					{
						std::uniform_int_distribution<std::size_t> candidateDistribution(0, user.listingCandidates.size() - 1);
						user.pendingBidCandidate = user.listingCandidates[candidateDistribution(m_random)];
					}
					if (!SendBid(user, now, error))
					{
						user.state = EVirtualUserState::Ready;
						user.nextActionAt = now + MakeSearchInterval();
					}
				}
			}
		}
	}

	void FAuctionLoadTestRunner::MarkUserFailed(
		FVirtualAuctionUser& user,
		const std::string_view reason,
		const bool networkFailure)
	{
		if (user.state == EVirtualUserState::Failed)
			return;
		if (networkFailure)
			m_metrics.RecordNetworkFailure();
		std::cerr << "AUCTION_LOAD_TEST_USER_FAILED sessionId=" << user.sessionId << " userId=" << user.authenticatedUserId
				  << " reason=" << reason << '\n';
		user.pendingRequest.reset();
		user.state = EVirtualUserState::Failed;
		if (user.sessionId != 0)
			m_client->DisconnectSession(user.sessionId, std::string(reason));
	}

	std::chrono::milliseconds FAuctionLoadTestRunner::MakeSearchInterval()
	{
		std::uniform_int_distribution<std::int64_t> distribution(m_config.searchIntervalMin.count(), m_config.searchIntervalMax.count());
		return std::chrono::milliseconds(distribution(m_random));
	}

	bool FAuctionLoadTestRunner::IsAuthenticatedLoadUser(
		const std::uint64_t userId) const noexcept
	{
		return m_authenticatedUserIds.contains(userId);
	}

	void FAuctionLoadTestRunner::CacheBidCandidates(
		FVirtualAuctionUser& user,
		const Generated::Auction::FListingSearchRp& response)
	{
		user.listingCandidates.clear();
		for (std::size_t index = 0; index < response.listingIds.size(); ++index)
		{
			if (response.sellerUserIds[index] == user.authenticatedUserId || !IsAuthenticatedLoadUser(response.sellerUserIds[index]) ||
				user.highestBidListingIds.contains(response.listingIds[index]))
			{
				continue;
			}
			user.listingCandidates.push_back(SListingCandidate{response.listingIds[index],
				response.sellerUserIds[index],
				response.startPrices[index],
				response.currentBidPrices[index],
				response.buyoutPrices[index],
				response.versions[index]});
		}
		std::sort(user.listingCandidates.begin(),
			user.listingCandidates.end(),
			[](const SListingCandidate& left, const SListingCandidate& right)
			{
				return left.listingId < right.listingId;
			});
	}

	std::size_t FAuctionLoadTestRunner::CountConnectedUsers() const noexcept
	{
		return std::count_if(m_users.begin(),
			m_users.end(),
			[](const FVirtualAuctionUser& user)
			{
				return user.sessionId != 0 && user.state != EVirtualUserState::Failed;
			});
	}

	std::size_t FAuctionLoadTestRunner::CountAuthenticatedUsers() const noexcept
	{
		return std::count_if(m_users.begin(),
			m_users.end(),
			[](const FVirtualAuctionUser& user)
			{
				return user.authenticatedUserId != 0 && user.state != EVirtualUserState::Failed;
			});
	}

	bool FAuctionLoadTestRunner::HasPendingRequests() const noexcept
	{
		return std::any_of(m_users.begin(),
			m_users.end(),
			[](const FVirtualAuctionUser& user)
			{
				return user.HasPendingRequest();
			});
	}

	bool FAuctionLoadTestRunner::Run(
		std::string& outError)
	{
		if (!LoadTickets(outError))
			return false;

		ClientNetworkLib::FClientNetworkConfig networkConfig{};
		networkConfig.ServerIp = m_config.serverIp;
		networkConfig.ServerPort = m_config.port;
		networkConfig.WorkerThreadCount = m_config.workerThreadCount;
		networkConfig.PacketCipherConfig.packetKey = m_config.packetKey;
		m_client = std::make_unique<ClientNetworkLib::FClientNetwork>(networkConfig);
		if (!m_client->Start(outError))
			return false;

		m_users.reserve(m_config.virtualUserCount);
		for (std::size_t index = 0; index < m_config.virtualUserCount; ++index)
			m_users.emplace_back(m_tickets[index]);
		m_userIndexBySession.reserve(m_config.virtualUserCount);

		const auto startedAt = std::chrono::steady_clock::now();
		const auto connectInterval = std::chrono::microseconds(1'000'000 / m_config.connectsPerSecond);
		auto nextConnectAt = startedAt;
		auto nextSummaryAt = startedAt + m_config.consoleSummaryInterval;
		std::optional<std::chrono::steady_clock::time_point> steadyDeadline;
		std::size_t nextUserIndex = 0;

		while (true)
		{
			const auto now = std::chrono::steady_clock::now();
			while (nextUserIndex < m_users.size() && now >= nextConnectAt)
			{
				std::string connectError;
				ConnectNextUser(nextUserIndex, connectError);
				++nextUserIndex;
				nextConnectAt += connectInterval;
			}

			ProcessEvents(now);
			ProcessTimeouts(now);

			if (!steadyDeadline && nextUserIndex == m_users.size())
			{
				const bool authenticationFinished = std::none_of(m_users.begin(),
					m_users.end(),
					[](const FVirtualAuctionUser& user)
					{
						return user.state == EVirtualUserState::Authenticating;
					});
				if (authenticationFinished)
				{
					steadyDeadline = now + m_config.runDuration;
					std::cout << "AUCTION_LOAD_TEST_STEADY_STARTED users=" << CountAuthenticatedUsers()
							  << " runSeconds=" << m_config.runDuration.count() << '\n';
				}
			}

			const bool allowNewRequests = !steadyDeadline || now < *steadyDeadline;
			ScheduleReadyUsers(now, allowNewRequests);

			if (now >= nextSummaryAt)
			{
				m_metrics.PrintSummary(now - startedAt, CountConnectedUsers(), CountAuthenticatedUsers(), false);
				nextSummaryAt = now + m_config.consoleSummaryInterval;
			}

			if (steadyDeadline && now >= *steadyDeadline && !HasPendingRequests())
				break;
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}

		const auto finishedAt = std::chrono::steady_clock::now();
		m_metrics.PrintSummary(finishedAt - startedAt, CountConnectedUsers(), CountAuthenticatedUsers(), true);
		m_client->Stop();
		if (CountAuthenticatedUsers() == 0)
		{
			outError = "no virtual user completed authentication.";
			return false;
		}
		if (m_metrics.GetUnexpectedFailureCount() != 0)
		{
			outError = "load test completed with unexpected failures=" + std::to_string(m_metrics.GetUnexpectedFailureCount());
			return false;
		}
		return true;
	}

	bool LoadAuctionLoadTestConfig(
		const std::filesystem::path& configPath,
		SAuctionLoadTestConfig& outConfig,
		std::string& outError)
	{
		Generated::Config::AuctionDummyClient::FAuctionDummyClientConfigDocument document{};
		if (!Generated::Config::AuctionDummyClient::FAuctionDummyClientConfigLoader::LoadFromFile(configPath, document, outError))
			return false;

		const auto& source = document.AuctionDummy;
		if (source.PacketKey > std::numeric_limits<std::uint8_t>::max() || source.WorkerThreadCount <= 0 || source.VirtualUserCount <= 0 ||
			source.ConnectsPerSecond <= 0 || source.RunSeconds <= 0 || source.SearchIntervalMinMs < 0 ||
			source.SearchIntervalMaxMs < source.SearchIntervalMinMs || source.ResponseTimeoutMs <= 0 ||
			source.ConsoleSummaryIntervalSeconds <= 0 || source.EventPollMaxCount <= 0 || source.RandomStatMaximum < 0 ||
			source.SearchWeight <= 0 || source.SearchWeight > 100000 || source.RegisterWeight < 0 || source.RegisterWeight > 100000 ||
			source.BidWeight < 0 || source.BidWeight > 100000 || source.InitialGoldAmount == 0 || source.BidIncrementMinimum == 0 ||
			source.BidIncrementMaximum < source.BidIncrementMinimum || source.BidHotspotPercent < 0 || source.BidHotspotPercent > 100 ||
			source.OutbidRefundPercent < 0 || source.OutbidRefundPercent > 100 || source.InventoryListLimit <= 0 ||
			source.InventoryListLimit > 20 || source.ListingStartPriceMinimum == 0 ||
			source.ListingStartPriceMaximum < source.ListingStartPriceMinimum || source.ListingBuyoutMarkupMinimum == 0 ||
			source.ListingBuyoutMarkupMaximum < source.ListingBuyoutMarkupMinimum)
		{
			outError = "AuctionDummy load-test configuration contains an invalid range.";
			return false;
		}

		outConfig.serverIp = source.ServerIp;
		outConfig.port = source.Port;
		outConfig.packetKey = static_cast<std::uint8_t>(source.PacketKey);
		outConfig.workerThreadCount = static_cast<std::uint32_t>(source.WorkerThreadCount);
		outConfig.virtualUserCount = static_cast<std::size_t>(source.VirtualUserCount);
		outConfig.connectsPerSecond = static_cast<std::uint32_t>(source.ConnectsPerSecond);
		outConfig.runDuration = std::chrono::seconds(source.RunSeconds);
		outConfig.searchIntervalMin = std::chrono::milliseconds(source.SearchIntervalMinMs);
		outConfig.searchIntervalMax = std::chrono::milliseconds(source.SearchIntervalMaxMs);
		outConfig.responseTimeout = std::chrono::milliseconds(source.ResponseTimeoutMs);
		outConfig.consoleSummaryInterval = std::chrono::seconds(source.ConsoleSummaryIntervalSeconds);
		outConfig.eventPollMaxCount = static_cast<std::size_t>(source.EventPollMaxCount);
		outConfig.randomStatMaximum = static_cast<std::uint32_t>(source.RandomStatMaximum);
		outConfig.searchWeight = static_cast<std::uint32_t>(source.SearchWeight);
		outConfig.registerWeight = static_cast<std::uint32_t>(source.RegisterWeight);
		outConfig.bidWeight = static_cast<std::uint32_t>(source.BidWeight);
		outConfig.initialGoldAmount = source.InitialGoldAmount;
		outConfig.bidIncrementMinimum = source.BidIncrementMinimum;
		outConfig.bidIncrementMaximum = source.BidIncrementMaximum;
		outConfig.bidHotspotPercent = static_cast<std::uint32_t>(source.BidHotspotPercent);
		outConfig.outbidRefundPercent = static_cast<std::uint32_t>(source.OutbidRefundPercent);
		outConfig.inventoryListLimit = static_cast<std::uint32_t>(source.InventoryListLimit);
		if (!TryParseItemDataIds(source.CheatItemDataIds, outConfig.cheatItemDataIds))
		{
			outError = "CheatItemDataIds must contain comma-separated positive integers.";
			return false;
		}
		outConfig.listingStartPriceMinimum = source.ListingStartPriceMinimum;
		outConfig.listingStartPriceMaximum = source.ListingStartPriceMaximum;
		outConfig.listingBuyoutMarkupMinimum = source.ListingBuyoutMarkupMinimum;
		outConfig.listingBuyoutMarkupMaximum = source.ListingBuyoutMarkupMaximum;
		outConfig.randomSeed = source.RandomSeed;
		outConfig.ticketFilePath = std::filesystem::path(source.TicketFilePath);
		if (outConfig.ticketFilePath.is_relative())
			outConfig.ticketFilePath = configPath.parent_path() / outConfig.ticketFilePath;
		return true;
	}

	int RunAuctionLoadTest(
		const std::filesystem::path& configPath)
	{
		SAuctionLoadTestConfig config{};
		std::string error;
		if (!LoadAuctionLoadTestConfig(configPath, config, error))
		{
			std::cerr << "load-test config failed: " << error << '\n';
			return 1;
		}
		FAuctionLoadTestRunner runner(std::move(config));
		if (!runner.Run(error))
		{
			std::cerr << "auction load test failed: " << error << '\n';
			return 1;
		}
		std::cout << "AUCTION_LOAD_TEST_SUCCESS\n";
		return 0;
	}
}
