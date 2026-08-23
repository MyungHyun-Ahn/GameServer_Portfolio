#pragma once

namespace Generated
{
	class FPacketRouter
	{
	public:
		void SetAuctionHandler(
			Auction::IAuctionPacketDispatcher* handler) noexcept
		{
			m_auctionHandler = handler;
		}

		void SetChatHandler(
			Chat::IChatPacketDispatcher* handler) noexcept
		{
			m_chatHandler = handler;
		}

		void SetChattingHandler(
			Chatting::IChattingPacketDispatcher* handler) noexcept
		{
			m_chattingHandler = handler;
		}

		void SetEchoHandler(
			Echo::IEchoPacketDispatcher* handler) noexcept
		{
			m_echoHandler = handler;
		}

		void SetLoginHandler(
			Login::ILoginPacketDispatcher* handler) noexcept
		{
			m_loginHandler = handler;
		}

		bool DispatchPacket(
			NetworkLib::IServer& server,
			std::uint64_t sessionId,
			const NetworkLib::Packet::View::FPacketView& packetView)
		{
			switch (packetView.opcode)
			{
				case Auction::FAuctionAuthRq::kOpcode:
					return m_auctionHandler != nullptr ? m_auctionHandler->DispatchPacket(server, sessionId, packetView) : false;
				case Auction::FAuctionAuthRp::kOpcode:
					return m_auctionHandler != nullptr ? m_auctionHandler->DispatchPacket(server, sessionId, packetView) : false;
				case Auction::FPingRq::kOpcode:
					return m_auctionHandler != nullptr ? m_auctionHandler->DispatchPacket(server, sessionId, packetView) : false;
				case Auction::FPingRp::kOpcode:
					return m_auctionHandler != nullptr ? m_auctionHandler->DispatchPacket(server, sessionId, packetView) : false;
				case Auction::FMyBidListRq::kOpcode:
					return m_auctionHandler != nullptr ? m_auctionHandler->DispatchPacket(server, sessionId, packetView) : false;
				case Auction::FMyBidListRp::kOpcode:
					return m_auctionHandler != nullptr ? m_auctionHandler->DispatchPacket(server, sessionId, packetView) : false;
				case Auction::FInventoryListRq::kOpcode:
					return m_auctionHandler != nullptr ? m_auctionHandler->DispatchPacket(server, sessionId, packetView) : false;
				case Auction::FInventoryListRp::kOpcode:
					return m_auctionHandler != nullptr ? m_auctionHandler->DispatchPacket(server, sessionId, packetView) : false;
				case Auction::FListingRegisterRq::kOpcode:
					return m_auctionHandler != nullptr ? m_auctionHandler->DispatchPacket(server, sessionId, packetView) : false;
				case Auction::FListingRegisterRp::kOpcode:
					return m_auctionHandler != nullptr ? m_auctionHandler->DispatchPacket(server, sessionId, packetView) : false;
				case Auction::FListingSearchRq::kOpcode:
					return m_auctionHandler != nullptr ? m_auctionHandler->DispatchPacket(server, sessionId, packetView) : false;
				case Auction::FListingSearchRp::kOpcode:
					return m_auctionHandler != nullptr ? m_auctionHandler->DispatchPacket(server, sessionId, packetView) : false;
				case Auction::FListingDetailRq::kOpcode:
					return m_auctionHandler != nullptr ? m_auctionHandler->DispatchPacket(server, sessionId, packetView) : false;
				case Auction::FListingDetailRp::kOpcode:
					return m_auctionHandler != nullptr ? m_auctionHandler->DispatchPacket(server, sessionId, packetView) : false;
				case Auction::FBidRq::kOpcode:
					return m_auctionHandler != nullptr ? m_auctionHandler->DispatchPacket(server, sessionId, packetView) : false;
				case Auction::FBidRp::kOpcode:
					return m_auctionHandler != nullptr ? m_auctionHandler->DispatchPacket(server, sessionId, packetView) : false;
				case Auction::FAuctionOutbidNoti::kOpcode:
					return m_auctionHandler != nullptr ? m_auctionHandler->DispatchPacket(server, sessionId, packetView) : false;
				case Auction::FBuyoutRq::kOpcode:
					return m_auctionHandler != nullptr ? m_auctionHandler->DispatchPacket(server, sessionId, packetView) : false;
				case Auction::FBuyoutRp::kOpcode:
					return m_auctionHandler != nullptr ? m_auctionHandler->DispatchPacket(server, sessionId, packetView) : false;
				case Auction::FMailListRq::kOpcode:
					return m_auctionHandler != nullptr ? m_auctionHandler->DispatchPacket(server, sessionId, packetView) : false;
				case Auction::FMailListRp::kOpcode:
					return m_auctionHandler != nullptr ? m_auctionHandler->DispatchPacket(server, sessionId, packetView) : false;
				case Auction::FMailDetailRq::kOpcode:
					return m_auctionHandler != nullptr ? m_auctionHandler->DispatchPacket(server, sessionId, packetView) : false;
				case Auction::FMailDetailRp::kOpcode:
					return m_auctionHandler != nullptr ? m_auctionHandler->DispatchPacket(server, sessionId, packetView) : false;
				case Auction::FMailClaimRq::kOpcode:
					return m_auctionHandler != nullptr ? m_auctionHandler->DispatchPacket(server, sessionId, packetView) : false;
				case Auction::FMailClaimRp::kOpcode:
					return m_auctionHandler != nullptr ? m_auctionHandler->DispatchPacket(server, sessionId, packetView) : false;
				case Auction::FListingCancelRq::kOpcode:
					return m_auctionHandler != nullptr ? m_auctionHandler->DispatchPacket(server, sessionId, packetView) : false;
				case Auction::FListingCancelRp::kOpcode:
					return m_auctionHandler != nullptr ? m_auctionHandler->DispatchPacket(server, sessionId, packetView) : false;
				case Auction::FAuctionWonNoti::kOpcode:
					return m_auctionHandler != nullptr ? m_auctionHandler->DispatchPacket(server, sessionId, packetView) : false;
				case Auction::FDebugCheatRq::kOpcode:
					return m_auctionHandler != nullptr ? m_auctionHandler->DispatchPacket(server, sessionId, packetView) : false;
				case Auction::FDebugCheatRp::kOpcode:
					return m_auctionHandler != nullptr ? m_auctionHandler->DispatchPacket(server, sessionId, packetView) : false;
				case Auction::FSaleHistorySearchRq::kOpcode:
					return m_auctionHandler != nullptr ? m_auctionHandler->DispatchPacket(server, sessionId, packetView) : false;
				case Auction::FSaleHistorySearchRp::kOpcode:
					return m_auctionHandler != nullptr ? m_auctionHandler->DispatchPacket(server, sessionId, packetView) : false;
				case Auction::FSaleHistoryDetailRq::kOpcode:
					return m_auctionHandler != nullptr ? m_auctionHandler->DispatchPacket(server, sessionId, packetView) : false;
				case Auction::FSaleHistoryDetailRp::kOpcode:
					return m_auctionHandler != nullptr ? m_auctionHandler->DispatchPacket(server, sessionId, packetView) : false;
				case Auction::FBidRefundRq::kOpcode:
					return m_auctionHandler != nullptr ? m_auctionHandler->DispatchPacket(server, sessionId, packetView) : false;
				case Auction::FBidRefundRp::kOpcode:
					return m_auctionHandler != nullptr ? m_auctionHandler->DispatchPacket(server, sessionId, packetView) : false;
				case Chat::FRoomListRq::kOpcode:
					return m_chatHandler != nullptr ? m_chatHandler->DispatchPacket(server, sessionId, packetView) : false;
				case Chat::FRoomListRp::kOpcode:
					return m_chatHandler != nullptr ? m_chatHandler->DispatchPacket(server, sessionId, packetView) : false;
				case Chat::FRoomEnterRq::kOpcode:
					return m_chatHandler != nullptr ? m_chatHandler->DispatchPacket(server, sessionId, packetView) : false;
				case Chat::FRoomEnterRp::kOpcode:
					return m_chatHandler != nullptr ? m_chatHandler->DispatchPacket(server, sessionId, packetView) : false;
				case Chat::FRoomChangeRq::kOpcode:
					return m_chatHandler != nullptr ? m_chatHandler->DispatchPacket(server, sessionId, packetView) : false;
				case Chat::FRoomChangeRp::kOpcode:
					return m_chatHandler != nullptr ? m_chatHandler->DispatchPacket(server, sessionId, packetView) : false;
				case Chatting::FRoomListRq::kOpcode:
					return m_chattingHandler != nullptr ? m_chattingHandler->DispatchPacket(server, sessionId, packetView) : false;
				case Chatting::FRoomListRp::kOpcode:
					return m_chattingHandler != nullptr ? m_chattingHandler->DispatchPacket(server, sessionId, packetView) : false;
				case Chatting::FRoomChangeRq::kOpcode:
					return m_chattingHandler != nullptr ? m_chattingHandler->DispatchPacket(server, sessionId, packetView) : false;
				case Chatting::FRoomChangeRp::kOpcode:
					return m_chattingHandler != nullptr ? m_chattingHandler->DispatchPacket(server, sessionId, packetView) : false;
				case Chatting::FChattingRq::kOpcode:
					return m_chattingHandler != nullptr ? m_chattingHandler->DispatchPacket(server, sessionId, packetView) : false;
				case Chatting::FChattingRp::kOpcode:
					return m_chattingHandler != nullptr ? m_chattingHandler->DispatchPacket(server, sessionId, packetView) : false;
				case Chatting::FBroadcast::kOpcode:
					return m_chattingHandler != nullptr ? m_chattingHandler->DispatchPacket(server, sessionId, packetView) : false;
				case Echo::FEchoRq::kOpcode:
					return m_echoHandler != nullptr ? m_echoHandler->DispatchPacket(server, sessionId, packetView) : false;
				case Echo::FEchoRp::kOpcode:
					return m_echoHandler != nullptr ? m_echoHandler->DispatchPacket(server, sessionId, packetView) : false;
				case Echo::FEchoNoti::kOpcode:
					return m_echoHandler != nullptr ? m_echoHandler->DispatchPacket(server, sessionId, packetView) : false;
				case Login::FLoginRq::kOpcode:
					return m_loginHandler != nullptr ? m_loginHandler->DispatchPacket(server, sessionId, packetView) : false;
				case Login::FLoginRp::kOpcode:
					return m_loginHandler != nullptr ? m_loginHandler->DispatchPacket(server, sessionId, packetView) : false;
				case Login::FLoginAuthRq::kOpcode:
					return m_loginHandler != nullptr ? m_loginHandler->DispatchPacket(server, sessionId, packetView) : false;
				case Login::FLoginAuthRp::kOpcode:
					return m_loginHandler != nullptr ? m_loginHandler->DispatchPacket(server, sessionId, packetView) : false;
				default:
					return false;
			}
		}

	private:
		Auction::IAuctionPacketDispatcher* m_auctionHandler = nullptr;
		Chat::IChatPacketDispatcher* m_chatHandler = nullptr;
		Chatting::IChattingPacketDispatcher* m_chattingHandler = nullptr;
		Echo::IEchoPacketDispatcher* m_echoHandler = nullptr;
		Login::ILoginPacketDispatcher* m_loginHandler = nullptr;
	};
}
