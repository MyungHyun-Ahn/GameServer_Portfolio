#pragma once

namespace Generated::Chat
{
	class IChatPacketHandler
	{
	public:
		virtual ~IChatPacketHandler() = default;

		virtual bool HandleRoomListRq(NetworkLib::IServer& server, std::uint64_t sessionId, const FRoomListRq& packet) = 0;
		virtual bool HandleRoomListRp(NetworkLib::IServer& server, std::uint64_t sessionId, const FRoomListRp& packet) = 0;
		virtual bool HandleRoomEnterRq(NetworkLib::IServer& server, std::uint64_t sessionId, const FRoomEnterRq& packet) = 0;
		virtual bool HandleRoomEnterRp(NetworkLib::IServer& server, std::uint64_t sessionId, const FRoomEnterRp& packet) = 0;
		virtual bool HandleRoomChangeRq(NetworkLib::IServer& server, std::uint64_t sessionId, const FRoomChangeRq& packet) = 0;
		virtual bool HandleRoomChangeRp(NetworkLib::IServer& server, std::uint64_t sessionId, const FRoomChangeRp& packet) = 0;
	};

	class IChatPacketDispatcher
	{
	public:
		virtual ~IChatPacketDispatcher() = default;
		virtual bool DispatchPacket(NetworkLib::IServer& server,
			std::uint64_t sessionId,
			const NetworkLib::Packet::View::FPacketView& packetView) = 0;
	};

	class FChatPacketHandlerBase : public IChatPacketHandler, public IChatPacketDispatcher
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
				case FRoomEnterRq::kOpcode:
				{
					FRoomEnterRq packet;
					if (!NetworkLib::Packet::Serialization::DeserializeContentPacket(packetView, packet))
					{
						return false;
					}

					return HandleRoomEnterRq(server, sessionId, packet);
				}
				case FRoomEnterRp::kOpcode:
				{
					FRoomEnterRp packet;
					if (!NetworkLib::Packet::Serialization::DeserializeContentPacket(packetView, packet))
					{
						return false;
					}

					return HandleRoomEnterRp(server, sessionId, packet);
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

		bool HandleRoomEnterRq(
			NetworkLib::IServer&,
			std::uint64_t,
			const FRoomEnterRq&) override
		{
			return false;
		}

		bool HandleRoomEnterRp(
			NetworkLib::IServer&,
			std::uint64_t,
			const FRoomEnterRp&) override
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
