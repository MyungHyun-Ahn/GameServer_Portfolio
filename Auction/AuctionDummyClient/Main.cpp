#include "AuctionDummyClientPch.h"

#include "ClientNetworkLib/FClientNetwork.h"
#include "Generated/Packets/Cpp/Auction/AuctionPackets.h"
#include "LoadTest/FAuctionLoadTestRunner.h"

#include <algorithm>
#include <barrier>
#include <cstdlib>
#include <unordered_map>
#include <unordered_set>

namespace
{
	constexpr std::uint8_t kPacketKey = 0x37;
	constexpr std::uint8_t kRandomKey = 0x5A;
	constexpr std::uint16_t kDefaultPort = 19100;
	constexpr std::uint16_t kSuccess = 0;
	constexpr std::uint16_t kServerBusy = 1;
	constexpr std::uint16_t kDatabaseUnavailable = 3;
	constexpr std::uint16_t kBidNotClaimable = 4;
	constexpr std::uint16_t kAuthRequired = 8;
	constexpr std::uint16_t kAuthenticationFailed = 9;
	constexpr std::uint16_t kAlreadyAuthenticated = 10;
	constexpr std::uint16_t kInventoryItemNotFound = 11;
	constexpr std::uint16_t kItemVersionMismatch = 13;
	constexpr std::uint16_t kListingNotFound = 15;
	constexpr std::uint16_t kInsufficientCurrency = 17;
	constexpr std::uint16_t kListingVersionMismatch = 19;
	constexpr std::uint16_t kBidRequiresBuyout = 33;
	constexpr std::uint16_t kMailNotFound = 22;
	constexpr std::uint16_t kMailAttachmentNotClaimable = 23;
	constexpr std::uint16_t kHighestBidExists = 28;
	constexpr std::array<std::uint64_t, 6> kRoutingKeys = {101, 202, 303, 404, 101, 202};
	constexpr std::size_t kSaturationRequestCount = 12;
	constexpr std::uint64_t kSlowShardRoutingKey = 100;
	constexpr std::uint64_t kIsolationRoutingKey = 101;

	struct SCommandLineOptions
	{
		std::uint16_t port = kDefaultPort;
		bool backpressureTest = false;
		bool databaseFlowTest = false;
		bool cacheReadRpcTest = false;
		bool cacheWriteRpcTest = false;
		bool bidRefundFaultTest = false;
		bool concurrencyTest = false;
		bool reconnectTest = false;
		bool loadTest = false;
		std::filesystem::path loadTestConfigPath;
		std::string ticket;
		std::string outbidTicket;
		std::string sellerTicket;
		std::vector<std::string> concurrencyTickets;
		std::uint64_t expirationRaceAtUnixMs = 0;
	};

	struct SObservedRoute
	{
		std::uint32_t shardIndex = 0;
		std::uint64_t contentInstanceId = 0;
		std::uint32_t contentThreadId = 0;
	};

	struct SRequest
	{
		std::uint64_t requestId = 0;
		std::uint64_t routingKey = 0;
	};

	std::uint64_t GetUnixTimeMilliseconds() noexcept
	{
		return static_cast<std::uint64_t>(
			std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
	}

	bool TryParseOptions(
		const int argc,
		char* argv[],
		SCommandLineOptions& outOptions)
	{
		for (int index = 1; index < argc; ++index)
		{
			const std::string_view argument = argv[index];
			if (argument == "--port" && index + 1 < argc)
			{
				const int port = std::atoi(argv[++index]);
				if (port <= 0 || port > 65535)
				{
					return false;
				}
				outOptions.port = static_cast<std::uint16_t>(port);
			}
			else if (argument == "--backpressure-test")
			{
				outOptions.backpressureTest = true;
			}
			else if (argument == "--database-flow-test")
			{
				outOptions.databaseFlowTest = true;
			}
			else if (argument == "--cache-read-rpc-test")
			{
				outOptions.cacheReadRpcTest = true;
			}
			else if (argument == "--cache-write-rpc-test")
			{
				outOptions.cacheWriteRpcTest = true;
			}
			else if (argument == "--bid-refund-fault-test")
			{
				outOptions.bidRefundFaultTest = true;
			}
			else if (argument == "--concurrency-test")
			{
				outOptions.concurrencyTest = true;
			}
			else if (argument == "--reconnect-test")
			{
				outOptions.reconnectTest = true;
			}
			else if (argument == "--load-test")
			{
				outOptions.loadTest = true;
			}
			else if (argument == "--config" && index + 1 < argc)
			{
				outOptions.loadTestConfigPath = argv[++index];
			}
			else if (argument == "--concurrency-ticket" && index + 1 < argc)
			{
				outOptions.concurrencyTickets.emplace_back(argv[++index]);
			}
			else if (argument == "--expiration-race-at-unix-ms" && index + 1 < argc)
			{
				outOptions.expirationRaceAtUnixMs = std::strtoull(argv[++index], nullptr, 10);
				if (outOptions.expirationRaceAtUnixMs == 0)
					return false;
			}
			else if (argument == "--ticket" && index + 1 < argc)
			{
				outOptions.ticket = argv[++index];
			}
			else if (argument == "--outbid-ticket" && index + 1 < argc)
			{
				outOptions.outbidTicket = argv[++index];
			}
			else if (argument == "--seller-ticket" && index + 1 < argc)
			{
				outOptions.sellerTicket = argv[++index];
			}
			else
			{
				return false;
			}
		}

		return true;
	}

	template <typename TPacket>
	bool WaitForPacket(ClientNetworkLib::FClientNetwork& client,
		TPacket& outPacket,
		std::string& outError,
		const std::chrono::seconds timeout = std::chrono::seconds(8));

	bool RunReconnectFlow(
		ClientNetworkLib::FClientNetwork& client,
		const ClientNetworkLib::FClientSessionId sessionId,
		const std::string& ticket,
		std::string& outError)
	{
		constexpr std::uint64_t kListingId = 99200001;
		Generated::Auction::FAuctionAuthRq authRequest;
		authRequest.requestId = 1;
		authRequest.ticket = ticket;
		if (!client.SendPacket(sessionId, authRequest, kRandomKey, outError))
			return false;
		Generated::Auction::FAuctionAuthRp authResponse;
		if (!WaitForPacket(client, authResponse, outError) || authResponse.resultCode != kSuccess)
		{
			outError = "reconnect test authentication failed: " + outError;
			return false;
		}

		const auto readDetail = [&](const std::uint64_t requestId)
		{
			Generated::Auction::FListingDetailRq request;
			request.requestId = requestId;
			request.listingId = kListingId;
			if (!client.SendPacket(sessionId, request, kRandomKey, outError))
				return false;
			Generated::Auction::FListingDetailRp response;
			return WaitForPacket(client, response, outError) && response.resultCode == kSuccess && response.listingId == kListingId;
		};
		if (!readDetail(10))
		{
			outError = "baseline replica read failed: " + outError;
			return false;
		}
		std::cout << "RECONNECT_FIRST_REPLICA_STOP_READY" << std::endl;
		std::this_thread::sleep_for(std::chrono::seconds(3));
		if (!readDetail(11))
		{
			outError = "secondary replica read fallback failed: " + outError;
			return false;
		}
		std::cout << "RECONNECT_SECOND_REPLICA_STOP_READY" << std::endl;
		std::this_thread::sleep_for(std::chrono::seconds(3));
		if (!readDetail(12))
		{
			outError = "primary read fallback failed: " + outError;
			return false;
		}
		std::cout << "RECONNECT_ALL_REPLICAS_FALLBACK_SUCCESS" << std::endl;
		std::this_thread::sleep_for(std::chrono::seconds(15));
		if (!readDetail(13))
		{
			outError = "replica recovery read failed: " + outError;
			return false;
		}
		std::cout << "RECONNECT_MASTER_STOP_READY" << std::endl;
		std::this_thread::sleep_for(std::chrono::seconds(3));

		Generated::Auction::FBidRq bidRequest;
		bidRequest.requestId = 20;
		bidRequest.listingId = kListingId;
		bidRequest.bidAmount = 1500;
		bidRequest.expectedListingVersion = 1;
		if (!client.SendPacket(sessionId, bidRequest, kRandomKey, outError))
			return false;
		Generated::Auction::FBidRp failedBidResponse;
		if (!WaitForPacket(client, failedBidResponse, outError) || failedBidResponse.resultCode != kDatabaseUnavailable)
		{
			outError = "master outage did not return DATABASE_UNAVAILABLE: " + outError;
			return false;
		}
		std::cout << "RECONNECT_MASTER_FAILURE_OBSERVED" << std::endl;
		std::this_thread::sleep_for(std::chrono::seconds(15));

		bidRequest.requestId = 21;
		if (!client.SendPacket(sessionId, bidRequest, kRandomKey, outError))
			return false;
		Generated::Auction::FBidRp recoveredBidResponse;
		if (!WaitForPacket(client, recoveredBidResponse, outError) || recoveredBidResponse.resultCode != kSuccess ||
			recoveredBidResponse.bidAmount != 1500)
		{
			outError = "next request did not reconnect to master: " + outError;
			return false;
		}
		std::cout << "AUCTION_DB_RECONNECT_TEST_SUCCESS secondaryReplicaFallback=1 primaryReadFallback=1 replicaReconnected=1"
				  << " masterCurrentRequestFailed=1 masterNextRequestRecovered=1" << std::endl;
		return true;
	}

	int RunBidRefundFaultTest(
		ClientNetworkLib::FClientNetwork& client,
		const ClientNetworkLib::FClientSessionId sessionId,
		const std::string& ticket,
		std::string& outError)
	{
		constexpr std::uint64_t kUserId = 940030001;
		constexpr std::uint64_t kListingId = 99300001;
		constexpr std::uint64_t kBidId = 77300001;
		constexpr std::uint64_t kExpectedBidVersion = 1;

		Generated::Auction::FAuctionAuthRq authRequest;
		authRequest.requestId = 1;
		authRequest.ticket = ticket;
		if (!client.SendPacket(sessionId, authRequest, kRandomKey, outError))
		{
			return 1;
		}

		Generated::Auction::FAuctionAuthRp authResponse;
		if (!WaitForPacket(client, authResponse, outError) || authResponse.resultCode != kSuccess || authResponse.userId != kUserId ||
			authResponse.searchPageSize == 0 || authResponse.inventoryListPageSize == 0 || authResponse.mailListPageSize == 0 ||
			authResponse.defaultCurrencyId == 0 || authResponse.minimumBidIncrement == 0 || authResponse.minimumListingPrice == 0 ||
			authResponse.minimumListingPrice > authResponse.maximumListingPrice)
		{
			outError = "bid refund fault test authentication failed: " + outError;
			return 1;
		}

		Generated::Auction::FBidRefundRq refundRequest;
		refundRequest.requestId = 2;
		refundRequest.listingId = kListingId;
		refundRequest.bidId = kBidId;
		refundRequest.expectedBidVersion = kExpectedBidVersion;
		if (!client.SendPacket(sessionId, refundRequest, kRandomKey, outError))
		{
			std::cerr << "BID_REFUND_FAULT_NO_RESPONSE error=" << outError << '\n';
			return 2;
		}

		Generated::Auction::FBidRefundRp refundResponse;
		if (!WaitForPacket(client, refundResponse, outError))
		{
			std::cerr << "BID_REFUND_FAULT_NO_RESPONSE error=" << outError << '\n';
			return 2;
		}

		std::cout << "BID_REFUND_FAULT_RESULT"
				  << " resultCode=" << refundResponse.resultCode << " bidId=" << refundResponse.bidId
				  << " refundedAmount=" << refundResponse.refundedAmount << " currencyBalance=" << refundResponse.currencyBalance
				  << " bidState=" << static_cast<std::uint32_t>(refundResponse.bidState) << " bidVersion=" << refundResponse.bidVersion
				  << '\n';
		return 0;
	}

	template <typename TPacket>
	bool TryReadPacketForSession(
		const ClientNetworkLib::FClientEvent& event,
		const ClientNetworkLib::FClientSessionId sessionId,
		TPacket& outPacket)
	{
		return event.Type == ClientNetworkLib::EClientEventType::PacketReceived && event.SessionId == sessionId &&
			   event.Packet.Opcode == TPacket::kOpcode && ClientNetworkLib::TryDeserializePacketEvent(event, outPacket);
	}

	template <typename TPacket>
	bool WaitForPacket(
		ClientNetworkLib::FClientNetwork& client,
		TPacket& outPacket,
		std::string& outError,
		const std::chrono::seconds timeout)
	{
		const auto deadline = std::chrono::steady_clock::now() + timeout;
		while (std::chrono::steady_clock::now() < deadline)
		{
			ClientNetworkLib::FClientEvent event{};
			while (client.TryPopEvent(event))
			{
				if (event.Type == ClientNetworkLib::EClientEventType::PacketReceived && event.Packet.Opcode == TPacket::kOpcode)
				{
					if (!ClientNetworkLib::TryDeserializePacketEvent(event, outPacket))
					{
						outError = "packet deserialize failed.";
						return false;
					}
					return true;
				}
				if (event.Type == ClientNetworkLib::EClientEventType::ConnectFailed ||
					event.Type == ClientNetworkLib::EClientEventType::SendFailed ||
					event.Type == ClientNetworkLib::EClientEventType::SessionError)
				{
					outError = event.Message;
					return false;
				}
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
		outError = "packet response timeout.";
		return false;
	}

	bool RunDatabaseFlow(
		ClientNetworkLib::FClientNetwork& client,
		const ClientNetworkLib::FClientSessionId sessionId,
		const std::string& ticket,
		const std::string& outbidTicket,
		const std::string& sellerTicket,
		std::string& outError)
	{
		// Reserved identities for Run-DatabaseFlowSmoke.ps1. Keeping these users in
		// a dedicated high range lets the smoke test clean up only its own rows.
		constexpr std::uint64_t kUserId = 940020001;
		constexpr std::uint64_t kOutbidUserId = 940020002;
		constexpr std::uint64_t kSellerUserId = 940020005;
		// Deliberately maps to a different modulo than kUserId so the smoke test can
		// detect an accidental return to listingId-based routing.
		constexpr std::uint64_t kListingId = 99000002;
		constexpr std::uint64_t kBidId = 77000001;
		constexpr std::uint64_t kRefundAmount = 1500;
		constexpr std::uint64_t kExpectedBalance = 6500;
		constexpr std::uint64_t kBidListingId = 99000003;
		constexpr std::uint64_t kCatchUpListingId = 99000004;
		constexpr std::uint64_t kOtherBuyoutListingId = 99000005;

		Generated::Auction::FMyBidListRq listRequest;
		listRequest.requestId = 1;
		listRequest.limit = 10;
		if (!client.SendPacket(sessionId, listRequest, kRandomKey, outError))
		{
			return false;
		}
		Generated::Auction::FMyBidListRp unauthenticatedResponse;
		if (!WaitForPacket(client, unauthenticatedResponse, outError) || unauthenticatedResponse.resultCode != kAuthRequired)
		{
			outError = "unauthenticated request was not rejected: " + outError;
			return false;
		}

		Generated::Auction::FAuctionAuthRq authRequest;
		authRequest.requestId = 2;
		authRequest.ticket = ticket;
		if (!client.SendPacket(sessionId, authRequest, kRandomKey, outError))
		{
			return false;
		}
		Generated::Auction::FAuctionAuthRp authResponse;
		if (!WaitForPacket(client, authResponse, outError) || authResponse.resultCode != kSuccess || authResponse.userId != kUserId ||
			authResponse.searchPageSize == 0 || authResponse.inventoryListPageSize == 0 || authResponse.mailListPageSize == 0 ||
			authResponse.defaultCurrencyId == 0 || authResponse.minimumBidIncrement == 0 || authResponse.minimumListingPrice == 0 ||
			authResponse.minimumListingPrice > authResponse.maximumListingPrice)
		{
			outError = "AuctionAuth validation failed: " + outError;
			return false;
		}

		authRequest.requestId = 3;
		if (!client.SendPacket(sessionId, authRequest, kRandomKey, outError))
		{
			return false;
		}

		Generated::Auction::FAuctionAuthRp repeatedAuthResponse;
		if (!WaitForPacket(client, repeatedAuthResponse, outError) || repeatedAuthResponse.resultCode != kAlreadyAuthenticated ||
			repeatedAuthResponse.userId != kUserId)
		{
			outError = "authenticated session was allowed to authenticate again: " + outError;
			return false;
		}

		Generated::Auction::FInventoryListRq inventoryRequest;
		inventoryRequest.requestId = 4;
		inventoryRequest.limit = authResponse.inventoryListPageSize;
		if (!client.SendPacket(sessionId, inventoryRequest, kRandomKey, outError))
		{
			return false;
		}
		Generated::Auction::FInventoryListRp inventoryResponse;
		if (!WaitForPacket(client, inventoryResponse, outError) || inventoryResponse.resultCode != kSuccess ||
			inventoryResponse.itemInstanceIds.size() != 3 ||
			inventoryResponse.itemDataIds != std::vector<std::uint32_t>{3001, 2001, 1001} ||
			inventoryResponse.quantities != std::vector<std::uint32_t>{50, 20, 1} ||
			inventoryResponse.tradableStates != std::vector<std::uint8_t>{1, 1, 1} ||
			inventoryResponse.versions != std::vector<std::uint64_t>{1, 1, 1})
		{
			outError = "InventoryList validation failed: " + outError;
			return false;
		}

		Generated::Auction::FListingRegisterRq registerRequest;
		registerRequest.requestId = 5;
		registerRequest.itemInstanceId = inventoryResponse.itemInstanceIds[2];
		registerRequest.expectedItemVersion = inventoryResponse.versions[2];
		registerRequest.currencyId = authResponse.defaultCurrencyId;
		registerRequest.startPrice = std::clamp<std::uint64_t>(1000, authResponse.minimumListingPrice, authResponse.maximumListingPrice);
		registerRequest.buyoutPrice = std::clamp<std::uint64_t>(5000, registerRequest.startPrice, authResponse.maximumListingPrice);
		registerRequest.durationSeconds = authResponse.defaultListingDurationSeconds;
		auto staleRegisterRequest = registerRequest;
		staleRegisterRequest.expectedItemVersion += 1;
		if (!client.SendPacket(sessionId, staleRegisterRequest, kRandomKey, outError))
		{
			return false;
		}
		Generated::Auction::FListingRegisterRp staleRegisterResponse;
		if (!WaitForPacket(client, staleRegisterResponse, outError) || staleRegisterResponse.resultCode != kItemVersionMismatch)
		{
			outError = "stale ListingRegister version was not rejected. result=" + std::to_string(staleRegisterResponse.resultCode) +
					   " error=" + outError;
			return false;
		}

		registerRequest.requestId = 6;
		if (!client.SendPacket(sessionId, registerRequest, kRandomKey, outError))
		{
			return false;
		}
		Generated::Auction::FListingRegisterRp registerResponse;
		if (!WaitForPacket(client, registerResponse, outError) || registerResponse.resultCode != kSuccess ||
			registerResponse.listingId == 0)
		{
			outError = "ListingRegister validation failed. result=" + std::to_string(registerResponse.resultCode) +
					   " listingId=" + std::to_string(registerResponse.listingId) + " error=" + outError;
			return false;
		}

		registerRequest.requestId = 7;
		if (!client.SendPacket(sessionId, registerRequest, kRandomKey, outError))
		{
			return false;
		}
		Generated::Auction::FListingRegisterRp repeatedRegisterResponse;
		if (!WaitForPacket(client, repeatedRegisterResponse, outError) || repeatedRegisterResponse.resultCode != kInventoryItemNotFound)
		{
			outError = "duplicate ListingRegister was not rejected: " + outError;
			return false;
		}

		Generated::Auction::FListingSearchRq searchRequest;
		searchRequest.requestId = 8;
		searchRequest.sortType = 1;
		searchRequest.itemCategory = 1;
		searchRequest.itemDataIds = {1001};
		searchRequest.minStr = 10;
		if (authResponse.searchPageSize == 0 || authResponse.searchPageSize >= 100)
		{
			outError = "AuctionAuth returned an invalid search page size.";
			return false;
		}
		searchRequest.limit = authResponse.searchPageSize;
		Generated::Auction::FListingSearchRp searchResponse;
		bool registeredListingVisible = false;
		for (int attempt = 0; attempt < 3; ++attempt)
		{
			if (attempt > 0)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(attempt * 500));
			}
			searchRequest.requestId = static_cast<std::uint64_t>(800 + attempt);
			Generated::Auction::FListingSearchRp attemptResponse;
			if (!client.SendPacket(sessionId, searchRequest, kRandomKey, outError) || !WaitForPacket(client, attemptResponse, outError) ||
				attemptResponse.resultCode != kSuccess)
			{
				return false;
			}
			searchResponse = std::move(attemptResponse);
			registeredListingVisible = !searchResponse.listingIds.empty() && searchResponse.listingIds[0] == registerResponse.listingId;
			if (registeredListingVisible)
			{
				break;
			}
		}
		if (!registeredListingVisible || searchResponse.listingIds.empty() || searchResponse.listingIds[0] != registerResponse.listingId ||
			searchResponse.itemDataIds[0] != 1001 || searchResponse.itemCategories[0] != 1 || searchResponse.names[0] != "Warrior Sword" ||
			searchResponse.strStats[0] != 12)
		{
			std::ostringstream oss;
			oss << "ListingSearch validation failed. result=" << searchResponse.resultCode << " count=" << searchResponse.listingIds.size();
			if (!searchResponse.listingIds.empty())
			{
				oss << " firstListingId=" << searchResponse.listingIds[0] << " expectedListingId=" << registerResponse.listingId
					<< " itemDataCount=" << searchResponse.itemDataIds.size() << " nameCount=" << searchResponse.names.size()
					<< " strCount=" << searchResponse.strStats.size();
			}
			outError = oss.str();
			return false;
		}

		searchRequest.requestId = 9;
		if (!std::is_sorted(searchResponse.listingIds.begin(), searchResponse.listingIds.end(), std::greater<>{}))
		{
			outError = "ListingSearch newest order validation failed.";
			return false;
		}
		searchRequest.cursorListingId = searchResponse.listingIds.back();
		if (!client.SendPacket(sessionId, searchRequest, kRandomKey, outError))
		{
			return false;
		}
		Generated::Auction::FListingSearchRp cursorSearchResponse;
		if (!WaitForPacket(client, cursorSearchResponse, outError) || cursorSearchResponse.resultCode != kSuccess ||
			!std::is_sorted(cursorSearchResponse.listingIds.begin(), cursorSearchResponse.listingIds.end(), std::greater<>{}) ||
			std::ranges::any_of(cursorSearchResponse.listingIds,
				[&](const std::uint64_t listingId)
				{
					return listingId >= searchRequest.cursorListingId ||
						   std::ranges::find(searchResponse.listingIds, listingId) != searchResponse.listingIds.end();
				}))
		{
			outError = "ListingSearch cursor validation failed: " + outError;
			return false;
		}

		searchRequest.requestId = 10;
		searchRequest.cursorListingId = 0;
		searchRequest.minStr = 13;
		if (!client.SendPacket(sessionId, searchRequest, kRandomKey, outError))
		{
			return false;
		}
		Generated::Auction::FListingSearchRp filteredSearchResponse;
		if (!WaitForPacket(client, filteredSearchResponse, outError) || filteredSearchResponse.resultCode != kSuccess ||
			filteredSearchResponse.listingIds.size() != filteredSearchResponse.strStats.size() ||
			std::ranges::find(filteredSearchResponse.listingIds, registerResponse.listingId) != filteredSearchResponse.listingIds.end() ||
			std::ranges::any_of(filteredSearchResponse.strStats,
				[](const std::uint32_t strength)
				{
					return strength < 13;
				}))
		{
			outError = "ListingSearch stat filter validation failed: " + outError;
			return false;
		}

		Generated::Auction::FListingDetailRq detailRequest;
		detailRequest.requestId = 11;
		detailRequest.listingId = registerResponse.listingId;
		if (!client.SendPacket(sessionId, detailRequest, kRandomKey, outError))
		{
			return false;
		}
		Generated::Auction::FListingDetailRp detailResponse;
		if (!WaitForPacket(client, detailResponse, outError) || detailResponse.resultCode != kSuccess ||
			detailResponse.listingId != registerResponse.listingId || detailResponse.sellerUserId != kUserId ||
			detailResponse.itemDataId != 1001 || detailResponse.itemCategory != 1 || detailResponse.quantity != 1 ||
			detailResponse.name != "Warrior Sword" || detailResponse.strStat != 12 || detailResponse.startPrice != 1000 ||
			detailResponse.buyoutPrice != 5000 || detailResponse.version != 2)
		{
			outError = "ListingDetail validation failed: " + outError;
			return false;
		}

		detailRequest.requestId = 12;
		detailRequest.listingId = 1;
		if (!client.SendPacket(sessionId, detailRequest, kRandomKey, outError))
		{
			return false;
		}
		Generated::Auction::FListingDetailRp missingDetailResponse;
		if (!WaitForPacket(client, missingDetailResponse, outError) || missingDetailResponse.resultCode != kListingNotFound)
		{
			outError = "missing ListingDetail was not rejected: " + outError;
			return false;
		}

		Generated::Auction::FInventoryListRp inventoryAfterRegister;
		bool inventoryRemovalVisible = false;
		for (int attempt = 0; attempt < 3; ++attempt)
		{
			if (attempt > 0)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(attempt * 500));
			}
			inventoryRequest.requestId = static_cast<std::uint64_t>(130 + attempt);
			Generated::Auction::FInventoryListRp attemptResponse;
			if (!client.SendPacket(sessionId, inventoryRequest, kRandomKey, outError) ||
				!WaitForPacket(client, attemptResponse, outError) || attemptResponse.resultCode != kSuccess)
			{
				return false;
			}
			inventoryAfterRegister = std::move(attemptResponse);
			inventoryRemovalVisible = inventoryAfterRegister.itemDataIds == std::vector<std::uint32_t>{3001, 2001};
			if (inventoryRemovalVisible)
			{
				break;
			}
		}
		if (!inventoryRemovalVisible)
		{
			outError = "inventory item was not removed after listing registration: " + outError;
			return false;
		}

		listRequest.requestId = 10;
		if (!client.SendPacket(sessionId, listRequest, kRandomKey, outError))
		{
			return false;
		}
		Generated::Auction::FMyBidListRp listResponse;
		if (!WaitForPacket(client, listResponse, outError) || listResponse.resultCode != kSuccess || listResponse.bidIds.size() != 1 ||
			listResponse.listingIds.size() != 1 || listResponse.bidStates.size() != 1 || listResponse.bidVersions.size() != 1 ||
			listResponse.bidIds[0] != kBidId || listResponse.listingIds[0] != kListingId || listResponse.bidStates[0] != 3 ||
			listResponse.bidVersions[0] != 1)
		{
			outError = "initial MyBidList validation failed: " + outError;
			return false;
		}

		Generated::Auction::FBidRefundRq refundRequest;
		refundRequest.requestId = 20;
		refundRequest.listingId = kListingId;
		refundRequest.bidId = kBidId;
		refundRequest.expectedBidVersion = listResponse.bidVersions[0];
		if (!client.SendPacket(sessionId, refundRequest, kRandomKey, outError))
		{
			return false;
		}
		Generated::Auction::FBidRefundRp refundResponse;
		if (!WaitForPacket(client, refundResponse, outError) || refundResponse.resultCode != kSuccess || refundResponse.bidId != kBidId ||
			refundResponse.refundedAmount != kRefundAmount || refundResponse.currencyBalance != kExpectedBalance ||
			refundResponse.bidState != 5 || refundResponse.bidVersion != 3)
		{
			outError = "BidRefund validation failed: " + outError;
			return false;
		}

		refundRequest.requestId = 21;
		if (!client.SendPacket(sessionId, refundRequest, kRandomKey, outError))
		{
			return false;
		}
		Generated::Auction::FBidRefundRp replayResponse;
		if (!WaitForPacket(client, replayResponse, outError) || replayResponse.resultCode != kBidNotClaimable)
		{
			outError = "duplicate BidRefund was not rejected: " + outError;
			return false;
		}

		std::uint64_t replicaReadAttempts = 0;
		for (std::uint64_t attempt = 0; attempt < 20; ++attempt)
		{
			listRequest.requestId = 100 + attempt;
			if (!client.SendPacket(sessionId, listRequest, kRandomKey, outError))
			{
				return false;
			}
			Generated::Auction::FMyBidListRp finalResponse;
			if (!WaitForPacket(client, finalResponse, outError))
			{
				return false;
			}
			if (finalResponse.resultCode == kSuccess && finalResponse.bidIds.size() == 1 && finalResponse.bidStates.size() == 1 &&
				finalResponse.bidVersions.size() == 1 && finalResponse.bidIds[0] == kBidId && finalResponse.bidStates[0] == 5 &&
				finalResponse.bidVersions[0] == 3)
			{
				replicaReadAttempts = attempt + 1;
				break;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}
		if (replicaReadAttempts == 0)
		{
			outError = "Auction replica did not observe the refunded bid state.";
			return false;
		}

		ClientNetworkLib::FClientSessionId outbidSessionId = 0;
		if (!client.ConnectSession(outbidSessionId, outError))
		{
			return false;
		}
		Generated::Auction::FAuctionAuthRq outbidAuthRequest;
		outbidAuthRequest.requestId = 200;
		outbidAuthRequest.ticket = outbidTicket;
		if (!client.SendPacket(outbidSessionId, outbidAuthRequest, kRandomKey, outError))
		{
			return false;
		}
		Generated::Auction::FAuctionAuthRp outbidAuthResponse;
		if (!WaitForPacket(client, outbidAuthResponse, outError) || outbidAuthResponse.resultCode != kSuccess ||
			outbidAuthResponse.userId != kOutbidUserId)
		{
			outError = "outbid listener authentication failed: " + outError;
			return false;
		}
		Generated::Auction::FAuctionOutbidNoti catchUpNotification;
		if (!WaitForPacket(client, catchUpNotification, outError) || catchUpNotification.listingId != kCatchUpListingId ||
			catchUpNotification.bidId != 77000004 || catchUpNotification.heldAmount != 1800 || catchUpNotification.newHighestAmount != 2200)
		{
			outError = "login outbid catch-up validation failed: " + outError;
			return false;
		}

		Generated::Auction::FBidRq bidRequest;
		bidRequest.requestId = 201;
		bidRequest.listingId = kBidListingId;
		bidRequest.bidAmount = 2500;
		bidRequest.expectedListingVersion = 1;
		if (!client.SendPacket(sessionId, bidRequest, kRandomKey, outError))
		{
			return false;
		}

		Generated::Auction::FBidRp bidResponse;
		Generated::Auction::FAuctionOutbidNoti onlineNotification;
		bool receivedBidResponse = false;
		bool receivedOnlineNotification = false;
		const auto notificationDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
		while (std::chrono::steady_clock::now() < notificationDeadline && (!receivedBidResponse || !receivedOnlineNotification))
		{
			ClientNetworkLib::FClientEvent event{};
			while (client.TryPopEvent(event))
			{
				if (event.Type == ClientNetworkLib::EClientEventType::PacketReceived && event.SessionId == sessionId &&
					event.Packet.Opcode == Generated::Auction::FBidRp::kOpcode)
				{
					receivedBidResponse = ClientNetworkLib::TryDeserializePacketEvent(event, bidResponse);
				}
				else if (event.Type == ClientNetworkLib::EClientEventType::PacketReceived && event.SessionId == outbidSessionId &&
						 event.Packet.Opcode == Generated::Auction::FAuctionOutbidNoti::kOpcode)
				{
					receivedOnlineNotification = ClientNetworkLib::TryDeserializePacketEvent(event, onlineNotification);
				}
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
		if (!receivedBidResponse || bidResponse.resultCode != kSuccess || bidResponse.listingId != kBidListingId ||
			bidResponse.bidId == 0 || bidResponse.bidAmount != 2500 || bidResponse.additionalDebit != 2500 ||
			bidResponse.currencyBalance != 4000 || bidResponse.listingVersion != 3)
		{
			outError = "normal bid validation failed.";
			return false;
		}
		if (!receivedOnlineNotification || onlineNotification.listingId != kBidListingId || onlineNotification.bidId != 77000003 ||
			onlineNotification.heldAmount != 2000 || onlineNotification.newHighestAmount != 2500)
		{
			outError = "online outbid notification validation failed.";
			return false;
		}

		bidRequest.requestId = 202;
		bidRequest.bidAmount = 3000;
		bidRequest.expectedListingVersion = bidResponse.listingVersion;
		if (!client.SendPacket(sessionId, bidRequest, kRandomKey, outError))
		{
			return false;
		}
		Generated::Auction::FBidRp higherBidResponse;
		if (!WaitForPacket(client, higherBidResponse, outError) || higherBidResponse.resultCode != kSuccess ||
			higherBidResponse.bidId != bidResponse.bidId || higherBidResponse.additionalDebit != 500 ||
			higherBidResponse.currencyBalance != 3500 || higherBidResponse.listingVersion != 5)
		{
			outError = "higher bid validation failed: " + outError;
			return false;
		}

		Generated::Auction::FBuyoutRq buyoutRequest;
		buyoutRequest.requestId = 203;
		buyoutRequest.listingId = kBidListingId;
		buyoutRequest.expectedListingVersion = higherBidResponse.listingVersion;
		if (!client.SendPacket(sessionId, buyoutRequest, kRandomKey, outError))
		{
			return false;
		}
		Generated::Auction::FBuyoutRp ownBidBuyoutResponse;
		if (!WaitForPacket(client, ownBidBuyoutResponse, outError) || ownBidBuyoutResponse.resultCode != kSuccess ||
			ownBidBuyoutResponse.listingId != kBidListingId || ownBidBuyoutResponse.buyoutPrice != 5000 ||
			ownBidBuyoutResponse.additionalDebit != 2000 || ownBidBuyoutResponse.currencyBalance != 1500 ||
			ownBidBuyoutResponse.itemMailId == 0 || ownBidBuyoutResponse.sellerMailId == 0 || ownBidBuyoutResponse.listingVersion != 7)
		{
			outError = "highest bidder buyout validation failed: " + outError;
			return false;
		}

		buyoutRequest.requestId = 204;
		buyoutRequest.listingId = kOtherBuyoutListingId;
		buyoutRequest.expectedListingVersion = 1;
		if (!client.SendPacket(sessionId, buyoutRequest, kRandomKey, outError))
		{
			return false;
		}
		Generated::Auction::FBuyoutRp otherBuyoutResponse;
		Generated::Auction::FAuctionOutbidNoti buyoutNotification;
		bool receivedBuyoutResponse = false;
		bool receivedBuyoutNotification = false;
		const auto buyoutDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
		while (std::chrono::steady_clock::now() < buyoutDeadline && (!receivedBuyoutResponse || !receivedBuyoutNotification))
		{
			ClientNetworkLib::FClientEvent event{};
			while (client.TryPopEvent(event))
			{
				if (event.Type == ClientNetworkLib::EClientEventType::PacketReceived && event.SessionId == sessionId &&
					event.Packet.Opcode == Generated::Auction::FBuyoutRp::kOpcode)
				{
					receivedBuyoutResponse = ClientNetworkLib::TryDeserializePacketEvent(event, otherBuyoutResponse);
				}
				else if (event.Type == ClientNetworkLib::EClientEventType::PacketReceived && event.SessionId == outbidSessionId &&
						 event.Packet.Opcode == Generated::Auction::FAuctionOutbidNoti::kOpcode)
				{
					receivedBuyoutNotification = ClientNetworkLib::TryDeserializePacketEvent(event, buyoutNotification);
				}
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
		if (!receivedBuyoutResponse || otherBuyoutResponse.resultCode != kSuccess || otherBuyoutResponse.buyoutPrice != 1000 ||
			otherBuyoutResponse.additionalDebit != 1000 || otherBuyoutResponse.currencyBalance != 500 ||
			otherBuyoutResponse.itemMailId == 0 || otherBuyoutResponse.sellerMailId == 0 || otherBuyoutResponse.listingVersion != 3)
		{
			outError = "buyout validation failed.";
			return false;
		}
		if (!receivedBuyoutNotification || buyoutNotification.listingId != kOtherBuyoutListingId || buyoutNotification.bidId != 77000005 ||
			buyoutNotification.heldAmount != 500 || buyoutNotification.newHighestAmount != 1000)
		{
			outError = "buyout outbid notification validation failed.";
			return false;
		}

		Generated::Auction::FMailListRq mailListRequest;
		mailListRequest.requestId = 205;
		mailListRequest.limit = authResponse.mailListPageSize;
		if (!client.SendPacket(sessionId, mailListRequest, kRandomKey, outError))
			return false;
		Generated::Auction::FMailListRp buyerMailList;
		if (!WaitForPacket(client, buyerMailList, outError) || buyerMailList.resultCode != kSuccess || buyerMailList.mailIds.size() != 2 ||
			buyerMailList.subjects.size() != 2 || buyerMailList.subjects[0].empty() || buyerMailList.subjects[1].empty())
		{
			outError = "buyer MailList validation failed: " + outError;
			return false;
		}

		Generated::Auction::FMailDetailRq mailDetailRequest;
		mailDetailRequest.requestId = 206;
		mailDetailRequest.mailId = buyerMailList.mailIds[0];
		if (!client.SendPacket(sessionId, mailDetailRequest, kRandomKey, outError))
			return false;
		Generated::Auction::FMailDetailRp buyerMailDetail;
		if (!WaitForPacket(client, buyerMailDetail, outError) || buyerMailDetail.resultCode != kSuccess ||
			buyerMailDetail.attachmentIds.size() != 1 || buyerMailDetail.attachmentTypes[0] != 1 ||
			buyerMailDetail.itemInstanceIds[0] != 994020000004 || buyerMailDetail.itemDataIds[0] != 2001 ||
			buyerMailDetail.quantities[0] != 10 || buyerMailDetail.attachmentStates[0] != 1)
		{
			outError = "buyer MailDetail validation failed: " + outError;
			return false;
		}

		Generated::Auction::FMailClaimRq mailClaimRequest;
		mailClaimRequest.requestId = 207;
		mailClaimRequest.mailId = buyerMailDetail.mailId;
		mailClaimRequest.attachmentId = buyerMailDetail.attachmentIds[0];
		if (!client.SendPacket(sessionId, mailClaimRequest, kRandomKey, outError))
			return false;
		Generated::Auction::FMailClaimRp itemClaimResponse;
		if (!WaitForPacket(client, itemClaimResponse, outError) || itemClaimResponse.resultCode != kSuccess ||
			itemClaimResponse.attachmentType != 1 || itemClaimResponse.itemInstanceId != 994020000004 ||
			itemClaimResponse.itemDataId != 2001 || itemClaimResponse.quantity != 10 || itemClaimResponse.mailState != 3)
		{
			outError = "item MailClaim validation failed: " + outError;
			return false;
		}
		mailClaimRequest.requestId = 208;
		if (!client.SendPacket(sessionId, mailClaimRequest, kRandomKey, outError))
			return false;
		Generated::Auction::FMailClaimRp duplicateClaimResponse;
		if (!WaitForPacket(client, duplicateClaimResponse, outError) || duplicateClaimResponse.resultCode != kMailAttachmentNotClaimable)
		{
			outError = "duplicate MailClaim was not rejected: " + outError;
			return false;
		}

		mailDetailRequest.requestId = 209;
		mailDetailRequest.mailId = ownBidBuyoutResponse.itemMailId;
		if (!client.SendPacket(sessionId, mailDetailRequest, kRandomKey, outError))
			return false;
		Generated::Auction::FMailDetailRp secondBuyerMail;
		if (!WaitForPacket(client, secondBuyerMail, outError) || secondBuyerMail.resultCode != kSuccess ||
			secondBuyerMail.attachmentIds.size() != 1 || secondBuyerMail.itemInstanceIds[0] != 994020000002)
		{
			outError = "second buyer mail validation failed: " + outError;
			return false;
		}
		mailClaimRequest.requestId = 210;
		mailClaimRequest.mailId = secondBuyerMail.mailId;
		mailClaimRequest.attachmentId = secondBuyerMail.attachmentIds[0];
		if (!client.SendPacket(sessionId, mailClaimRequest, kRandomKey, outError))
			return false;
		Generated::Auction::FMailClaimRp secondItemClaim;
		if (!WaitForPacket(client, secondItemClaim, outError) || secondItemClaim.resultCode != kSuccess ||
			secondItemClaim.itemInstanceId != 994020000002)
		{
			outError = "second item claim validation failed: " + outError;
			return false;
		}

		mailDetailRequest.requestId = 211;
		mailDetailRequest.mailId = otherBuyoutResponse.sellerMailId;
		if (!client.SendPacket(sessionId, mailDetailRequest, kRandomKey, outError))
			return false;
		Generated::Auction::FMailDetailRp foreignMailResponse;
		if (!WaitForPacket(client, foreignMailResponse, outError) || foreignMailResponse.resultCode != kMailNotFound)
		{
			outError = "foreign mail ownership validation failed: " + outError;
			return false;
		}

		ClientNetworkLib::FClientSessionId sellerSessionId = 0;
		if (!client.ConnectSession(sellerSessionId, outError))
			return false;
		Generated::Auction::FAuctionAuthRq sellerAuth;
		sellerAuth.requestId = 212;
		sellerAuth.ticket = sellerTicket;
		if (!client.SendPacket(sellerSessionId, sellerAuth, kRandomKey, outError))
			return false;
		Generated::Auction::FAuctionAuthRp sellerAuthResponse;
		if (!WaitForPacket(client, sellerAuthResponse, outError) || sellerAuthResponse.resultCode != kSuccess ||
			sellerAuthResponse.userId != kSellerUserId)
		{
			outError = "seller authentication failed: " + outError;
			return false;
		}
		mailListRequest.requestId = 213;
		if (!client.SendPacket(sellerSessionId, mailListRequest, kRandomKey, outError))
			return false;
		Generated::Auction::FMailListRp sellerMailList;
		if (!WaitForPacket(client, sellerMailList, outError) || sellerMailList.resultCode != kSuccess || sellerMailList.mailIds.size() != 2)
		{
			outError = "seller MailList validation failed: " + outError;
			return false;
		}
		std::uint64_t expectedSellerBalance = 0;
		for (std::size_t index = 0; index < sellerMailList.mailIds.size(); ++index)
		{
			mailDetailRequest.requestId = 214 + index * 2;
			mailDetailRequest.mailId = sellerMailList.mailIds[index];
			if (!client.SendPacket(sellerSessionId, mailDetailRequest, kRandomKey, outError))
				return false;
			Generated::Auction::FMailDetailRp sellerMailDetail;
			if (!WaitForPacket(client, sellerMailDetail, outError) || sellerMailDetail.resultCode != kSuccess ||
				sellerMailDetail.attachmentIds.size() != 1 || sellerMailDetail.attachmentTypes[0] != 2)
			{
				outError = "seller MailDetail validation failed: " + outError;
				return false;
			}
			expectedSellerBalance += sellerMailDetail.currencyAmounts[0];
			mailClaimRequest.requestId = 215 + index * 2;
			mailClaimRequest.mailId = sellerMailDetail.mailId;
			mailClaimRequest.attachmentId = sellerMailDetail.attachmentIds[0];
			if (!client.SendPacket(sellerSessionId, mailClaimRequest, kRandomKey, outError))
				return false;
			Generated::Auction::FMailClaimRp currencyClaim;
			if (!WaitForPacket(client, currencyClaim, outError) || currencyClaim.resultCode != kSuccess ||
				currencyClaim.attachmentType != 2 || currencyClaim.currencyId != authResponse.defaultCurrencyId ||
				currencyClaim.currencyBalance != expectedSellerBalance || currencyClaim.mailState != 3)
			{
				outError = "currency MailClaim validation failed: " + outError;
				return false;
			}
		}
		if (expectedSellerBalance != 6000)
		{
			outError = "seller currency mail total validation failed.";
			return false;
		}

		Generated::Auction::FListingCancelRq cancelRequest;
		cancelRequest.requestId = 220;
		cancelRequest.listingId = registerResponse.listingId;
		cancelRequest.expectedListingVersion = 3;
		if (!client.SendPacket(sessionId, cancelRequest, kRandomKey, outError))
			return false;
		Generated::Auction::FListingCancelRp staleCancelResponse;
		if (!WaitForPacket(client, staleCancelResponse, outError) || staleCancelResponse.resultCode != 19)
		{
			outError = "stale ListingCancel was not rejected: " + outError;
			return false;
		}

		cancelRequest.requestId = 221;
		cancelRequest.listingId = 99000006;
		cancelRequest.expectedListingVersion = 1;
		if (!client.SendPacket(sessionId, cancelRequest, kRandomKey, outError))
			return false;
		Generated::Auction::FListingCancelRp bidCancelResponse;
		if (!WaitForPacket(client, bidCancelResponse, outError) || bidCancelResponse.resultCode != kHighestBidExists)
		{
			outError = "listing with highest bid was cancellable: " + outError;
			return false;
		}

		cancelRequest.requestId = 222;
		cancelRequest.listingId = registerResponse.listingId;
		cancelRequest.expectedListingVersion = 2;
		if (!client.SendPacket(sessionId, cancelRequest, kRandomKey, outError))
			return false;
		Generated::Auction::FListingCancelRp cancelResponse;
		if (!WaitForPacket(client, cancelResponse, outError) || cancelResponse.resultCode != kSuccess || cancelResponse.returnMailId == 0 ||
			cancelResponse.listingVersion != 4)
		{
			outError = "ListingCancel validation failed: " + outError;
			return false;
		}

		cancelRequest.requestId = 223;
		cancelRequest.expectedListingVersion = cancelResponse.listingVersion;
		if (!client.SendPacket(sessionId, cancelRequest, kRandomKey, outError))
			return false;
		Generated::Auction::FListingCancelRp duplicateCancelResponse;
		if (!WaitForPacket(client, duplicateCancelResponse, outError) || duplicateCancelResponse.resultCode != 27)
		{
			outError = "duplicate ListingCancel was not rejected: " + outError;
			return false;
		}

		mailDetailRequest.requestId = 224;
		mailDetailRequest.mailId = cancelResponse.returnMailId;
		if (!client.SendPacket(sessionId, mailDetailRequest, kRandomKey, outError))
			return false;
		Generated::Auction::FMailDetailRp returnMailDetail;
		if (!WaitForPacket(client, returnMailDetail, outError) || returnMailDetail.resultCode != kSuccess ||
			returnMailDetail.subject.empty() || returnMailDetail.attachmentIds.size() != 1 || returnMailDetail.itemDataIds[0] != 1001)
		{
			outError = "cancel return mail validation failed: " + outError;
			return false;
		}
		mailClaimRequest.requestId = 225;
		mailClaimRequest.mailId = returnMailDetail.mailId;
		mailClaimRequest.attachmentId = returnMailDetail.attachmentIds[0];
		if (!client.SendPacket(sessionId, mailClaimRequest, kRandomKey, outError))
			return false;
		Generated::Auction::FMailClaimRp returnClaimResponse;
		if (!WaitForPacket(client, returnClaimResponse, outError) || returnClaimResponse.resultCode != kSuccess ||
			returnClaimResponse.itemDataId != 1001 || returnClaimResponse.mailState != 3)
		{
			outError = "cancel return item claim validation failed: " + outError;
			return false;
		}

		Generated::Auction::FAuctionWonNoti auctionWonNotification;
		bool receivedAuctionWonNotification = false;
		const auto expirationDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
		while (std::chrono::steady_clock::now() < expirationDeadline && !receivedAuctionWonNotification)
		{
			ClientNetworkLib::FClientEvent event{};
			while (client.TryPopEvent(event))
			{
				if (event.Type == ClientNetworkLib::EClientEventType::PacketReceived && event.SessionId == outbidSessionId &&
					event.Packet.Opcode == Generated::Auction::FAuctionWonNoti::kOpcode)
				{
					receivedAuctionWonNotification = ClientNetworkLib::TryDeserializePacketEvent(event, auctionWonNotification);
				}
				else if (event.Type == ClientNetworkLib::EClientEventType::ConnectFailed ||
						 event.Type == ClientNetworkLib::EClientEventType::SendFailed ||
						 event.Type == ClientNetworkLib::EClientEventType::SessionError)
				{
					outError = "expiration notification wait failed: " + event.Message;
					return false;
				}
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
		if (!receivedAuctionWonNotification || auctionWonNotification.listingId != 99000007 || auctionWonNotification.bidId != 77000011 ||
			auctionWonNotification.finalPrice != 2500 || auctionWonNotification.itemMailId == 0)
		{
			outError = "AuctionWon notification validation failed.";
			return false;
		}

		mailDetailRequest.requestId = 226;
		mailDetailRequest.mailId = auctionWonNotification.itemMailId;
		if (!client.SendPacket(outbidSessionId, mailDetailRequest, kRandomKey, outError))
			return false;
		Generated::Auction::FMailDetailRp wonItemMailDetail;
		if (!WaitForPacket(client, wonItemMailDetail, outError) || wonItemMailDetail.resultCode != kSuccess ||
			wonItemMailDetail.subject.empty() || wonItemMailDetail.attachmentIds.size() != 1 || wonItemMailDetail.itemDataIds[0] != 1002 ||
			wonItemMailDetail.quantities[0] != 1)
		{
			outError = "expired auction winner mail validation failed: " + outError;
			return false;
		}

		std::cout << "DATABASE_FLOW_TEST_SUCCESS userId=" << kUserId << " bidId=" << kBidId
				  << " refundedAmount=" << refundResponse.refundedAmount << " currencyBalance=" << otherBuyoutResponse.currencyBalance
				  << " inventoryCount=" << inventoryResponse.itemInstanceIds.size() << " listingId=" << registerResponse.listingId
				  << " duplicateListingRejected=1 staleItemVersionRejected=1"
				  << " listingSearchCount=" << searchResponse.listingIds.size() << " listingCursorVerified=1 listingDetailVerified=1"
				  << " loginOutbidCatchUp=1 onlineOutbidNoti=1 normalBid=1 higherBid=1"
				  << " highestBidderBuyout=1 buyoutMail=1 buyoutOutbidNoti=1"
				  << " mailList=1 mailDetail=1 itemClaim=1 currencyClaim=1 duplicateClaimRejected=1 ownershipChecked=1"
				  << " listingCancel=1 cancelReturnClaim=1 staleCancelRejected=1 bidCancelRejected=1 duplicateCancelRejected=1"
				  << " expirationWonNoti=1 expirationWinnerMail=1"
				  << " authRequired=1 alreadyAuthenticated=1 duplicateRejected=1"
				  << " replicaReadAttempts=" << replicaReadAttempts << "\n";
		return true;
	}

	bool RunCacheReadRpcFlow(
		ClientNetworkLib::FClientNetwork& client,
		const ClientNetworkLib::FClientSessionId sessionId,
		const std::string& ticket,
		const bool testWrites,
		std::string& outError)
	{
		constexpr std::uint64_t kUserId = 930010001;
		constexpr std::uint64_t kFirstItemInstanceId = 930010000003;
		constexpr std::uint64_t kSecondItemInstanceId = 930010000002;
		constexpr std::uint64_t kThirdItemInstanceId = 930010000001;
		constexpr std::uint64_t kMailId = 930010000001;
		constexpr std::uint64_t kAttachmentId = 930010000001;
		constexpr std::uint64_t kMissingMailId = 999999999999999999;
		constexpr std::uint64_t kInsufficientBidListingId = 993010000001;

		Generated::Auction::FAuctionAuthRq authRequest;
		authRequest.requestId = 1;
		authRequest.ticket = ticket;
		if (!client.SendPacket(sessionId, authRequest, kRandomKey, outError))
		{
			return false;
		}

		Generated::Auction::FAuctionAuthRp authResponse;
		if (!WaitForPacket(client, authResponse, outError) || authResponse.resultCode != kSuccess || authResponse.userId != kUserId ||
			authResponse.searchPageSize == 0 || authResponse.inventoryListPageSize == 0 || authResponse.mailListPageSize == 0 ||
			authResponse.defaultCurrencyId == 0 || authResponse.minimumBidIncrement == 0 || authResponse.minimumListingPrice == 0 ||
			authResponse.minimumListingPrice > authResponse.maximumListingPrice)
		{
			outError = "AuctionAuth validation failed. result=" + std::to_string(authResponse.resultCode) + " error=" + outError;
			return false;
		}

		Generated::Auction::FInventoryListRq inventoryRequest;
		inventoryRequest.requestId = 2;
		inventoryRequest.limit = authResponse.inventoryListPageSize;
		if (!client.SendPacket(sessionId, inventoryRequest, kRandomKey, outError))
		{
			return false;
		}

		Generated::Auction::FInventoryListRp inventoryResponse;
		if (!WaitForPacket(client, inventoryResponse, outError) || inventoryResponse.resultCode != kSuccess ||
			inventoryResponse.itemInstanceIds !=
				std::vector<std::uint64_t>{kFirstItemInstanceId, kSecondItemInstanceId, kThirdItemInstanceId} ||
			inventoryResponse.itemDataIds != std::vector<std::uint32_t>{3001, 2001, 1001} ||
			inventoryResponse.quantities != std::vector<std::uint32_t>{30, 20, 1} ||
			inventoryResponse.equippedStates != std::vector<std::uint8_t>{0, 0, 1} ||
			inventoryResponse.tradableStates != std::vector<std::uint8_t>{0, 1, 1} ||
			inventoryResponse.versions != std::vector<std::uint64_t>{3, 2, 1})
		{
			outError = "InventoryList cache RPC validation failed. result=" + std::to_string(inventoryResponse.resultCode) +
					   " count=" + std::to_string(inventoryResponse.itemInstanceIds.size()) + " error=" + outError;
			return false;
		}

		Generated::Auction::FMailListRq mailListRequest;
		mailListRequest.requestId = 3;
		mailListRequest.limit = authResponse.mailListPageSize;
		if (!client.SendPacket(sessionId, mailListRequest, kRandomKey, outError))
		{
			return false;
		}

		Generated::Auction::FMailListRp mailListResponse;
		if (!WaitForPacket(client, mailListResponse, outError) || mailListResponse.resultCode != kSuccess ||
			mailListResponse.mailIds != std::vector<std::uint64_t>{kMailId} || mailListResponse.mailTypes != std::vector<std::uint8_t>{2} ||
			mailListResponse.subjects != std::vector<std::string>{"Cache RPC smoke"} ||
			mailListResponse.states != std::vector<std::uint8_t>{1})
		{
			outError = "MailList cache RPC validation failed. result=" + std::to_string(mailListResponse.resultCode) +
					   " count=" + std::to_string(mailListResponse.mailIds.size()) + " error=" + outError;
			return false;
		}

		Generated::Auction::FMailDetailRq mailDetailRequest;
		mailDetailRequest.requestId = 4;
		mailDetailRequest.mailId = kMailId;
		if (!client.SendPacket(sessionId, mailDetailRequest, kRandomKey, outError))
		{
			return false;
		}

		Generated::Auction::FMailDetailRp mailDetailResponse;
		if (!WaitForPacket(client, mailDetailResponse, outError) || mailDetailResponse.resultCode != kSuccess ||
			mailDetailResponse.mailId != kMailId || mailDetailResponse.mailType != 2 || mailDetailResponse.subject != "Cache RPC smoke" ||
			mailDetailResponse.body != "GameDB reads are served by CacheServer RPC." || mailDetailResponse.state != 1 ||
			mailDetailResponse.attachmentIds != std::vector<std::uint64_t>{kAttachmentId} ||
			mailDetailResponse.attachmentTypes != std::vector<std::uint8_t>{2} ||
			mailDetailResponse.currencyIds != std::vector<std::uint16_t>{authResponse.defaultCurrencyId} ||
			mailDetailResponse.currencyAmounts != std::vector<std::uint64_t>{777} ||
			mailDetailResponse.attachmentStates != std::vector<std::uint8_t>{1})
		{
			outError = "MailDetail cache RPC validation failed. result=" + std::to_string(mailDetailResponse.resultCode) +
					   " attachmentCount=" + std::to_string(mailDetailResponse.attachmentIds.size()) + " error=" + outError;
			return false;
		}

		mailDetailRequest.requestId = 5;
		mailDetailRequest.mailId = kMissingMailId;
		if (!client.SendPacket(sessionId, mailDetailRequest, kRandomKey, outError))
		{
			return false;
		}

		Generated::Auction::FMailDetailRp missingMailResponse;
		if (!WaitForPacket(client, missingMailResponse, outError) || missingMailResponse.resultCode != kMailNotFound)
		{
			outError =
				"missing MailDetail was not rejected. result=" + std::to_string(missingMailResponse.resultCode) + " error=" + outError;
			return false;
		}

		if (!testWrites)
		{
			std::cout << "CACHE_READ_RPC_TEST_SUCCESS userId=" << kUserId << " inventoryCount=" << inventoryResponse.itemInstanceIds.size()
					  << " mailCount=" << mailListResponse.mailIds.size() << " mailDetailFound=1 mailDetailNotFound=1\n";
			return true;
		}

		Generated::Auction::FDebugCheatRq goldCheat;
		goldCheat.requestId = 6;
		goldCheat.cheatType = 1;
		goldCheat.amount = 500;
		if (!client.SendPacket(sessionId, goldCheat, kRandomKey, outError))
		{
			return false;
		}
		Generated::Auction::FDebugCheatRp goldCheatResponse;
		if (!WaitForPacket(client, goldCheatResponse, outError) || goldCheatResponse.resultCode != kSuccess ||
			goldCheatResponse.currencyBalance != 1500)
		{
			outError = "CreditCurrency RPC validation failed. result=" + std::to_string(goldCheatResponse.resultCode) +
					   " balance=" + std::to_string(goldCheatResponse.currencyBalance) + " error=" + outError;
			return false;
		}

		Generated::Auction::FDebugCheatRq itemCheat;
		itemCheat.requestId = 7;
		itemCheat.cheatType = 2;
		itemCheat.itemDataId = 1002;
		itemCheat.strStat = 4;
		itemCheat.dexStat = 5;
		itemCheat.intStat = 6;
		itemCheat.lukStat = 7;
		if (!client.SendPacket(sessionId, itemCheat, kRandomKey, outError))
		{
			return false;
		}
		Generated::Auction::FDebugCheatRp itemCheatResponse;
		if (!WaitForPacket(client, itemCheatResponse, outError) || itemCheatResponse.resultCode != kSuccess ||
			itemCheatResponse.itemInstanceId == 0)
		{
			outError = "GrantInventoryItem RPC validation failed. result=" + std::to_string(itemCheatResponse.resultCode) +
					   " itemInstanceId=" + std::to_string(itemCheatResponse.itemInstanceId) + " error=" + outError;
			return false;
		}

		inventoryRequest.requestId = 8;
		if (!client.SendPacket(sessionId, inventoryRequest, kRandomKey, outError))
		{
			return false;
		}
		Generated::Auction::FInventoryListRp inventoryAfterGrant;
		if (!WaitForPacket(client, inventoryAfterGrant, outError) || inventoryAfterGrant.resultCode != kSuccess)
		{
			return false;
		}
		const auto grantedItemIt = std::find(
			inventoryAfterGrant.itemInstanceIds.begin(), inventoryAfterGrant.itemInstanceIds.end(), itemCheatResponse.itemInstanceId);
		if (grantedItemIt == inventoryAfterGrant.itemInstanceIds.end())
		{
			outError = "granted item was not visible in the cached inventory.";
			return false;
		}
		const std::size_t grantedItemIndex = static_cast<std::size_t>(grantedItemIt - inventoryAfterGrant.itemInstanceIds.begin());
		if (inventoryAfterGrant.itemDataIds[grantedItemIndex] != 1002 || inventoryAfterGrant.quantities[grantedItemIndex] != 1 ||
			inventoryAfterGrant.tradableStates[grantedItemIndex] != 1 || inventoryAfterGrant.versions[grantedItemIndex] != 1 ||
			inventoryAfterGrant.itemData[grantedItemIndex].find("\"str\":4") == std::string::npos ||
			inventoryAfterGrant.itemData[grantedItemIndex].find("\"dex\":5") == std::string::npos ||
			inventoryAfterGrant.itemData[grantedItemIndex].find("\"int\":6") == std::string::npos ||
			inventoryAfterGrant.itemData[grantedItemIndex].find("\"luk\":7") == std::string::npos)
		{
			outError = "granted item snapshot did not match the requested item data.";
			return false;
		}

		Generated::Auction::FMailClaimRq claimRequest;
		claimRequest.requestId = 9;
		claimRequest.mailId = kMailId;
		claimRequest.attachmentId = kAttachmentId;
		if (!client.SendPacket(sessionId, claimRequest, kRandomKey, outError))
		{
			return false;
		}
		Generated::Auction::FMailClaimRp claimResponse;
		if (!WaitForPacket(client, claimResponse, outError) || claimResponse.resultCode != kSuccess || claimResponse.attachmentType != 2 ||
			claimResponse.currencyId != authResponse.defaultCurrencyId || claimResponse.currencyAmount != 777 ||
			claimResponse.currencyBalance != 2277 || claimResponse.mailState != 3)
		{
			outError = "ClaimMailAttachment RPC validation failed. result=" + std::to_string(claimResponse.resultCode) +
					   " balance=" + std::to_string(claimResponse.currencyBalance) + " error=" + outError;
			return false;
		}

		claimRequest.requestId = 10;
		if (!client.SendPacket(sessionId, claimRequest, kRandomKey, outError))
		{
			return false;
		}
		Generated::Auction::FMailClaimRp duplicateClaimResponse;
		if (!WaitForPacket(client, duplicateClaimResponse, outError) || duplicateClaimResponse.resultCode != kMailAttachmentNotClaimable)
		{
			outError = "duplicate Cache RPC mail claim was not rejected. result=" + std::to_string(duplicateClaimResponse.resultCode) +
					   " error=" + outError;
			return false;
		}

		Generated::Auction::FListingRegisterRq registerRequest;
		registerRequest.requestId = 11;
		registerRequest.itemInstanceId = itemCheatResponse.itemInstanceId;
		registerRequest.expectedItemVersion = 1;
		registerRequest.currencyId = authResponse.defaultCurrencyId;
		registerRequest.startPrice = std::clamp<std::uint64_t>(1000, authResponse.minimumListingPrice, authResponse.maximumListingPrice);
		registerRequest.buyoutPrice = std::clamp<std::uint64_t>(2000, registerRequest.startPrice, authResponse.maximumListingPrice);
		registerRequest.durationSeconds = authResponse.defaultListingDurationSeconds;
		if (!client.SendPacket(sessionId, registerRequest, kRandomKey, outError))
		{
			return false;
		}
		Generated::Auction::FListingRegisterRp registerResponse;
		if (!WaitForPacket(client, registerResponse, outError) || registerResponse.resultCode != kSuccess ||
			registerResponse.listingId == 0)
		{
			outError = "listing registration through Cache RPC failed. result=" + std::to_string(registerResponse.resultCode) +
					   " listingId=" + std::to_string(registerResponse.listingId) + " error=" + outError;
			return false;
		}

		inventoryRequest.requestId = 12;
		if (!client.SendPacket(sessionId, inventoryRequest, kRandomKey, outError))
		{
			return false;
		}
		Generated::Auction::FInventoryListRp inventoryAfterListing;
		if (!WaitForPacket(client, inventoryAfterListing, outError) || inventoryAfterListing.resultCode != kSuccess ||
			std::find(inventoryAfterListing.itemInstanceIds.begin(),
				inventoryAfterListing.itemInstanceIds.end(),
				itemCheatResponse.itemInstanceId) != inventoryAfterListing.itemInstanceIds.end())
		{
			outError = "listed item remained in the cached inventory.";
			return false;
		}

		Generated::Auction::FBidRq buyoutThresholdBidRequest;
		buyoutThresholdBidRequest.requestId = 13;
		buyoutThresholdBidRequest.listingId = kInsufficientBidListingId;
		buyoutThresholdBidRequest.bidAmount = 10000;
		buyoutThresholdBidRequest.expectedListingVersion = 1;
		if (!client.SendPacket(sessionId, buyoutThresholdBidRequest, kRandomKey, outError))
		{
			return false;
		}
		Generated::Auction::FBidRp buyoutThresholdBidResponse;
		if (!WaitForPacket(client, buyoutThresholdBidResponse, outError) || buyoutThresholdBidResponse.resultCode != kBidRequiresBuyout ||
			buyoutThresholdBidResponse.listingId != kInsufficientBidListingId || buyoutThresholdBidResponse.bidId != 0)
		{
			outError = "bid at the buyout price was not rejected. result=" + std::to_string(buyoutThresholdBidResponse.resultCode) +
					   " error=" + outError;
			return false;
		}

		Generated::Auction::FBidRq insufficientBidRequest;
		insufficientBidRequest.requestId = 14;
		insufficientBidRequest.listingId = kInsufficientBidListingId;
		insufficientBidRequest.bidAmount = 5000;
		insufficientBidRequest.expectedListingVersion = 1;
		if (!client.SendPacket(sessionId, insufficientBidRequest, kRandomKey, outError))
		{
			return false;
		}
		Generated::Auction::FBidRp insufficientBidResponse;
		if (!WaitForPacket(client, insufficientBidResponse, outError) || insufficientBidResponse.resultCode != kInsufficientCurrency ||
			insufficientBidResponse.listingId != kInsufficientBidListingId || insufficientBidResponse.bidId != 0)
		{
			outError = "insufficient-currency bid was not rejected through Cache RPC. result=" +
					   std::to_string(insufficientBidResponse.resultCode) + " error=" + outError;
			return false;
		}

		std::cout << "CACHE_WRITE_RPC_TEST_SUCCESS userId=" << kUserId << " currencyBalance=" << claimResponse.currencyBalance
				  << " grantedItemInstanceId=" << itemCheatResponse.itemInstanceId << " listingId=" << registerResponse.listingId
				  << " duplicateClaimRejected=1 cacheInventoryUpdated=1 buyoutThresholdRejected=1 debitFailureReverted=1\n";
		return true;
	}

	bool RunConcurrencyFlow(
		ClientNetworkLib::FClientNetwork& client,
		const ClientNetworkLib::FClientSessionId firstSessionId,
		const std::vector<std::string>& tickets,
		const std::uint64_t expirationRaceAtUnixMs,
		std::string& outError)
	{
		constexpr std::size_t kBuyerCount = 4;
		constexpr std::size_t kSessionCount = kBuyerCount + 1;
		constexpr std::uint64_t kBidRaceListingId = 99100001;
		constexpr std::uint64_t kBidBuyoutRaceListingId = 99100002;
		constexpr std::uint64_t kBuyoutCancelRaceListingId = 99100003;
		if (tickets.size() != kSessionCount)
		{
			outError = "concurrency test requires four buyer tickets and one seller ticket.";
			return false;
		}

		std::array<ClientNetworkLib::FClientSessionId, kSessionCount> sessions{};
		sessions[0] = firstSessionId;
		for (std::size_t index = 1; index < sessions.size(); ++index)
		{
			if (!client.ConnectSession(sessions[index], outError))
				return false;
		}
		for (std::size_t index = 0; index < sessions.size(); ++index)
		{
			Generated::Auction::FAuctionAuthRq request;
			request.requestId = 10 + index;
			request.ticket = tickets[index];
			if (!client.SendPacket(sessions[index], request, kRandomKey, outError))
				return false;
			Generated::Auction::FAuctionAuthRp response;
			if (!WaitForPacket(client, response, outError) || response.resultCode != kSuccess)
			{
				outError = "concurrency session authentication failed: " + outError;
				return false;
			}
		}

		std::array<std::string, kBuyerCount> bidSendErrors{};
		std::vector<std::thread> senders;
		senders.reserve(kBuyerCount);
		std::barrier bidStartLine(static_cast<std::ptrdiff_t>(kBuyerCount + 1));
		for (std::size_t index = 0; index < kBuyerCount; ++index)
		{
			senders.emplace_back(
				[&, index]
				{
					Generated::Auction::FBidRq request;
					request.requestId = 100 + index;
					request.listingId = kBidRaceListingId;
					request.bidAmount = 1100 + (index * 100);
					request.expectedListingVersion = 1;
					bidStartLine.arrive_and_wait();
					client.SendPacket(sessions[index], request, kRandomKey, bidSendErrors[index]);
				});
		}
		bidStartLine.arrive_and_wait();
		for (auto& sender : senders)
			sender.join();
		for (const auto& error : bidSendErrors)
		{
			if (!error.empty())
			{
				outError = "concurrent bid send failed: " + error;
				return false;
			}
		}

		std::size_t bidResponses = 0;
		std::size_t bidSuccesses = 0;
		std::size_t expectedBidRaceFailures = 0;
		std::size_t winningBidderIndex = kBuyerCount;
		const auto bidDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
		while (std::chrono::steady_clock::now() < bidDeadline && bidResponses < kBuyerCount)
		{
			ClientNetworkLib::FClientEvent event{};
			while (client.TryPopEvent(event))
			{
				if (event.Type == ClientNetworkLib::EClientEventType::PacketReceived &&
					event.Packet.Opcode == Generated::Auction::FBidRp::kOpcode)
				{
					Generated::Auction::FBidRp response;
					if (!ClientNetworkLib::TryDeserializePacketEvent(event, response))
					{
						outError = "bid race response deserialize failed.";
						return false;
					}
					++bidResponses;
					if (response.resultCode == kSuccess)
					{
						++bidSuccesses;
						const auto sessionIt = std::find(sessions.begin(), sessions.begin() + kBuyerCount, event.SessionId);
						if (sessionIt != sessions.begin() + kBuyerCount)
							winningBidderIndex = static_cast<std::size_t>(sessionIt - sessions.begin());
					}
					else if (response.resultCode == kListingVersionMismatch)
					{
						++expectedBidRaceFailures;
					}
				}
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}
		if (bidResponses != kBuyerCount || bidSuccesses != 1 || expectedBidRaceFailures != kBuyerCount - 1 ||
			winningBidderIndex == kBuyerCount)
		{
			outError = "bid race must produce exactly one successful bidder.";
			return false;
		}
		const std::size_t higherBidderIndex = (winningBidderIndex + 1) % kBuyerCount;
		Generated::Auction::FBidRq higherBidRequest;
		higherBidRequest.requestId = 150;
		higherBidRequest.listingId = kBidRaceListingId;
		higherBidRequest.bidAmount = 2000;
		higherBidRequest.expectedListingVersion = 3;
		if (!client.SendPacket(sessions[higherBidderIndex], higherBidRequest, kRandomKey, outError))
			return false;
		Generated::Auction::FBidRp higherBidResponse;
		if (!WaitForPacket(client, higherBidResponse, outError) || higherBidResponse.resultCode != kSuccess ||
			higherBidResponse.bidAmount != 2000 || higherBidResponse.listingVersion != 5)
		{
			outError = "post-race higher bid did not create the claimable loser state: " + outError;
			return false;
		}

		Generated::Auction::FBidRq bidRequest;
		bidRequest.requestId = 200;
		bidRequest.listingId = kBidBuyoutRaceListingId;
		bidRequest.bidAmount = 1600;
		bidRequest.expectedListingVersion = 1;
		Generated::Auction::FBuyoutRq buyoutRequest;
		buyoutRequest.requestId = 201;
		buyoutRequest.listingId = kBidBuyoutRaceListingId;
		buyoutRequest.expectedListingVersion = 1;
		std::array<std::string, 2> bidBuyoutErrors{};
		std::barrier bidBuyoutStartLine(3);
		std::thread bidSender(
			[&]
			{
				bidBuyoutStartLine.arrive_and_wait();
				client.SendPacket(sessions[0], bidRequest, kRandomKey, bidBuyoutErrors[0]);
			});
		std::thread buyoutSender(
			[&]
			{
				bidBuyoutStartLine.arrive_and_wait();
				client.SendPacket(sessions[1], buyoutRequest, kRandomKey, bidBuyoutErrors[1]);
			});
		bidBuyoutStartLine.arrive_and_wait();
		bidSender.join();
		buyoutSender.join();
		if (!bidBuyoutErrors[0].empty() || !bidBuyoutErrors[1].empty())
		{
			outError = "bid/buyout race send failed.";
			return false;
		}

		bool receivedBid = false;
		bool receivedBuyout = false;
		std::size_t bidBuyoutSuccesses = 0;
		const auto bidBuyoutDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
		while (std::chrono::steady_clock::now() < bidBuyoutDeadline && (!receivedBid || !receivedBuyout))
		{
			ClientNetworkLib::FClientEvent event{};
			while (client.TryPopEvent(event))
			{
				Generated::Auction::FBidRp bidResponse;
				Generated::Auction::FBuyoutRp buyoutResponse;
				if (TryReadPacketForSession(event, sessions[0], bidResponse))
				{
					receivedBid = true;
					if (bidResponse.resultCode == kSuccess)
						++bidBuyoutSuccesses;
				}
				else if (TryReadPacketForSession(event, sessions[1], buyoutResponse))
				{
					receivedBuyout = true;
					if (buyoutResponse.resultCode == kSuccess)
						++bidBuyoutSuccesses;
				}
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}
		if (!receivedBid || !receivedBuyout || bidBuyoutSuccesses != 1)
		{
			outError = "bid/buyout race must produce exactly one success.";
			return false;
		}

		Generated::Auction::FBuyoutRq finalBuyoutRequest;
		finalBuyoutRequest.requestId = 300;
		finalBuyoutRequest.listingId = kBuyoutCancelRaceListingId;
		finalBuyoutRequest.expectedListingVersion = 1;
		Generated::Auction::FListingCancelRq cancelRequest;
		cancelRequest.requestId = 301;
		cancelRequest.listingId = kBuyoutCancelRaceListingId;
		cancelRequest.expectedListingVersion = 1;
		std::array<std::string, 2> buyoutCancelErrors{};
		std::barrier buyoutCancelStartLine(3);
		std::thread finalBuyoutSender(
			[&]
			{
				buyoutCancelStartLine.arrive_and_wait();
				client.SendPacket(sessions[0], finalBuyoutRequest, kRandomKey, buyoutCancelErrors[0]);
			});
		std::thread cancelSender(
			[&]
			{
				buyoutCancelStartLine.arrive_and_wait();
				client.SendPacket(sessions[kBuyerCount], cancelRequest, kRandomKey, buyoutCancelErrors[1]);
			});
		buyoutCancelStartLine.arrive_and_wait();
		finalBuyoutSender.join();
		cancelSender.join();
		if (!buyoutCancelErrors[0].empty() || !buyoutCancelErrors[1].empty())
		{
			outError = "buyout/cancel race send failed.";
			return false;
		}

		bool receivedFinalBuyout = false;
		bool receivedCancel = false;
		std::size_t buyoutCancelSuccesses = 0;
		const auto buyoutCancelDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
		while (std::chrono::steady_clock::now() < buyoutCancelDeadline && (!receivedFinalBuyout || !receivedCancel))
		{
			ClientNetworkLib::FClientEvent event{};
			while (client.TryPopEvent(event))
			{
				Generated::Auction::FBuyoutRp buyoutResponse;
				Generated::Auction::FListingCancelRp cancelResponse;
				if (TryReadPacketForSession(event, sessions[0], buyoutResponse))
				{
					receivedFinalBuyout = true;
					if (buyoutResponse.resultCode == kSuccess)
						++buyoutCancelSuccesses;
				}
				else if (TryReadPacketForSession(event, sessions[kBuyerCount], cancelResponse))
				{
					receivedCancel = true;
					if (cancelResponse.resultCode == kSuccess)
						++buyoutCancelSuccesses;
				}
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}
		if (!receivedFinalBuyout || !receivedCancel || buyoutCancelSuccesses != 1)
		{
			outError = "buyout/cancel race must produce exactly one success.";
			return false;
		}

		if (expirationRaceAtUnixMs != 0)
		{
			const auto waitUntilUnixMs = [](const std::uint64_t targetUnixMs)
			{
				while (GetUnixTimeMilliseconds() + 1 < targetUnixMs)
					std::this_thread::sleep_for(std::chrono::milliseconds(1));
				while (GetUnixTimeMilliseconds() < targetUnixMs)
					std::this_thread::yield();
			};

			std::array<std::string, 3> expirationRaceErrors{};
			std::barrier expirationStartLine(4);
			std::thread expirationBidSender(
				[&]
				{
					Generated::Auction::FBidRq request;
					request.requestId = 400;
					request.listingId = 99100004;
					request.bidAmount = 1500;
					request.expectedListingVersion = 1;
					expirationStartLine.arrive_and_wait();
					waitUntilUnixMs(expirationRaceAtUnixMs - 25);
					client.SendPacket(sessions[2], request, kRandomKey, expirationRaceErrors[0]);
				});
			std::thread expirationBuyoutSender(
				[&]
				{
					Generated::Auction::FBuyoutRq request;
					request.requestId = 401;
					request.listingId = 99100005;
					request.expectedListingVersion = 1;
					expirationStartLine.arrive_and_wait();
					waitUntilUnixMs(expirationRaceAtUnixMs - 50);
					client.SendPacket(sessions[3], request, kRandomKey, expirationRaceErrors[1]);
				});
			std::thread expirationCancelSender(
				[&]
				{
					Generated::Auction::FListingCancelRq request;
					request.requestId = 402;
					request.listingId = 99100006;
					request.expectedListingVersion = 1;
					expirationStartLine.arrive_and_wait();
					waitUntilUnixMs(expirationRaceAtUnixMs);
					client.SendPacket(sessions[kBuyerCount], request, kRandomKey, expirationRaceErrors[2]);
				});
			expirationStartLine.arrive_and_wait();
			expirationBidSender.join();
			expirationBuyoutSender.join();
			expirationCancelSender.join();
			for (const auto& error : expirationRaceErrors)
			{
				if (!error.empty())
				{
					outError = "expiration race send failed: " + error;
					return false;
				}
			}

			bool receivedExpirationBid = false;
			bool receivedExpirationBuyout = false;
			bool receivedExpirationCancel = false;
			const auto expirationDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
			while (std::chrono::steady_clock::now() < expirationDeadline &&
				   (!receivedExpirationBid || !receivedExpirationBuyout || !receivedExpirationCancel))
			{
				ClientNetworkLib::FClientEvent event{};
				while (client.TryPopEvent(event))
				{
					Generated::Auction::FBidRp bidResponse;
					Generated::Auction::FBuyoutRp buyoutResponse;
					Generated::Auction::FListingCancelRp cancelResponse;
					if (TryReadPacketForSession(event, sessions[2], bidResponse))
					{
						receivedExpirationBid = true;
						if (bidResponse.resultCode != kSuccess && bidResponse.resultCode != kListingVersionMismatch)
						{
							outError = "unexpected expiration/bid result.";
							return false;
						}
					}
					else if (TryReadPacketForSession(event, sessions[3], buyoutResponse))
					{
						receivedExpirationBuyout = true;
						if (buyoutResponse.resultCode != kSuccess && buyoutResponse.resultCode != 20 &&
							buyoutResponse.resultCode != kListingVersionMismatch)
						{
							outError = "unexpected expiration/buyout result.";
							return false;
						}
					}
					else if (TryReadPacketForSession(event, sessions[kBuyerCount], cancelResponse))
					{
						receivedExpirationCancel = true;
						if (cancelResponse.resultCode != kSuccess && cancelResponse.resultCode != 27 &&
							cancelResponse.resultCode != kListingVersionMismatch)
						{
							outError = "unexpected expiration/cancel result.";
							return false;
						}
					}
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(5));
			}
			if (!receivedExpirationBid || !receivedExpirationBuyout || !receivedExpirationCancel)
			{
				outError = "expiration boundary response timeout.";
				return false;
			}
		}

		std::cout << "AUCTION_CONCURRENCY_CLIENT_SUCCESS"
				  << " simultaneousBidders=" << kBuyerCount << " bidWinnerCount=1 outbidClaimableCount=1"
				  << " bidBuyoutWinnerCount=1 buyoutCancelWinnerCount=1"
				  << " expirationBoundaryRaces=" << (expirationRaceAtUnixMs == 0 ? 0 : 3) << "\n";
		return true;
	}

	std::vector<SRequest> BuildRequests(
		const bool backpressureTest)
	{
		std::vector<SRequest> requests;
		if (!backpressureTest)
		{
			requests.reserve(kRoutingKeys.size());
			for (std::size_t index = 0; index < kRoutingKeys.size(); ++index)
			{
				requests.push_back({static_cast<std::uint64_t>(index + 1), kRoutingKeys[index]});
			}
			return requests;
		}

		requests.reserve(kSaturationRequestCount + 1);
		for (std::size_t index = 0; index < kSaturationRequestCount; ++index)
		{
			requests.push_back({static_cast<std::uint64_t>(index + 1), kSlowShardRoutingKey});
		}
		requests.push_back({1000, kIsolationRoutingKey});
		return requests;
	}
}

int main(
	int argc,
	char* argv[])
{
	SCommandLineOptions options{};
	if (!TryParseOptions(argc, argv, options))
	{
		std::cerr << "Usage: AuctionDummyClient [--port N] [--backpressure-test]"
				  << " [--database-flow-test --ticket VALUE --outbid-ticket VALUE --seller-ticket VALUE]"
				  << " [--cache-read-rpc-test --ticket VALUE] [--cache-write-rpc-test --ticket VALUE]"
				  << " [--bid-refund-fault-test --ticket VALUE]"
				  << " [--concurrency-test --concurrency-ticket VALUE (five times)]"
				  << " [--expiration-race-at-unix-ms N] [--reconnect-test --ticket VALUE]"
				  << " [--load-test --config PATH]\n";
		return 1;
	}
	if (options.loadTest)
	{
		if (options.loadTestConfigPath.empty())
		{
			std::cerr << "--config PATH is required for --load-test.\n";
			return 1;
		}
		return AuctionDummyClient::LoadTest::RunAuctionLoadTest(options.loadTestConfigPath);
	}

	ClientNetworkLib::FClientNetworkConfig config{};
	config.ServerIp = "127.0.0.1";
	config.ServerPort = options.port;
	config.WorkerThreadCount = 1;
	config.PacketCipherConfig.packetKey = kPacketKey;

	ClientNetworkLib::FClientNetwork client(config);
	std::string errorMessage;
	if (!client.Start(errorMessage))
	{
		std::cerr << "client start failed: " << errorMessage << "\n";
		return 1;
	}

	ClientNetworkLib::FClientSessionId sessionId = 0;
	if (!client.ConnectSession(sessionId, errorMessage))
	{
		std::cerr << "connect failed: " << errorMessage << "\n";
		client.Stop();
		return 1;
	}

	if (options.databaseFlowTest)
	{
		if (options.ticket.empty() || options.outbidTicket.empty() || options.sellerTicket.empty())
		{
			std::cerr << "--ticket, --outbid-ticket and --seller-ticket are required for --database-flow-test.\n";
			client.Stop();
			return 1;
		}
		const bool succeeded = RunDatabaseFlow(client, sessionId, options.ticket, options.outbidTicket, options.sellerTicket, errorMessage);
		if (succeeded)
		{
			ClientNetworkLib::FClientSessionId replaySessionId = 0;
			if (!client.ConnectSession(replaySessionId, errorMessage))
			{
				client.Stop();
				std::cerr << "ticket replay session connect failed: " << errorMessage << "\n";
				return 1;
			}
			Generated::Auction::FAuctionAuthRq replayRequest;
			replayRequest.requestId = 999;
			replayRequest.ticket = options.ticket;
			Generated::Auction::FAuctionAuthRp replayResponse;
			if (!client.SendPacket(replaySessionId, replayRequest, kRandomKey, errorMessage) ||
				!WaitForPacket(client, replayResponse, errorMessage) || replayResponse.resultCode != kAuthenticationFailed)
			{
				client.Stop();
				std::cerr << "consumed ticket replay was not rejected: " << errorMessage << "\n";
				return 1;
			}
			std::cout << "AUTH_REPLAY_REJECTED\n";
		}
		client.Stop();
		if (!succeeded)
		{
			std::cerr << "database flow test failed: " << errorMessage << "\n";
			return 1;
		}
		return 0;
	}
	if (options.cacheReadRpcTest || options.cacheWriteRpcTest)
	{
		if (options.ticket.empty())
		{
			std::cerr << "--ticket is required for Cache RPC tests.\n";
			client.Stop();
			return 1;
		}

		const bool succeeded = RunCacheReadRpcFlow(client, sessionId, options.ticket, options.cacheWriteRpcTest, errorMessage);
		client.Stop();
		if (!succeeded)
		{
			std::cerr << "cache RPC test failed: " << errorMessage << "\n";
			return 1;
		}
		return 0;
	}
	if (options.bidRefundFaultTest)
	{
		if (options.ticket.empty())
		{
			std::cerr << "--ticket is required for --bid-refund-fault-test.\n";
			client.Stop();
			return 1;
		}

		const int result = RunBidRefundFaultTest(client, sessionId, options.ticket, errorMessage);
		client.Stop();
		if (result == 1)
		{
			std::cerr << "bid refund fault test failed: " << errorMessage << "\n";
		}
		return result;
	}
	if (options.concurrencyTest)
	{
		const bool succeeded =
			RunConcurrencyFlow(client, sessionId, options.concurrencyTickets, options.expirationRaceAtUnixMs, errorMessage);
		client.Stop();
		if (!succeeded)
		{
			std::cerr << "concurrency test failed: " << errorMessage << "\n";
			return 1;
		}
		return 0;
	}
	if (options.reconnectTest)
	{
		if (options.ticket.empty())
		{
			std::cerr << "--ticket is required for --reconnect-test.\n";
			client.Stop();
			return 1;
		}
		const bool succeeded = RunReconnectFlow(client, sessionId, options.ticket, errorMessage);
		client.Stop();
		if (!succeeded)
		{
			std::cerr << "reconnect test failed: " << errorMessage << "\n";
			return 1;
		}
		return 0;
	}

	const std::vector<SRequest> requests = BuildRequests(options.backpressureTest);
	for (const SRequest& requestInfo : requests)
	{
		Generated::Auction::FPingRq request;
		request.requestId = requestInfo.requestId;
		request.routingKey = requestInfo.routingKey;
		request.clientTimeUnixMs = GetUnixTimeMilliseconds();
		if (!client.SendPacket(sessionId, request, kRandomKey, errorMessage))
		{
			std::cerr << "ping send failed: " << errorMessage << "\n";
			client.Stop();
			return 1;
		}
	}

	std::unordered_map<std::uint64_t, SObservedRoute> routeByRoutingKey;
	std::unordered_set<std::uint32_t> observedShards;
	std::vector<std::uint64_t> slowShardSuccessRequestIds;
	std::uint32_t expectedShardCount = 0;
	std::size_t receivedResponseCount = 0;
	std::size_t serverBusyCount = 0;
	bool sawSlowShardSuccess = false;
	bool isolationSucceededBeforeSlowShard = false;
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
	while (std::chrono::steady_clock::now() < deadline)
	{
		ClientNetworkLib::FClientEvent event{};
		while (client.TryPopEvent(event))
		{
			if (event.Type == ClientNetworkLib::EClientEventType::PacketReceived &&
				event.Packet.Opcode == Generated::Auction::FPingRp::kOpcode)
			{
				Generated::Auction::FPingRp response;
				if (!ClientNetworkLib::TryDeserializePacketEvent(event, response))
				{
					std::cerr << "ping response deserialize failed.\n";
					client.Stop();
					return 1;
				}

				if (response.requestId == 0 || response.routingKey == 0 || response.clientTimeUnixMs == 0 || response.shardCount == 0 ||
					response.shardIndex >= response.shardCount || response.contentInstanceId == 0)
				{
					std::cerr << "ping response validation failed.\n";
					client.Stop();
					return 1;
				}

				const std::uint32_t expectedShardIndex = static_cast<std::uint32_t>(response.routingKey % response.shardCount);
				if (response.shardIndex != expectedShardIndex)
				{
					std::cerr << "shard index does not match routingKey % shardCount.\n";
					client.Stop();
					return 1;
				}

				if (expectedShardCount == 0)
				{
					expectedShardCount = response.shardCount;
				}
				else if (expectedShardCount != response.shardCount)
				{
					std::cerr << "shard count changed during the test.\n";
					client.Stop();
					return 1;
				}

				if (response.resultCode == kServerBusy)
				{
					if (!options.backpressureTest || response.routingKey != kSlowShardRoutingKey || response.contentThreadId != 0)
					{
						std::cerr << "unexpected SERVER_BUSY response.\n";
						client.Stop();
						return 1;
					}
					++serverBusyCount;
				}
				else if (response.resultCode == kSuccess)
				{
					if (response.contentThreadId == 0)
					{
						std::cerr << "successful response has no content thread id.\n";
						client.Stop();
						return 1;
					}

					const SObservedRoute observedRoute{response.shardIndex, response.contentInstanceId, response.contentThreadId};
					const auto [routingIt, inserted] = routeByRoutingKey.emplace(response.routingKey, observedRoute);
					if (!inserted && (routingIt->second.shardIndex != observedRoute.shardIndex ||
										 routingIt->second.contentInstanceId != observedRoute.contentInstanceId ||
										 routingIt->second.contentThreadId != observedRoute.contentThreadId))
					{
						std::cerr << "same routing key reached a different shard execution context.\n";
						client.Stop();
						return 1;
					}

					observedShards.insert(response.shardIndex);
					if (options.backpressureTest && response.routingKey == kSlowShardRoutingKey)
					{
						sawSlowShardSuccess = true;
						slowShardSuccessRequestIds.push_back(response.requestId);
					}
					if (options.backpressureTest && response.routingKey == kIsolationRoutingKey && !sawSlowShardSuccess)
					{
						isolationSucceededBeforeSlowShard = true;
					}
				}
				else
				{
					std::cerr << "unknown auction result code.\n";
					client.Stop();
					return 1;
				}

				++receivedResponseCount;
				std::cout << "PING_RESPONSE"
						  << " resultCode=" << response.resultCode << " requestId=" << response.requestId
						  << " routingKey=" << response.routingKey << " shardIndex=" << response.shardIndex
						  << " contentThreadId=" << response.contentThreadId
						  << " rttMs=" << (GetUnixTimeMilliseconds() - response.clientTimeUnixMs) << "\n";

				if (receivedResponseCount == requests.size())
				{
					if (!options.backpressureTest)
					{
						if (serverBusyCount != 0 || observedShards.size() < 2)
						{
							std::cerr << "routing test result validation failed.\n";
							client.Stop();
							return 1;
						}
						std::cout << "SHARD_ROUTING_TEST_SUCCESS"
								  << " responses=" << receivedResponseCount << " uniqueKeys=" << routeByRoutingKey.size()
								  << " observedShards=" << observedShards.size() << " shardCount=" << expectedShardCount << "\n";
						client.Stop();
						return 0;
					}

					const bool ordered = std::is_sorted(slowShardSuccessRequestIds.begin(), slowShardSuccessRequestIds.end()) &&
										 std::adjacent_find(slowShardSuccessRequestIds.begin(), slowShardSuccessRequestIds.end()) ==
											 slowShardSuccessRequestIds.end();
					if (serverBusyCount == 0 || slowShardSuccessRequestIds.empty() || !ordered || !isolationSucceededBeforeSlowShard)
					{
						std::cerr << "backpressure/isolation/order validation failed."
								  << " busy=" << serverBusyCount << " slowSuccess=" << slowShardSuccessRequestIds.size()
								  << " ordered=" << ordered << " isolated=" << isolationSucceededBeforeSlowShard << "\n";
						client.Stop();
						return 1;
					}

					std::cout << "BACKPRESSURE_TEST_SUCCESS"
							  << " responses=" << receivedResponseCount << " acceptedSlowShard=" << slowShardSuccessRequestIds.size()
							  << " serverBusy=" << serverBusyCount << " ordered=1 isolated=1\n";
					client.Stop();
					return 0;
				}
			}

			if (event.Type == ClientNetworkLib::EClientEventType::ConnectFailed ||
				event.Type == ClientNetworkLib::EClientEventType::SendFailed ||
				event.Type == ClientNetworkLib::EClientEventType::SessionError)
			{
				std::cerr << "client network error: " << event.Message << "\n";
				client.Stop();
				return 1;
			}
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}

	std::cerr << "ping response timeout. received=" << receivedResponseCount << " expected=" << requests.size() << "\n";
	client.Stop();
	return 1;
}
