#pragma once

namespace Generated::World
{
	class IWorldPacketHandler
	{
	public:
		virtual ~IWorldPacketHandler() = default;

		virtual bool HandleWorldAuthRq(NetworkLib::IServer& server, std::uint64_t sessionId, const FWorldAuthRq& packet) = 0;
		virtual bool HandleWorldAuthRp(NetworkLib::IServer& server, std::uint64_t sessionId, const FWorldAuthRp& packet) = 0;
		virtual bool HandleEquipItemRq(NetworkLib::IServer& server, std::uint64_t sessionId, const FEquipItemRq& packet) = 0;
		virtual bool HandleEquipItemRp(NetworkLib::IServer& server, std::uint64_t sessionId, const FEquipItemRp& packet) = 0;
		virtual bool HandleUnequipItemRq(NetworkLib::IServer& server, std::uint64_t sessionId, const FUnequipItemRq& packet) = 0;
		virtual bool HandleUnequipItemRp(NetworkLib::IServer& server, std::uint64_t sessionId, const FUnequipItemRp& packet) = 0;
	};

	class IWorldPacketDispatcher
	{
	public:
		virtual ~IWorldPacketDispatcher() = default;
		virtual bool DispatchPacket(NetworkLib::IServer& server,
			std::uint64_t sessionId,
			const NetworkLib::Packet::View::FPacketView& packetView) = 0;
	};

	class FWorldPacketHandlerBase : public IWorldPacketHandler, public IWorldPacketDispatcher
	{
	public:
		bool DispatchPacket(
			NetworkLib::IServer& server,
			std::uint64_t sessionId,
			const NetworkLib::Packet::View::FPacketView& packetView) override
		{
			switch (packetView.opcode)
			{
				case FWorldAuthRq::kOpcode:
				{
					FWorldAuthRq packet;
					if (!NetworkLib::Packet::Serialization::DeserializeContentPacket(packetView, packet))
					{
						return false;
					}

					return HandleWorldAuthRq(server, sessionId, packet);
				}
				case FWorldAuthRp::kOpcode:
				{
					FWorldAuthRp packet;
					if (!NetworkLib::Packet::Serialization::DeserializeContentPacket(packetView, packet))
					{
						return false;
					}

					return HandleWorldAuthRp(server, sessionId, packet);
				}
				case FEquipItemRq::kOpcode:
				{
					FEquipItemRq packet;
					if (!NetworkLib::Packet::Serialization::DeserializeContentPacket(packetView, packet))
					{
						return false;
					}

					return HandleEquipItemRq(server, sessionId, packet);
				}
				case FEquipItemRp::kOpcode:
				{
					FEquipItemRp packet;
					if (!NetworkLib::Packet::Serialization::DeserializeContentPacket(packetView, packet))
					{
						return false;
					}

					return HandleEquipItemRp(server, sessionId, packet);
				}
				case FUnequipItemRq::kOpcode:
				{
					FUnequipItemRq packet;
					if (!NetworkLib::Packet::Serialization::DeserializeContentPacket(packetView, packet))
					{
						return false;
					}

					return HandleUnequipItemRq(server, sessionId, packet);
				}
				case FUnequipItemRp::kOpcode:
				{
					FUnequipItemRp packet;
					if (!NetworkLib::Packet::Serialization::DeserializeContentPacket(packetView, packet))
					{
						return false;
					}

					return HandleUnequipItemRp(server, sessionId, packet);
				}
				default:
					return OnUnhandledPacket(server, sessionId, packetView);
			}
		}

		bool HandleWorldAuthRq(
			NetworkLib::IServer&,
			std::uint64_t,
			const FWorldAuthRq&) override
		{
			return false;
		}

		bool HandleWorldAuthRp(
			NetworkLib::IServer&,
			std::uint64_t,
			const FWorldAuthRp&) override
		{
			return false;
		}

		bool HandleEquipItemRq(
			NetworkLib::IServer&,
			std::uint64_t,
			const FEquipItemRq&) override
		{
			return false;
		}

		bool HandleEquipItemRp(
			NetworkLib::IServer&,
			std::uint64_t,
			const FEquipItemRp&) override
		{
			return false;
		}

		bool HandleUnequipItemRq(
			NetworkLib::IServer&,
			std::uint64_t,
			const FUnequipItemRq&) override
		{
			return false;
		}

		bool HandleUnequipItemRp(
			NetworkLib::IServer&,
			std::uint64_t,
			const FUnequipItemRp&) override
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
