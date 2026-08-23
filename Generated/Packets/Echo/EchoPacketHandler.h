#pragma once

namespace Generated::Echo
{
	class IEchoPacketHandler
	{
	public:
		virtual ~IEchoPacketHandler() = default;

		virtual bool HandleEchoRq(NetworkLib::IServer& server, std::uint64_t sessionId, const FEchoRq& packet) = 0;
		virtual bool HandleEchoRp(NetworkLib::IServer& server, std::uint64_t sessionId, const FEchoRp& packet) = 0;
		virtual bool HandleEchoNoti(NetworkLib::IServer& server, std::uint64_t sessionId, const FEchoNoti& packet) = 0;
	};

	class IEchoPacketDispatcher
	{
	public:
		virtual ~IEchoPacketDispatcher() = default;
		virtual bool DispatchPacket(NetworkLib::IServer& server,
			std::uint64_t sessionId,
			const NetworkLib::Packet::View::FPacketView& packetView) = 0;
	};

	class FEchoPacketHandlerBase : public IEchoPacketHandler, public IEchoPacketDispatcher
	{
	public:
		bool DispatchPacket(
			NetworkLib::IServer& server,
			std::uint64_t sessionId,
			const NetworkLib::Packet::View::FPacketView& packetView) override
		{
			switch (packetView.opcode)
			{
				case FEchoRq::kOpcode:
				{
					FEchoRq packet;
					NetworkLib::Packet::View::FBorrowedViewScope borrowedViewScope;
					packet.BindBorrowedViewScope(borrowedViewScope.GetState());
					if (!NetworkLib::Packet::Serialization::DeserializeContentPacket(packetView, packet))
					{
						return false;
					}

					return HandleEchoRq(server, sessionId, packet);
				}
				case FEchoRp::kOpcode:
				{
					FEchoRp packet;
					NetworkLib::Packet::View::FBorrowedViewScope borrowedViewScope;
					packet.BindBorrowedViewScope(borrowedViewScope.GetState());
					if (!NetworkLib::Packet::Serialization::DeserializeContentPacket(packetView, packet))
					{
						return false;
					}

					return HandleEchoRp(server, sessionId, packet);
				}
				case FEchoNoti::kOpcode:
				{
					FEchoNoti packet;
					NetworkLib::Packet::View::FBorrowedViewScope borrowedViewScope;
					packet.BindBorrowedViewScope(borrowedViewScope.GetState());
					if (!NetworkLib::Packet::Serialization::DeserializeContentPacket(packetView, packet))
					{
						return false;
					}

					return HandleEchoNoti(server, sessionId, packet);
				}
				default:
					return OnUnhandledPacket(server, sessionId, packetView);
			}
		}

		bool HandleEchoRq(
			NetworkLib::IServer&,
			std::uint64_t,
			const FEchoRq&) override
		{
			return false;
		}

		bool HandleEchoRp(
			NetworkLib::IServer&,
			std::uint64_t,
			const FEchoRp&) override
		{
			return false;
		}

		bool HandleEchoNoti(
			NetworkLib::IServer&,
			std::uint64_t,
			const FEchoNoti&) override
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
