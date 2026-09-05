#pragma once

namespace Generated::Map
{
	class IMapPacketHandler
	{
	public:
		virtual ~IMapPacketHandler() = default;

		virtual bool HandleMapEnterRq(NetworkLib::IServer& server, std::uint64_t sessionId, const FMapEnterRq& packet) = 0;
		virtual bool HandleMapEnterRp(NetworkLib::IServer& server, std::uint64_t sessionId, const FMapEnterRp& packet) = 0;
		virtual bool HandleActorSpawnNoti(NetworkLib::IServer& server, std::uint64_t sessionId, const FActorSpawnNoti& packet) = 0;
		virtual bool HandleActorDespawnNoti(NetworkLib::IServer& server, std::uint64_t sessionId, const FActorDespawnNoti& packet) = 0;
		virtual bool HandleMoveRq(NetworkLib::IServer& server, std::uint64_t sessionId, const FMoveRq& packet) = 0;
		virtual bool HandleMoveRp(NetworkLib::IServer& server, std::uint64_t sessionId, const FMoveRp& packet) = 0;
		virtual bool HandleMoveNoti(NetworkLib::IServer& server, std::uint64_t sessionId, const FMoveNoti& packet) = 0;
		virtual bool HandleActorAttackNoti(NetworkLib::IServer& server, std::uint64_t sessionId, const FActorAttackNoti& packet) = 0;
		virtual bool HandleActorDeathNoti(NetworkLib::IServer& server, std::uint64_t sessionId, const FActorDeathNoti& packet) = 0;
		virtual bool HandleActorRespawnNoti(NetworkLib::IServer& server, std::uint64_t sessionId, const FActorRespawnNoti& packet) = 0;
		virtual bool HandleBasicAttackRq(NetworkLib::IServer& server, std::uint64_t sessionId, const FBasicAttackRq& packet) = 0;
		virtual bool HandleBasicAttackRp(NetworkLib::IServer& server, std::uint64_t sessionId, const FBasicAttackRp& packet) = 0;
	};

	class IMapPacketDispatcher
	{
	public:
		virtual ~IMapPacketDispatcher() = default;
		virtual bool DispatchPacket(NetworkLib::IServer& server,
			std::uint64_t sessionId,
			const NetworkLib::Packet::View::FPacketView& packetView) = 0;
	};

	class FMapPacketHandlerBase : public IMapPacketHandler, public IMapPacketDispatcher
	{
	public:
		bool DispatchPacket(
			NetworkLib::IServer& server,
			std::uint64_t sessionId,
			const NetworkLib::Packet::View::FPacketView& packetView) override
		{
			switch (packetView.opcode)
			{
				case FMapEnterRq::kOpcode:
				{
					FMapEnterRq packet;
					if (!NetworkLib::Packet::Serialization::DeserializeContentPacket(packetView, packet))
					{
						return false;
					}

					return HandleMapEnterRq(server, sessionId, packet);
				}
				case FMapEnterRp::kOpcode:
				{
					FMapEnterRp packet;
					if (!NetworkLib::Packet::Serialization::DeserializeContentPacket(packetView, packet))
					{
						return false;
					}

					return HandleMapEnterRp(server, sessionId, packet);
				}
				case FActorSpawnNoti::kOpcode:
				{
					FActorSpawnNoti packet;
					if (!NetworkLib::Packet::Serialization::DeserializeContentPacket(packetView, packet))
					{
						return false;
					}

					return HandleActorSpawnNoti(server, sessionId, packet);
				}
				case FActorDespawnNoti::kOpcode:
				{
					FActorDespawnNoti packet;
					if (!NetworkLib::Packet::Serialization::DeserializeContentPacket(packetView, packet))
					{
						return false;
					}

					return HandleActorDespawnNoti(server, sessionId, packet);
				}
				case FMoveRq::kOpcode:
				{
					FMoveRq packet;
					if (!NetworkLib::Packet::Serialization::DeserializeContentPacket(packetView, packet))
					{
						return false;
					}

					return HandleMoveRq(server, sessionId, packet);
				}
				case FMoveRp::kOpcode:
				{
					FMoveRp packet;
					if (!NetworkLib::Packet::Serialization::DeserializeContentPacket(packetView, packet))
					{
						return false;
					}

					return HandleMoveRp(server, sessionId, packet);
				}
				case FMoveNoti::kOpcode:
				{
					FMoveNoti packet;
					if (!NetworkLib::Packet::Serialization::DeserializeContentPacket(packetView, packet))
					{
						return false;
					}

					return HandleMoveNoti(server, sessionId, packet);
				}
				case FActorAttackNoti::kOpcode:
				{
					FActorAttackNoti packet;
					if (!NetworkLib::Packet::Serialization::DeserializeContentPacket(packetView, packet))
					{
						return false;
					}

					return HandleActorAttackNoti(server, sessionId, packet);
				}
				case FActorDeathNoti::kOpcode:
				{
					FActorDeathNoti packet;
					if (!NetworkLib::Packet::Serialization::DeserializeContentPacket(packetView, packet))
					{
						return false;
					}

					return HandleActorDeathNoti(server, sessionId, packet);
				}
				case FActorRespawnNoti::kOpcode:
				{
					FActorRespawnNoti packet;
					if (!NetworkLib::Packet::Serialization::DeserializeContentPacket(packetView, packet))
					{
						return false;
					}

					return HandleActorRespawnNoti(server, sessionId, packet);
				}
				case FBasicAttackRq::kOpcode:
				{
					FBasicAttackRq packet;
					if (!NetworkLib::Packet::Serialization::DeserializeContentPacket(packetView, packet))
					{
						return false;
					}

					return HandleBasicAttackRq(server, sessionId, packet);
				}
				case FBasicAttackRp::kOpcode:
				{
					FBasicAttackRp packet;
					if (!NetworkLib::Packet::Serialization::DeserializeContentPacket(packetView, packet))
					{
						return false;
					}

					return HandleBasicAttackRp(server, sessionId, packet);
				}
				default:
					return OnUnhandledPacket(server, sessionId, packetView);
			}
		}

		bool HandleMapEnterRq(
			NetworkLib::IServer&,
			std::uint64_t,
			const FMapEnterRq&) override
		{
			return false;
		}

		bool HandleMapEnterRp(
			NetworkLib::IServer&,
			std::uint64_t,
			const FMapEnterRp&) override
		{
			return false;
		}

		bool HandleActorSpawnNoti(
			NetworkLib::IServer&,
			std::uint64_t,
			const FActorSpawnNoti&) override
		{
			return false;
		}

		bool HandleActorDespawnNoti(
			NetworkLib::IServer&,
			std::uint64_t,
			const FActorDespawnNoti&) override
		{
			return false;
		}

		bool HandleMoveRq(
			NetworkLib::IServer&,
			std::uint64_t,
			const FMoveRq&) override
		{
			return false;
		}

		bool HandleMoveRp(
			NetworkLib::IServer&,
			std::uint64_t,
			const FMoveRp&) override
		{
			return false;
		}

		bool HandleMoveNoti(
			NetworkLib::IServer&,
			std::uint64_t,
			const FMoveNoti&) override
		{
			return false;
		}

		bool HandleActorAttackNoti(
			NetworkLib::IServer&,
			std::uint64_t,
			const FActorAttackNoti&) override
		{
			return false;
		}

		bool HandleActorDeathNoti(
			NetworkLib::IServer&,
			std::uint64_t,
			const FActorDeathNoti&) override
		{
			return false;
		}

		bool HandleActorRespawnNoti(
			NetworkLib::IServer&,
			std::uint64_t,
			const FActorRespawnNoti&) override
		{
			return false;
		}

		bool HandleBasicAttackRq(
			NetworkLib::IServer&,
			std::uint64_t,
			const FBasicAttackRq&) override
		{
			return false;
		}

		bool HandleBasicAttackRp(
			NetworkLib::IServer&,
			std::uint64_t,
			const FBasicAttackRp&) override
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
