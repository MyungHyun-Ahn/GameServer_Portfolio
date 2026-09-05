#pragma once

namespace Generated::Auction
{
	class IAuctionPacketHandler
	{
	public:
		virtual ~IAuctionPacketHandler() = default;

		virtual bool HandleAuctionAuthRq(NetworkLib::IServer& server, std::uint64_t sessionId, const FAuctionAuthRq& packet) = 0;
		virtual bool HandleAuctionAuthRp(NetworkLib::IServer& server, std::uint64_t sessionId, const FAuctionAuthRp& packet) = 0;
		virtual bool HandlePingRq(NetworkLib::IServer& server, std::uint64_t sessionId, const FPingRq& packet) = 0;
		virtual bool HandlePingRp(NetworkLib::IServer& server, std::uint64_t sessionId, const FPingRp& packet) = 0;
		virtual bool HandleMyBidListRq(NetworkLib::IServer& server, std::uint64_t sessionId, const FMyBidListRq& packet) = 0;
		virtual bool HandleMyBidListRp(NetworkLib::IServer& server, std::uint64_t sessionId, const FMyBidListRp& packet) = 0;
		virtual bool HandleInventoryListRq(NetworkLib::IServer& server, std::uint64_t sessionId, const FInventoryListRq& packet) = 0;
		virtual bool HandleInventoryListRp(NetworkLib::IServer& server, std::uint64_t sessionId, const FInventoryListRp& packet) = 0;
		virtual bool HandleListingRegisterRq(NetworkLib::IServer& server, std::uint64_t sessionId, const FListingRegisterRq& packet) = 0;
		virtual bool HandleListingRegisterRp(NetworkLib::IServer& server, std::uint64_t sessionId, const FListingRegisterRp& packet) = 0;
		virtual bool HandleListingSearchRq(NetworkLib::IServer& server, std::uint64_t sessionId, const FListingSearchRq& packet) = 0;
		virtual bool HandleListingSearchRp(NetworkLib::IServer& server, std::uint64_t sessionId, const FListingSearchRp& packet) = 0;
		virtual bool HandleListingDetailRq(NetworkLib::IServer& server, std::uint64_t sessionId, const FListingDetailRq& packet) = 0;
		virtual bool HandleListingDetailRp(NetworkLib::IServer& server, std::uint64_t sessionId, const FListingDetailRp& packet) = 0;
		virtual bool HandleBidRq(NetworkLib::IServer& server, std::uint64_t sessionId, const FBidRq& packet) = 0;
		virtual bool HandleBidRp(NetworkLib::IServer& server, std::uint64_t sessionId, const FBidRp& packet) = 0;
		virtual bool HandleAuctionOutbidNoti(NetworkLib::IServer& server, std::uint64_t sessionId, const FAuctionOutbidNoti& packet) = 0;
		virtual bool HandleBuyoutRq(NetworkLib::IServer& server, std::uint64_t sessionId, const FBuyoutRq& packet) = 0;
		virtual bool HandleBuyoutRp(NetworkLib::IServer& server, std::uint64_t sessionId, const FBuyoutRp& packet) = 0;
		virtual bool HandleMailListRq(NetworkLib::IServer& server, std::uint64_t sessionId, const FMailListRq& packet) = 0;
		virtual bool HandleMailListRp(NetworkLib::IServer& server, std::uint64_t sessionId, const FMailListRp& packet) = 0;
		virtual bool HandleMailDetailRq(NetworkLib::IServer& server, std::uint64_t sessionId, const FMailDetailRq& packet) = 0;
		virtual bool HandleMailDetailRp(NetworkLib::IServer& server, std::uint64_t sessionId, const FMailDetailRp& packet) = 0;
		virtual bool HandleMailClaimRq(NetworkLib::IServer& server, std::uint64_t sessionId, const FMailClaimRq& packet) = 0;
		virtual bool HandleMailClaimRp(NetworkLib::IServer& server, std::uint64_t sessionId, const FMailClaimRp& packet) = 0;
		virtual bool HandleListingCancelRq(NetworkLib::IServer& server, std::uint64_t sessionId, const FListingCancelRq& packet) = 0;
		virtual bool HandleListingCancelRp(NetworkLib::IServer& server, std::uint64_t sessionId, const FListingCancelRp& packet) = 0;
		virtual bool HandleAuctionWonNoti(NetworkLib::IServer& server, std::uint64_t sessionId, const FAuctionWonNoti& packet) = 0;
		virtual bool HandleDebugCheatRq(NetworkLib::IServer& server, std::uint64_t sessionId, const FDebugCheatRq& packet) = 0;
		virtual bool HandleDebugCheatRp(NetworkLib::IServer& server, std::uint64_t sessionId, const FDebugCheatRp& packet) = 0;
		virtual bool HandleSaleHistorySearchRq(NetworkLib::IServer& server,
			std::uint64_t sessionId,
			const FSaleHistorySearchRq& packet) = 0;
		virtual bool HandleSaleHistorySearchRp(NetworkLib::IServer& server,
			std::uint64_t sessionId,
			const FSaleHistorySearchRp& packet) = 0;
		virtual bool HandleSaleHistoryDetailRq(NetworkLib::IServer& server,
			std::uint64_t sessionId,
			const FSaleHistoryDetailRq& packet) = 0;
		virtual bool HandleSaleHistoryDetailRp(NetworkLib::IServer& server,
			std::uint64_t sessionId,
			const FSaleHistoryDetailRp& packet) = 0;
		virtual bool HandleBidRefundRq(NetworkLib::IServer& server, std::uint64_t sessionId, const FBidRefundRq& packet) = 0;
		virtual bool HandleBidRefundRp(NetworkLib::IServer& server, std::uint64_t sessionId, const FBidRefundRp& packet) = 0;
	};

	class IAuctionPacketDispatcher
	{
	public:
		virtual ~IAuctionPacketDispatcher() = default;
		virtual bool DispatchPacket(NetworkLib::IServer& server,
			std::uint64_t sessionId,
			const NetworkLib::Packet::View::FPacketView& packetView) = 0;
	};

	class FAuctionPacketHandlerBase : public IAuctionPacketHandler, public IAuctionPacketDispatcher
	{
	public:
		bool DispatchPacket(
			NetworkLib::IServer& server,
			std::uint64_t sessionId,
			const NetworkLib::Packet::View::FPacketView& packetView) override
		{
			switch (packetView.opcode)
			{
				case FAuctionAuthRq::kOpcode:
				{
					FAuctionAuthRq packet;
					if (!NetworkLib::Packet::Serialization::DeserializeContentPacket(packetView, packet))
					{
						return false;
					}

					return HandleAuctionAuthRq(server, sessionId, packet);
				}
				case FAuctionAuthRp::kOpcode:
				{
					FAuctionAuthRp packet;
					if (!NetworkLib::Packet::Serialization::DeserializeContentPacket(packetView, packet))
					{
						return false;
					}

					return HandleAuctionAuthRp(server, sessionId, packet);
				}
				case FPingRq::kOpcode:
				{
					FPingRq packet;
					if (!NetworkLib::Packet::Serialization::DeserializeContentPacket(packetView, packet))
					{
						return false;
					}

					return HandlePingRq(server, sessionId, packet);
				}
				case FPingRp::kOpcode:
				{
					FPingRp packet;
					if (!NetworkLib::Packet::Serialization::DeserializeContentPacket(packetView, packet))
					{
						return false;
					}

					return HandlePingRp(server, sessionId, packet);
				}
				case FMyBidListRq::kOpcode:
				{
					FMyBidListRq packet;
					if (!NetworkLib::Packet::Serialization::DeserializeContentPacket(packetView, packet))
					{
						return false;
					}

					return HandleMyBidListRq(server, sessionId, packet);
				}
				case FMyBidListRp::kOpcode:
				{
					FMyBidListRp packet;
					if (!NetworkLib::Packet::Serialization::DeserializeContentPacket(packetView, packet))
					{
						return false;
					}

					return HandleMyBidListRp(server, sessionId, packet);
				}
				case FInventoryListRq::kOpcode:
				{
					FInventoryListRq packet;
					if (!NetworkLib::Packet::Serialization::DeserializeContentPacket(packetView, packet))
					{
						return false;
					}

					return HandleInventoryListRq(server, sessionId, packet);
				}
				case FInventoryListRp::kOpcode:
				{
					FInventoryListRp packet;
					if (!NetworkLib::Packet::Serialization::DeserializeContentPacket(packetView, packet))
					{
						return false;
					}

					return HandleInventoryListRp(server, sessionId, packet);
				}
				case FListingRegisterRq::kOpcode:
				{
					FListingRegisterRq packet;
					if (!NetworkLib::Packet::Serialization::DeserializeContentPacket(packetView, packet))
					{
						return false;
					}

					return HandleListingRegisterRq(server, sessionId, packet);
				}
				case FListingRegisterRp::kOpcode:
				{
					FListingRegisterRp packet;
					if (!NetworkLib::Packet::Serialization::DeserializeContentPacket(packetView, packet))
					{
						return false;
					}

					return HandleListingRegisterRp(server, sessionId, packet);
				}
				case FListingSearchRq::kOpcode:
				{
					FListingSearchRq packet;
					if (!NetworkLib::Packet::Serialization::DeserializeContentPacket(packetView, packet))
					{
						return false;
					}

					return HandleListingSearchRq(server, sessionId, packet);
				}
				case FListingSearchRp::kOpcode:
				{
					FListingSearchRp packet;
					if (!NetworkLib::Packet::Serialization::DeserializeContentPacket(packetView, packet))
					{
						return false;
					}

					return HandleListingSearchRp(server, sessionId, packet);
				}
				case FListingDetailRq::kOpcode:
				{
					FListingDetailRq packet;
					if (!NetworkLib::Packet::Serialization::DeserializeContentPacket(packetView, packet))
					{
						return false;
					}

					return HandleListingDetailRq(server, sessionId, packet);
				}
				case FListingDetailRp::kOpcode:
				{
					FListingDetailRp packet;
					if (!NetworkLib::Packet::Serialization::DeserializeContentPacket(packetView, packet))
					{
						return false;
					}

					return HandleListingDetailRp(server, sessionId, packet);
				}
				case FBidRq::kOpcode:
				{
					FBidRq packet;
					if (!NetworkLib::Packet::Serialization::DeserializeContentPacket(packetView, packet))
					{
						return false;
					}

					return HandleBidRq(server, sessionId, packet);
				}
				case FBidRp::kOpcode:
				{
					FBidRp packet;
					if (!NetworkLib::Packet::Serialization::DeserializeContentPacket(packetView, packet))
					{
						return false;
					}

					return HandleBidRp(server, sessionId, packet);
				}
				case FAuctionOutbidNoti::kOpcode:
				{
					FAuctionOutbidNoti packet;
					if (!NetworkLib::Packet::Serialization::DeserializeContentPacket(packetView, packet))
					{
						return false;
					}

					return HandleAuctionOutbidNoti(server, sessionId, packet);
				}
				case FBuyoutRq::kOpcode:
				{
					FBuyoutRq packet;
					if (!NetworkLib::Packet::Serialization::DeserializeContentPacket(packetView, packet))
					{
						return false;
					}

					return HandleBuyoutRq(server, sessionId, packet);
				}
				case FBuyoutRp::kOpcode:
				{
					FBuyoutRp packet;
					if (!NetworkLib::Packet::Serialization::DeserializeContentPacket(packetView, packet))
					{
						return false;
					}

					return HandleBuyoutRp(server, sessionId, packet);
				}
				case FMailListRq::kOpcode:
				{
					FMailListRq packet;
					if (!NetworkLib::Packet::Serialization::DeserializeContentPacket(packetView, packet))
					{
						return false;
					}

					return HandleMailListRq(server, sessionId, packet);
				}
				case FMailListRp::kOpcode:
				{
					FMailListRp packet;
					if (!NetworkLib::Packet::Serialization::DeserializeContentPacket(packetView, packet))
					{
						return false;
					}

					return HandleMailListRp(server, sessionId, packet);
				}
				case FMailDetailRq::kOpcode:
				{
					FMailDetailRq packet;
					if (!NetworkLib::Packet::Serialization::DeserializeContentPacket(packetView, packet))
					{
						return false;
					}

					return HandleMailDetailRq(server, sessionId, packet);
				}
				case FMailDetailRp::kOpcode:
				{
					FMailDetailRp packet;
					if (!NetworkLib::Packet::Serialization::DeserializeContentPacket(packetView, packet))
					{
						return false;
					}

					return HandleMailDetailRp(server, sessionId, packet);
				}
				case FMailClaimRq::kOpcode:
				{
					FMailClaimRq packet;
					if (!NetworkLib::Packet::Serialization::DeserializeContentPacket(packetView, packet))
					{
						return false;
					}

					return HandleMailClaimRq(server, sessionId, packet);
				}
				case FMailClaimRp::kOpcode:
				{
					FMailClaimRp packet;
					if (!NetworkLib::Packet::Serialization::DeserializeContentPacket(packetView, packet))
					{
						return false;
					}

					return HandleMailClaimRp(server, sessionId, packet);
				}
				case FListingCancelRq::kOpcode:
				{
					FListingCancelRq packet;
					if (!NetworkLib::Packet::Serialization::DeserializeContentPacket(packetView, packet))
					{
						return false;
					}

					return HandleListingCancelRq(server, sessionId, packet);
				}
				case FListingCancelRp::kOpcode:
				{
					FListingCancelRp packet;
					if (!NetworkLib::Packet::Serialization::DeserializeContentPacket(packetView, packet))
					{
						return false;
					}

					return HandleListingCancelRp(server, sessionId, packet);
				}
				case FAuctionWonNoti::kOpcode:
				{
					FAuctionWonNoti packet;
					if (!NetworkLib::Packet::Serialization::DeserializeContentPacket(packetView, packet))
					{
						return false;
					}

					return HandleAuctionWonNoti(server, sessionId, packet);
				}
				case FDebugCheatRq::kOpcode:
				{
					FDebugCheatRq packet;
					if (!NetworkLib::Packet::Serialization::DeserializeContentPacket(packetView, packet))
					{
						return false;
					}

					return HandleDebugCheatRq(server, sessionId, packet);
				}
				case FDebugCheatRp::kOpcode:
				{
					FDebugCheatRp packet;
					if (!NetworkLib::Packet::Serialization::DeserializeContentPacket(packetView, packet))
					{
						return false;
					}

					return HandleDebugCheatRp(server, sessionId, packet);
				}
				case FSaleHistorySearchRq::kOpcode:
				{
					FSaleHistorySearchRq packet;
					if (!NetworkLib::Packet::Serialization::DeserializeContentPacket(packetView, packet))
					{
						return false;
					}

					return HandleSaleHistorySearchRq(server, sessionId, packet);
				}
				case FSaleHistorySearchRp::kOpcode:
				{
					FSaleHistorySearchRp packet;
					if (!NetworkLib::Packet::Serialization::DeserializeContentPacket(packetView, packet))
					{
						return false;
					}

					return HandleSaleHistorySearchRp(server, sessionId, packet);
				}
				case FSaleHistoryDetailRq::kOpcode:
				{
					FSaleHistoryDetailRq packet;
					if (!NetworkLib::Packet::Serialization::DeserializeContentPacket(packetView, packet))
					{
						return false;
					}

					return HandleSaleHistoryDetailRq(server, sessionId, packet);
				}
				case FSaleHistoryDetailRp::kOpcode:
				{
					FSaleHistoryDetailRp packet;
					if (!NetworkLib::Packet::Serialization::DeserializeContentPacket(packetView, packet))
					{
						return false;
					}

					return HandleSaleHistoryDetailRp(server, sessionId, packet);
				}
				case FBidRefundRq::kOpcode:
				{
					FBidRefundRq packet;
					if (!NetworkLib::Packet::Serialization::DeserializeContentPacket(packetView, packet))
					{
						return false;
					}

					return HandleBidRefundRq(server, sessionId, packet);
				}
				case FBidRefundRp::kOpcode:
				{
					FBidRefundRp packet;
					if (!NetworkLib::Packet::Serialization::DeserializeContentPacket(packetView, packet))
					{
						return false;
					}

					return HandleBidRefundRp(server, sessionId, packet);
				}
				default:
					return OnUnhandledPacket(server, sessionId, packetView);
			}
		}

		bool HandleAuctionAuthRq(
			NetworkLib::IServer&,
			std::uint64_t,
			const FAuctionAuthRq&) override
		{
			return false;
		}

		bool HandleAuctionAuthRp(
			NetworkLib::IServer&,
			std::uint64_t,
			const FAuctionAuthRp&) override
		{
			return false;
		}

		bool HandlePingRq(
			NetworkLib::IServer&,
			std::uint64_t,
			const FPingRq&) override
		{
			return false;
		}

		bool HandlePingRp(
			NetworkLib::IServer&,
			std::uint64_t,
			const FPingRp&) override
		{
			return false;
		}

		bool HandleMyBidListRq(
			NetworkLib::IServer&,
			std::uint64_t,
			const FMyBidListRq&) override
		{
			return false;
		}

		bool HandleMyBidListRp(
			NetworkLib::IServer&,
			std::uint64_t,
			const FMyBidListRp&) override
		{
			return false;
		}

		bool HandleInventoryListRq(
			NetworkLib::IServer&,
			std::uint64_t,
			const FInventoryListRq&) override
		{
			return false;
		}

		bool HandleInventoryListRp(
			NetworkLib::IServer&,
			std::uint64_t,
			const FInventoryListRp&) override
		{
			return false;
		}

		bool HandleListingRegisterRq(
			NetworkLib::IServer&,
			std::uint64_t,
			const FListingRegisterRq&) override
		{
			return false;
		}

		bool HandleListingRegisterRp(
			NetworkLib::IServer&,
			std::uint64_t,
			const FListingRegisterRp&) override
		{
			return false;
		}

		bool HandleListingSearchRq(
			NetworkLib::IServer&,
			std::uint64_t,
			const FListingSearchRq&) override
		{
			return false;
		}

		bool HandleListingSearchRp(
			NetworkLib::IServer&,
			std::uint64_t,
			const FListingSearchRp&) override
		{
			return false;
		}

		bool HandleListingDetailRq(
			NetworkLib::IServer&,
			std::uint64_t,
			const FListingDetailRq&) override
		{
			return false;
		}

		bool HandleListingDetailRp(
			NetworkLib::IServer&,
			std::uint64_t,
			const FListingDetailRp&) override
		{
			return false;
		}

		bool HandleBidRq(
			NetworkLib::IServer&,
			std::uint64_t,
			const FBidRq&) override
		{
			return false;
		}

		bool HandleBidRp(
			NetworkLib::IServer&,
			std::uint64_t,
			const FBidRp&) override
		{
			return false;
		}

		bool HandleAuctionOutbidNoti(
			NetworkLib::IServer&,
			std::uint64_t,
			const FAuctionOutbidNoti&) override
		{
			return false;
		}

		bool HandleBuyoutRq(
			NetworkLib::IServer&,
			std::uint64_t,
			const FBuyoutRq&) override
		{
			return false;
		}

		bool HandleBuyoutRp(
			NetworkLib::IServer&,
			std::uint64_t,
			const FBuyoutRp&) override
		{
			return false;
		}

		bool HandleMailListRq(
			NetworkLib::IServer&,
			std::uint64_t,
			const FMailListRq&) override
		{
			return false;
		}

		bool HandleMailListRp(
			NetworkLib::IServer&,
			std::uint64_t,
			const FMailListRp&) override
		{
			return false;
		}

		bool HandleMailDetailRq(
			NetworkLib::IServer&,
			std::uint64_t,
			const FMailDetailRq&) override
		{
			return false;
		}

		bool HandleMailDetailRp(
			NetworkLib::IServer&,
			std::uint64_t,
			const FMailDetailRp&) override
		{
			return false;
		}

		bool HandleMailClaimRq(
			NetworkLib::IServer&,
			std::uint64_t,
			const FMailClaimRq&) override
		{
			return false;
		}

		bool HandleMailClaimRp(
			NetworkLib::IServer&,
			std::uint64_t,
			const FMailClaimRp&) override
		{
			return false;
		}

		bool HandleListingCancelRq(
			NetworkLib::IServer&,
			std::uint64_t,
			const FListingCancelRq&) override
		{
			return false;
		}

		bool HandleListingCancelRp(
			NetworkLib::IServer&,
			std::uint64_t,
			const FListingCancelRp&) override
		{
			return false;
		}

		bool HandleAuctionWonNoti(
			NetworkLib::IServer&,
			std::uint64_t,
			const FAuctionWonNoti&) override
		{
			return false;
		}

		bool HandleDebugCheatRq(
			NetworkLib::IServer&,
			std::uint64_t,
			const FDebugCheatRq&) override
		{
			return false;
		}

		bool HandleDebugCheatRp(
			NetworkLib::IServer&,
			std::uint64_t,
			const FDebugCheatRp&) override
		{
			return false;
		}

		bool HandleSaleHistorySearchRq(
			NetworkLib::IServer&,
			std::uint64_t,
			const FSaleHistorySearchRq&) override
		{
			return false;
		}

		bool HandleSaleHistorySearchRp(
			NetworkLib::IServer&,
			std::uint64_t,
			const FSaleHistorySearchRp&) override
		{
			return false;
		}

		bool HandleSaleHistoryDetailRq(
			NetworkLib::IServer&,
			std::uint64_t,
			const FSaleHistoryDetailRq&) override
		{
			return false;
		}

		bool HandleSaleHistoryDetailRp(
			NetworkLib::IServer&,
			std::uint64_t,
			const FSaleHistoryDetailRp&) override
		{
			return false;
		}

		bool HandleBidRefundRq(
			NetworkLib::IServer&,
			std::uint64_t,
			const FBidRefundRq&) override
		{
			return false;
		}

		bool HandleBidRefundRp(
			NetworkLib::IServer&,
			std::uint64_t,
			const FBidRefundRp&) override
		{
			return false;
		}

	protected:
		virtual bool OnUnhandledPacket(
			NetworkLib::IServer&,
			std::uint64_t,
			const NetworkLib::Packet::View::FPacketView&)
		{
			return false;
		}
	};

	template <typename TPacket>
	inline bool SendGeneratedPacket(
		NetworkLib::IServer& server,
		std::uint64_t sessionId,
		const TPacket& packet)
	{
		return NetworkLib::Packet::Serialization::SendContentPacket(server, sessionId, packet);
	}
}
