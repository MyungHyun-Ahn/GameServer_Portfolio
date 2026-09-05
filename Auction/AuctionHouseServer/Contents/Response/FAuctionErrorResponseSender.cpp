#include "AuctionHouseServerPch.h"

#include "AuctionHouseServer/Contents/Response/FAuctionErrorResponseSender.h"

#include "AuctionHouseServer/Domain/AuctionResultCode.h"
#include "ContentsRuntime/Bridge/IContentBridge.h"
#include "Generated/Packets/Cpp/Auction/AuctionPackets.h"

namespace AuctionHouseServer::Contents
{
	namespace
	{
		template <typename TRequest, typename TResponse, typename TFillResponse>
		bool SendErrorResponse(
			ContentsRuntime::Bridge::IContentBridge& bridge,
			const std::uint64_t sessionId,
			const std::uint16_t requestOpcode,
			const std::span<const char> requestPayload,
			const Domain::EAuctionResultCode resultCode,
			TFillResponse&& fillResponse)
		{
			TRequest request;
			if (!ContentsRuntime::Bridge::DeserializeOwnedPacket(requestOpcode, requestPayload, request))
			{
				return false;
			}

			TResponse response;
			response.resultCode = static_cast<std::uint16_t>(resultCode);
			response.requestId = request.requestId;
			fillResponse(request, response);
			return ContentsRuntime::Bridge::SendContentPacket(bridge, sessionId, response);
		}

		template <typename TRequest, typename TResponse>
		bool SendErrorResponse(
			ContentsRuntime::Bridge::IContentBridge& bridge,
			const std::uint64_t sessionId,
			const std::uint16_t requestOpcode,
			const std::span<const char> requestPayload,
			const Domain::EAuctionResultCode resultCode)
		{
			return SendErrorResponse<TRequest, TResponse>(
				bridge, sessionId, requestOpcode, requestPayload, resultCode, [](const TRequest&, TResponse&) {});
		}
	}

	bool FAuctionErrorResponseSender::Send(
		ContentsRuntime::Bridge::IContentBridge& bridge,
		const std::uint64_t sessionId,
		const std::uint16_t requestOpcode,
		const std::span<const char> requestPayload,
		const Domain::EAuctionResultCode resultCode,
		const std::string_view debugMessage)
	{
		using namespace Generated::Auction;
		switch (requestOpcode)
		{
			case FAuctionAuthRq::kOpcode:
				return SendErrorResponse<FAuctionAuthRq, FAuctionAuthRp>(bridge, sessionId, requestOpcode, requestPayload, resultCode);
			case FPingRq::kOpcode:
				return SendErrorResponse<FPingRq, FPingRp>(bridge,
					sessionId,
					requestOpcode,
					requestPayload,
					resultCode,
					[](const FPingRq& request, FPingRp& response)
					{
						response.routingKey = request.routingKey;
						response.clientTimeUnixMs = request.clientTimeUnixMs;
					});
			case FMyBidListRq::kOpcode:
				return SendErrorResponse<FMyBidListRq, FMyBidListRp>(bridge, sessionId, requestOpcode, requestPayload, resultCode);
			case FBidRefundRq::kOpcode:
				return SendErrorResponse<FBidRefundRq, FBidRefundRp>(bridge,
					sessionId,
					requestOpcode,
					requestPayload,
					resultCode,
					[](const FBidRefundRq& request, FBidRefundRp& response)
					{
						response.bidId = request.bidId;
					});
			case FInventoryListRq::kOpcode:
				return SendErrorResponse<FInventoryListRq, FInventoryListRp>(bridge, sessionId, requestOpcode, requestPayload, resultCode);
			case FListingRegisterRq::kOpcode:
				return SendErrorResponse<FListingRegisterRq, FListingRegisterRp>(
					bridge, sessionId, requestOpcode, requestPayload, resultCode);
			case FListingSearchRq::kOpcode:
				return SendErrorResponse<FListingSearchRq, FListingSearchRp>(bridge, sessionId, requestOpcode, requestPayload, resultCode);
			case FListingDetailRq::kOpcode:
				return SendErrorResponse<FListingDetailRq, FListingDetailRp>(bridge,
					sessionId,
					requestOpcode,
					requestPayload,
					resultCode,
					[](const FListingDetailRq& request, FListingDetailRp& response)
					{
						response.listingId = request.listingId;
					});
			case FBidRq::kOpcode:
				return SendErrorResponse<FBidRq, FBidRp>(bridge,
					sessionId,
					requestOpcode,
					requestPayload,
					resultCode,
					[](const FBidRq& request, FBidRp& response)
					{
						response.listingId = request.listingId;
					});
			case FBuyoutRq::kOpcode:
				return SendErrorResponse<FBuyoutRq, FBuyoutRp>(bridge,
					sessionId,
					requestOpcode,
					requestPayload,
					resultCode,
					[](const FBuyoutRq& request, FBuyoutRp& response)
					{
						response.listingId = request.listingId;
					});
			case FMailListRq::kOpcode:
				return SendErrorResponse<FMailListRq, FMailListRp>(bridge, sessionId, requestOpcode, requestPayload, resultCode);
			case FMailDetailRq::kOpcode:
				return SendErrorResponse<FMailDetailRq, FMailDetailRp>(bridge,
					sessionId,
					requestOpcode,
					requestPayload,
					resultCode,
					[](const FMailDetailRq& request, FMailDetailRp& response)
					{
						response.mailId = request.mailId;
					});
			case FMailClaimRq::kOpcode:
				return SendErrorResponse<FMailClaimRq, FMailClaimRp>(bridge,
					sessionId,
					requestOpcode,
					requestPayload,
					resultCode,
					[](const FMailClaimRq& request, FMailClaimRp& response)
					{
						response.mailId = request.mailId;
						response.attachmentId = request.attachmentId;
					});
			case FListingCancelRq::kOpcode:
				return SendErrorResponse<FListingCancelRq, FListingCancelRp>(bridge,
					sessionId,
					requestOpcode,
					requestPayload,
					resultCode,
					[](const FListingCancelRq& request, FListingCancelRp& response)
					{
						response.listingId = request.listingId;
					});
			case FDebugCheatRq::kOpcode:
				return SendErrorResponse<FDebugCheatRq, FDebugCheatRp>(bridge,
					sessionId,
					requestOpcode,
					requestPayload,
					resultCode,
					[debugMessage](const FDebugCheatRq& request, FDebugCheatRp& response)
					{
						response.cheatType = request.cheatType;
						response.message.assign(debugMessage);
					});
			case FSaleHistorySearchRq::kOpcode:
				return SendErrorResponse<FSaleHistorySearchRq, FSaleHistorySearchRp>(
					bridge, sessionId, requestOpcode, requestPayload, resultCode);
			case FSaleHistoryDetailRq::kOpcode:
				return SendErrorResponse<FSaleHistoryDetailRq, FSaleHistoryDetailRp>(bridge,
					sessionId,
					requestOpcode,
					requestPayload,
					resultCode,
					[](const FSaleHistoryDetailRq& request, FSaleHistoryDetailRp& response)
					{
						response.listingId = request.listingId;
					});
			default:
				return false;
		}
	}
}
