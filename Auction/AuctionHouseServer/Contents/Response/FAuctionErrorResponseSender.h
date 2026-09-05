#pragma once

namespace ContentsRuntime::Bridge
{
	class IContentBridge;
}

namespace AuctionHouseServer::Contents
{
	class FAuctionErrorResponseSender final
	{
	public:
		static bool Send(ContentsRuntime::Bridge::IContentBridge& bridge,
			std::uint64_t sessionId,
			std::uint16_t requestOpcode,
			std::span<const char> requestPayload,
			Domain::EAuctionResultCode resultCode,
			std::string_view debugMessage = {});
	};
}
