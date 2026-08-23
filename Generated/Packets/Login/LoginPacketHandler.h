#pragma once

namespace Generated::Login
{
	class ILoginPacketHandler
	{
	public:
		virtual ~ILoginPacketHandler() = default;

		virtual bool HandleLoginRq(NetworkLib::IServer& server, std::uint64_t sessionId, const FLoginRq& packet) = 0;
		virtual bool HandleLoginRp(NetworkLib::IServer& server, std::uint64_t sessionId, const FLoginRp& packet) = 0;
		virtual bool HandleLoginAuthRq(NetworkLib::IServer& server, std::uint64_t sessionId, const FLoginAuthRq& packet) = 0;
		virtual bool HandleLoginAuthRp(NetworkLib::IServer& server, std::uint64_t sessionId, const FLoginAuthRp& packet) = 0;
	};

	class ILoginPacketDispatcher
	{
	public:
		virtual ~ILoginPacketDispatcher() = default;
		virtual bool DispatchPacket(NetworkLib::IServer& server,
			std::uint64_t sessionId,
			const NetworkLib::Packet::View::FPacketView& packetView) = 0;
	};

	class FLoginPacketHandlerBase : public ILoginPacketHandler, public ILoginPacketDispatcher
	{
	public:
		bool DispatchPacket(
			NetworkLib::IServer& server,
			std::uint64_t sessionId,
			const NetworkLib::Packet::View::FPacketView& packetView) override
		{
			switch (packetView.opcode)
			{
				case FLoginRq::kOpcode:
				{
					FLoginRq packet;
					if (!NetworkLib::Packet::Serialization::DeserializeContentPacket(packetView, packet))
					{
						return false;
					}

					return HandleLoginRq(server, sessionId, packet);
				}
				case FLoginRp::kOpcode:
				{
					FLoginRp packet;
					if (!NetworkLib::Packet::Serialization::DeserializeContentPacket(packetView, packet))
					{
						return false;
					}

					return HandleLoginRp(server, sessionId, packet);
				}
				case FLoginAuthRq::kOpcode:
				{
					FLoginAuthRq packet;
					if (!NetworkLib::Packet::Serialization::DeserializeContentPacket(packetView, packet))
					{
						return false;
					}

					return HandleLoginAuthRq(server, sessionId, packet);
				}
				case FLoginAuthRp::kOpcode:
				{
					FLoginAuthRp packet;
					if (!NetworkLib::Packet::Serialization::DeserializeContentPacket(packetView, packet))
					{
						return false;
					}

					return HandleLoginAuthRp(server, sessionId, packet);
				}
				default:
					return OnUnhandledPacket(server, sessionId, packetView);
			}
		}

		bool HandleLoginRq(
			NetworkLib::IServer&,
			std::uint64_t,
			const FLoginRq&) override
		{
			return false;
		}

		bool HandleLoginRp(
			NetworkLib::IServer&,
			std::uint64_t,
			const FLoginRp&) override
		{
			return false;
		}

		bool HandleLoginAuthRq(
			NetworkLib::IServer&,
			std::uint64_t,
			const FLoginAuthRq&) override
		{
			return false;
		}

		bool HandleLoginAuthRp(
			NetworkLib::IServer&,
			std::uint64_t,
			const FLoginAuthRp&) override
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
