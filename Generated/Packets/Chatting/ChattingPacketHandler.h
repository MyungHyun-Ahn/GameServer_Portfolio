#pragma once

namespace Generated::Chatting
{
	class IChattingPacketHandler
	{
	public:
		virtual ~IChattingPacketHandler() = default;

		virtual bool HandleRoomListRq(NetworkLib::IServer& server, std::uint64_t sessionId, const FRoomListRq& packet) = 0;
		virtual bool HandleRoomListRp(NetworkLib::IServer& server, std::uint64_t sessionId, const FRoomListRp& packet) = 0;
		virtual bool HandleRoomChangeRq(NetworkLib::IServer& server, std::uint64_t sessionId, const FRoomChangeRq& packet) = 0;
		virtual bool HandleRoomChangeRp(NetworkLib::IServer& server, std::uint64_t sessionId, const FRoomChangeRp& packet) = 0;
		virtual bool HandleChattingRq(NetworkLib::IServer& server, std::uint64_t sessionId, const FChattingRq& packet) = 0;
		virtual bool HandleChattingRp(NetworkLib::IServer& server, std::uint64_t sessionId, const FChattingRp& packet) = 0;
		virtual bool HandleBroadcast(NetworkLib::IServer& server, std::uint64_t sessionId, const FBroadcast& packet) = 0;
	};

	class IChattingPacketDispatcher
	{
	public:
		virtual ~IChattingPacketDispatcher() = default;
		virtual bool DispatchPacket(NetworkLib::IServer& server,
			std::uint64_t sessionId,
			const NetworkLib::Packet::View::FPacketView& packetView) = 0;
	};

	class FChattingPacketHandlerBase : public IChattingPacketHandler, public IChattingPacketDispatcher
	{
	public:
		bool DispatchPacket(
			NetworkLib::IServer& server,
			std::uint64_t sessionId,
			const NetworkLib::Packet::View::FPacketView& packetView) override
		{
			switch (packetView.opcode)
			{
				case FRoomListRq::kOpcode:
				{
					FRoomListRq packet;
					if (!NetworkLib::Packet::Serialization::DeserializeContentPacket(packetView, packet))
					{
						return false;
					}

					return HandleRoomListRq(server, sessionId, packet);
				}
				case FRoomListRp::kOpcode:
				{
					FRoomListRp packet;
					if (!NetworkLib::Packet::Serialization::DeserializeContentPacket(packetView, packet))
					{
						return false;
					}

					return HandleRoomListRp(server, sessionId, packet);
				}
				case FRoomChangeRq::kOpcode:
				{
					FRoomChangeRq packet;
					if (!NetworkLib::Packet::Serialization::DeserializeContentPacket(packetView, packet))
					{
						return false;
					}

					return HandleRoomChangeRq(server, sessionId, packet);
				}
				case FRoomChangeRp::kOpcode:
				{
					FRoomChangeRp packet;
					if (!NetworkLib::Packet::Serialization::DeserializeContentPacket(packetView, packet))
					{
						return false;
					}

					return HandleRoomChangeRp(server, sessionId, packet);
				}
				case FChattingRq::kOpcode:
				{
					FChattingRq packet;
					if (!NetworkLib::Packet::Serialization::DeserializeContentPacket(packetView, packet))
					{
						return false;
					}

					return HandleChattingRq(server, sessionId, packet);
				}
				case FChattingRp::kOpcode:
				{
					FChattingRp packet;
					if (!NetworkLib::Packet::Serialization::DeserializeContentPacket(packetView, packet))
					{
						return false;
					}

					return HandleChattingRp(server, sessionId, packet);
				}
				case FBroadcast::kOpcode:
				{
					FBroadcast packet;
					if (!NetworkLib::Packet::Serialization::DeserializeContentPacket(packetView, packet))
					{
						return false;
					}

					return HandleBroadcast(server, sessionId, packet);
				}
				default:
					return OnUnhandledPacket(server, sessionId, packetView);
			}
		}

		bool HandleRoomListRq(
			NetworkLib::IServer&,
			std::uint64_t,
			const FRoomListRq&) override
		{
			return false;
		}

		bool HandleRoomListRp(
			NetworkLib::IServer&,
			std::uint64_t,
			const FRoomListRp&) override
		{
			return false;
		}

		bool HandleRoomChangeRq(
			NetworkLib::IServer&,
			std::uint64_t,
			const FRoomChangeRq&) override
		{
			return false;
		}

		bool HandleRoomChangeRp(
			NetworkLib::IServer&,
			std::uint64_t,
			const FRoomChangeRp&) override
		{
			return false;
		}

		bool HandleChattingRq(
			NetworkLib::IServer&,
			std::uint64_t,
			const FChattingRq&) override
		{
			return false;
		}

		bool HandleChattingRp(
			NetworkLib::IServer&,
			std::uint64_t,
			const FChattingRp&) override
		{
			return false;
		}

		bool HandleBroadcast(
			NetworkLib::IServer&,
			std::uint64_t,
			const FBroadcast&) override
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
