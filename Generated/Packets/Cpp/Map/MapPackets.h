#pragma once

namespace Generated::Map
{
	class FMapEnterRq final : public NetworkLib::Packet::Serialization::IContentPacket
	{
	public:
		static constexpr std::uint16_t kOpcode = 5000;
		static constexpr NetworkLib::Packet::Serialization::EContentPacketKind kPacketKind =
			NetworkLib::Packet::Serialization::EContentPacketKind::Request;

		std::uint64_t requestId{};
		std::uint32_t mapDataId{};

	public:
		std::uint16_t GetOpcode() const noexcept override
		{
			return kOpcode;
		}

		bool ContainsBorrowedViews() const noexcept override
		{
			return false;
		}

		std::size_t GetEstimatedBodySize() const noexcept override
		{
			return NetworkLib::Packet::Serialization::GetSerializedSize(requestId) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(mapDataId);
		}

		void Serialize(
			NetworkLib::Packet::Serialization::FPacketWriter& writer) const override
		{
			writer.Write(requestId);
			writer.Write(mapDataId);
		}

		bool Deserialize(
			NetworkLib::Packet::Serialization::FPacketReader& reader) override
		{
			return reader.Read(requestId) && reader.Read(mapDataId);
		}
	};

	class FMapEnterRp final : public NetworkLib::Packet::Serialization::IResponsePacket
	{
	public:
		static constexpr std::uint16_t kOpcode = 5001;
		static constexpr NetworkLib::Packet::Serialization::EContentPacketKind kPacketKind =
			NetworkLib::Packet::Serialization::EContentPacketKind::Response;

		std::uint16_t resultCode{};
		std::uint64_t requestId{};
		std::uint64_t mapInstanceId{};
		std::uint64_t entityId{};
		float positionX{};
		float positionY{};
		float directionX{};
		float directionY{};
		std::uint64_t serverTick{};
		std::uint64_t characterId{};
		std::uint32_t characterDataId{};
		std::uint32_t level{};
		std::uint64_t exp{};
		std::uint64_t requiredExpToNextLevel{};
		std::uint32_t strStat{};
		std::uint32_t dexStat{};
		std::uint32_t intStat{};
		std::uint32_t lukStat{};
		std::uint32_t unspentStatPoints{};
		std::uint64_t progressVersion{};
		std::uint64_t statVersion{};
		std::uint32_t finalStr{};
		std::uint32_t finalDex{};
		std::uint32_t finalInt{};
		std::uint32_t finalLuk{};
		std::uint32_t currentHp{};
		std::uint32_t maxHp{};
		std::uint32_t currentMp{};
		std::uint32_t maxMp{};
		std::uint32_t attack{};
		std::uint32_t defense{};
		std::uint32_t moveSpeedMilli{};
		std::uint64_t equipmentVersion{};
		std::uint64_t statRevision{};

	public:
		std::uint16_t GetOpcode() const noexcept override
		{
			return kOpcode;
		}

		bool ContainsBorrowedViews() const noexcept override
		{
			return false;
		}

		std::size_t GetEstimatedBodySize() const noexcept override
		{
			return NetworkLib::Packet::Serialization::GetSerializedSize(resultCode) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(requestId) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(mapInstanceId) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(entityId) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(positionX) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(positionY) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(directionX) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(directionY) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(serverTick) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(characterId) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(characterDataId) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(level) + NetworkLib::Packet::Serialization::GetSerializedSize(exp) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(requiredExpToNextLevel) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(strStat) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(dexStat) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(intStat) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(lukStat) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(unspentStatPoints) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(progressVersion) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(statVersion) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(finalStr) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(finalDex) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(finalInt) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(finalLuk) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(currentHp) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(maxHp) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(currentMp) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(maxMp) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(attack) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(defense) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(moveSpeedMilli) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(equipmentVersion) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(statRevision);
		}

		void Serialize(
			NetworkLib::Packet::Serialization::FPacketWriter& writer) const override
		{
			writer.Write(resultCode);
			writer.Write(requestId);
			writer.Write(mapInstanceId);
			writer.Write(entityId);
			writer.Write(positionX);
			writer.Write(positionY);
			writer.Write(directionX);
			writer.Write(directionY);
			writer.Write(serverTick);
			writer.Write(characterId);
			writer.Write(characterDataId);
			writer.Write(level);
			writer.Write(exp);
			writer.Write(requiredExpToNextLevel);
			writer.Write(strStat);
			writer.Write(dexStat);
			writer.Write(intStat);
			writer.Write(lukStat);
			writer.Write(unspentStatPoints);
			writer.Write(progressVersion);
			writer.Write(statVersion);
			writer.Write(finalStr);
			writer.Write(finalDex);
			writer.Write(finalInt);
			writer.Write(finalLuk);
			writer.Write(currentHp);
			writer.Write(maxHp);
			writer.Write(currentMp);
			writer.Write(maxMp);
			writer.Write(attack);
			writer.Write(defense);
			writer.Write(moveSpeedMilli);
			writer.Write(equipmentVersion);
			writer.Write(statRevision);
		}

		bool Deserialize(
			NetworkLib::Packet::Serialization::FPacketReader& reader) override
		{
			return reader.Read(resultCode) && reader.Read(requestId) && reader.Read(mapInstanceId) && reader.Read(entityId) &&
				   reader.Read(positionX) && reader.Read(positionY) && reader.Read(directionX) && reader.Read(directionY) &&
				   reader.Read(serverTick) && reader.Read(characterId) && reader.Read(characterDataId) && reader.Read(level) &&
				   reader.Read(exp) && reader.Read(requiredExpToNextLevel) && reader.Read(strStat) && reader.Read(dexStat) &&
				   reader.Read(intStat) && reader.Read(lukStat) && reader.Read(unspentStatPoints) && reader.Read(progressVersion) &&
				   reader.Read(statVersion) && reader.Read(finalStr) && reader.Read(finalDex) && reader.Read(finalInt) &&
				   reader.Read(finalLuk) && reader.Read(currentHp) && reader.Read(maxHp) && reader.Read(currentMp) && reader.Read(maxMp) &&
				   reader.Read(attack) && reader.Read(defense) && reader.Read(moveSpeedMilli) && reader.Read(equipmentVersion) &&
				   reader.Read(statRevision);
		}
	};

	class FActorSpawnNoti final : public NetworkLib::Packet::Serialization::IContentPacket
	{
	public:
		static constexpr std::uint16_t kOpcode = 5002;
		static constexpr NetworkLib::Packet::Serialization::EContentPacketKind kPacketKind =
			NetworkLib::Packet::Serialization::EContentPacketKind::Notification;

		std::uint64_t entityId{};
		std::uint8_t actorKind{};
		std::uint32_t actorDataId{};
		float positionX{};
		float positionY{};
		float directionX{};
		float directionY{};
		std::uint32_t moveSequence{};
		std::uint8_t moveState{};
		std::uint64_t serverTick{};
		std::uint32_t currentHp{};
		std::uint32_t maxHp{};
		std::uint64_t lifeRevision{};

	public:
		std::uint16_t GetOpcode() const noexcept override
		{
			return kOpcode;
		}

		bool ContainsBorrowedViews() const noexcept override
		{
			return false;
		}

		std::size_t GetEstimatedBodySize() const noexcept override
		{
			return NetworkLib::Packet::Serialization::GetSerializedSize(entityId) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(actorKind) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(actorDataId) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(positionX) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(positionY) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(directionX) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(directionY) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(moveSequence) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(moveState) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(serverTick) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(currentHp) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(maxHp) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(lifeRevision);
		}

		void Serialize(
			NetworkLib::Packet::Serialization::FPacketWriter& writer) const override
		{
			writer.Write(entityId);
			writer.Write(actorKind);
			writer.Write(actorDataId);
			writer.Write(positionX);
			writer.Write(positionY);
			writer.Write(directionX);
			writer.Write(directionY);
			writer.Write(moveSequence);
			writer.Write(moveState);
			writer.Write(serverTick);
			writer.Write(currentHp);
			writer.Write(maxHp);
			writer.Write(lifeRevision);
		}

		bool Deserialize(
			NetworkLib::Packet::Serialization::FPacketReader& reader) override
		{
			return reader.Read(entityId) && reader.Read(actorKind) && reader.Read(actorDataId) && reader.Read(positionX) &&
				   reader.Read(positionY) && reader.Read(directionX) && reader.Read(directionY) && reader.Read(moveSequence) &&
				   reader.Read(moveState) && reader.Read(serverTick) && reader.Read(currentHp) && reader.Read(maxHp) &&
				   reader.Read(lifeRevision);
		}
	};

	class FActorDespawnNoti final : public NetworkLib::Packet::Serialization::IContentPacket
	{
	public:
		static constexpr std::uint16_t kOpcode = 5003;
		static constexpr NetworkLib::Packet::Serialization::EContentPacketKind kPacketKind =
			NetworkLib::Packet::Serialization::EContentPacketKind::Notification;

		std::uint64_t entityId{};

	public:
		std::uint16_t GetOpcode() const noexcept override
		{
			return kOpcode;
		}

		bool ContainsBorrowedViews() const noexcept override
		{
			return false;
		}

		std::size_t GetEstimatedBodySize() const noexcept override
		{
			return NetworkLib::Packet::Serialization::GetSerializedSize(entityId);
		}

		void Serialize(
			NetworkLib::Packet::Serialization::FPacketWriter& writer) const override
		{
			writer.Write(entityId);
		}

		bool Deserialize(
			NetworkLib::Packet::Serialization::FPacketReader& reader) override
		{
			return reader.Read(entityId);
		}
	};

	class FMoveRq final : public NetworkLib::Packet::Serialization::IContentPacket
	{
	public:
		static constexpr std::uint16_t kOpcode = 5004;
		static constexpr NetworkLib::Packet::Serialization::EContentPacketKind kPacketKind =
			NetworkLib::Packet::Serialization::EContentPacketKind::Request;

		std::uint32_t sequence{};
		std::uint8_t moveState{};
		float clientPositionX{};
		float clientPositionY{};
		float directionX{};
		float directionY{};

	public:
		std::uint16_t GetOpcode() const noexcept override
		{
			return kOpcode;
		}

		bool ContainsBorrowedViews() const noexcept override
		{
			return false;
		}

		std::size_t GetEstimatedBodySize() const noexcept override
		{
			return NetworkLib::Packet::Serialization::GetSerializedSize(sequence) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(moveState) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(clientPositionX) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(clientPositionY) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(directionX) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(directionY);
		}

		void Serialize(
			NetworkLib::Packet::Serialization::FPacketWriter& writer) const override
		{
			writer.Write(sequence);
			writer.Write(moveState);
			writer.Write(clientPositionX);
			writer.Write(clientPositionY);
			writer.Write(directionX);
			writer.Write(directionY);
		}

		bool Deserialize(
			NetworkLib::Packet::Serialization::FPacketReader& reader) override
		{
			return reader.Read(sequence) && reader.Read(moveState) && reader.Read(clientPositionX) && reader.Read(clientPositionY) &&
				   reader.Read(directionX) && reader.Read(directionY);
		}
	};

	class FMoveRp final : public NetworkLib::Packet::Serialization::IResponsePacket
	{
	public:
		static constexpr std::uint16_t kOpcode = 5005;
		static constexpr NetworkLib::Packet::Serialization::EContentPacketKind kPacketKind =
			NetworkLib::Packet::Serialization::EContentPacketKind::Response;

		std::uint16_t resultCode{};
		std::uint32_t sequence{};
		std::uint8_t moveState{};
		float acceptedPositionX{};
		float acceptedPositionY{};
		float directionX{};
		float directionY{};
		bool isCorrected{};

	public:
		std::uint16_t GetOpcode() const noexcept override
		{
			return kOpcode;
		}

		bool ContainsBorrowedViews() const noexcept override
		{
			return false;
		}

		std::size_t GetEstimatedBodySize() const noexcept override
		{
			return NetworkLib::Packet::Serialization::GetSerializedSize(resultCode) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(sequence) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(moveState) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(acceptedPositionX) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(acceptedPositionY) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(directionX) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(directionY) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(isCorrected);
		}

		void Serialize(
			NetworkLib::Packet::Serialization::FPacketWriter& writer) const override
		{
			writer.Write(resultCode);
			writer.Write(sequence);
			writer.Write(moveState);
			writer.Write(acceptedPositionX);
			writer.Write(acceptedPositionY);
			writer.Write(directionX);
			writer.Write(directionY);
			writer.Write(isCorrected);
		}

		bool Deserialize(
			NetworkLib::Packet::Serialization::FPacketReader& reader) override
		{
			return reader.Read(resultCode) && reader.Read(sequence) && reader.Read(moveState) && reader.Read(acceptedPositionX) &&
				   reader.Read(acceptedPositionY) && reader.Read(directionX) && reader.Read(directionY) && reader.Read(isCorrected);
		}
	};

	class FMoveNoti final : public NetworkLib::Packet::Serialization::IContentPacket
	{
	public:
		static constexpr std::uint16_t kOpcode = 5006;
		static constexpr NetworkLib::Packet::Serialization::EContentPacketKind kPacketKind =
			NetworkLib::Packet::Serialization::EContentPacketKind::Notification;

		std::uint64_t entityId{};
		std::uint32_t sequence{};
		std::uint8_t moveState{};
		float positionX{};
		float positionY{};
		float directionX{};
		float directionY{};
		std::uint64_t serverTick{};

	public:
		std::uint16_t GetOpcode() const noexcept override
		{
			return kOpcode;
		}

		bool ContainsBorrowedViews() const noexcept override
		{
			return false;
		}

		std::size_t GetEstimatedBodySize() const noexcept override
		{
			return NetworkLib::Packet::Serialization::GetSerializedSize(entityId) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(sequence) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(moveState) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(positionX) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(positionY) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(directionX) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(directionY) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(serverTick);
		}

		void Serialize(
			NetworkLib::Packet::Serialization::FPacketWriter& writer) const override
		{
			writer.Write(entityId);
			writer.Write(sequence);
			writer.Write(moveState);
			writer.Write(positionX);
			writer.Write(positionY);
			writer.Write(directionX);
			writer.Write(directionY);
			writer.Write(serverTick);
		}

		bool Deserialize(
			NetworkLib::Packet::Serialization::FPacketReader& reader) override
		{
			return reader.Read(entityId) && reader.Read(sequence) && reader.Read(moveState) && reader.Read(positionX) &&
				   reader.Read(positionY) && reader.Read(directionX) && reader.Read(directionY) && reader.Read(serverTick);
		}
	};

	class FActorAttackNoti final : public NetworkLib::Packet::Serialization::IContentPacket
	{
	public:
		static constexpr std::uint16_t kOpcode = 5011;
		static constexpr NetworkLib::Packet::Serialization::EContentPacketKind kPacketKind =
			NetworkLib::Packet::Serialization::EContentPacketKind::Notification;

		std::uint64_t attackerEntityId{};
		std::uint64_t targetEntityId{};
		std::uint32_t damage{};
		std::uint32_t targetCurrentHp{};
		std::uint32_t targetMaxHp{};
		std::uint64_t serverTick{};

	public:
		std::uint16_t GetOpcode() const noexcept override
		{
			return kOpcode;
		}

		bool ContainsBorrowedViews() const noexcept override
		{
			return false;
		}

		std::size_t GetEstimatedBodySize() const noexcept override
		{
			return NetworkLib::Packet::Serialization::GetSerializedSize(attackerEntityId) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(targetEntityId) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(damage) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(targetCurrentHp) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(targetMaxHp) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(serverTick);
		}

		void Serialize(
			NetworkLib::Packet::Serialization::FPacketWriter& writer) const override
		{
			writer.Write(attackerEntityId);
			writer.Write(targetEntityId);
			writer.Write(damage);
			writer.Write(targetCurrentHp);
			writer.Write(targetMaxHp);
			writer.Write(serverTick);
		}

		bool Deserialize(
			NetworkLib::Packet::Serialization::FPacketReader& reader) override
		{
			return reader.Read(attackerEntityId) && reader.Read(targetEntityId) && reader.Read(damage) && reader.Read(targetCurrentHp) &&
				   reader.Read(targetMaxHp) && reader.Read(serverTick);
		}
	};

	class FActorDeathNoti final : public NetworkLib::Packet::Serialization::IContentPacket
	{
	public:
		static constexpr std::uint16_t kOpcode = 5012;
		static constexpr NetworkLib::Packet::Serialization::EContentPacketKind kPacketKind =
			NetworkLib::Packet::Serialization::EContentPacketKind::Notification;

		std::uint64_t entityId{};
		std::uint64_t killerEntityId{};
		std::uint64_t lifeRevision{};
		std::uint64_t serverTick{};

	public:
		std::uint16_t GetOpcode() const noexcept override
		{
			return kOpcode;
		}

		bool ContainsBorrowedViews() const noexcept override
		{
			return false;
		}

		std::size_t GetEstimatedBodySize() const noexcept override
		{
			return NetworkLib::Packet::Serialization::GetSerializedSize(entityId) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(killerEntityId) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(lifeRevision) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(serverTick);
		}

		void Serialize(
			NetworkLib::Packet::Serialization::FPacketWriter& writer) const override
		{
			writer.Write(entityId);
			writer.Write(killerEntityId);
			writer.Write(lifeRevision);
			writer.Write(serverTick);
		}

		bool Deserialize(
			NetworkLib::Packet::Serialization::FPacketReader& reader) override
		{
			return reader.Read(entityId) && reader.Read(killerEntityId) && reader.Read(lifeRevision) && reader.Read(serverTick);
		}
	};

	class FActorRespawnNoti final : public NetworkLib::Packet::Serialization::IContentPacket
	{
	public:
		static constexpr std::uint16_t kOpcode = 5013;
		static constexpr NetworkLib::Packet::Serialization::EContentPacketKind kPacketKind =
			NetworkLib::Packet::Serialization::EContentPacketKind::Notification;

		std::uint64_t entityId{};
		float positionX{};
		float positionY{};
		float directionX{};
		float directionY{};
		std::uint32_t currentHp{};
		std::uint32_t maxHp{};
		std::uint64_t lifeRevision{};
		std::uint64_t serverTick{};

	public:
		std::uint16_t GetOpcode() const noexcept override
		{
			return kOpcode;
		}

		bool ContainsBorrowedViews() const noexcept override
		{
			return false;
		}

		std::size_t GetEstimatedBodySize() const noexcept override
		{
			return NetworkLib::Packet::Serialization::GetSerializedSize(entityId) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(positionX) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(positionY) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(directionX) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(directionY) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(currentHp) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(maxHp) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(lifeRevision) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(serverTick);
		}

		void Serialize(
			NetworkLib::Packet::Serialization::FPacketWriter& writer) const override
		{
			writer.Write(entityId);
			writer.Write(positionX);
			writer.Write(positionY);
			writer.Write(directionX);
			writer.Write(directionY);
			writer.Write(currentHp);
			writer.Write(maxHp);
			writer.Write(lifeRevision);
			writer.Write(serverTick);
		}

		bool Deserialize(
			NetworkLib::Packet::Serialization::FPacketReader& reader) override
		{
			return reader.Read(entityId) && reader.Read(positionX) && reader.Read(positionY) && reader.Read(directionX) &&
				   reader.Read(directionY) && reader.Read(currentHp) && reader.Read(maxHp) && reader.Read(lifeRevision) &&
				   reader.Read(serverTick);
		}
	};

	class FBasicAttackRq final : public NetworkLib::Packet::Serialization::IContentPacket
	{
	public:
		static constexpr std::uint16_t kOpcode = 5014;
		static constexpr NetworkLib::Packet::Serialization::EContentPacketKind kPacketKind =
			NetworkLib::Packet::Serialization::EContentPacketKind::Request;

		std::uint32_t attackSequence{};
		std::uint64_t targetEntityId{};

	public:
		std::uint16_t GetOpcode() const noexcept override
		{
			return kOpcode;
		}

		bool ContainsBorrowedViews() const noexcept override
		{
			return false;
		}

		std::size_t GetEstimatedBodySize() const noexcept override
		{
			return NetworkLib::Packet::Serialization::GetSerializedSize(attackSequence) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(targetEntityId);
		}

		void Serialize(
			NetworkLib::Packet::Serialization::FPacketWriter& writer) const override
		{
			writer.Write(attackSequence);
			writer.Write(targetEntityId);
		}

		bool Deserialize(
			NetworkLib::Packet::Serialization::FPacketReader& reader) override
		{
			return reader.Read(attackSequence) && reader.Read(targetEntityId);
		}
	};

	class FBasicAttackRp final : public NetworkLib::Packet::Serialization::IResponsePacket
	{
	public:
		static constexpr std::uint16_t kOpcode = 5015;
		static constexpr NetworkLib::Packet::Serialization::EContentPacketKind kPacketKind =
			NetworkLib::Packet::Serialization::EContentPacketKind::Response;

		std::uint16_t resultCode{};
		std::uint32_t attackSequence{};
		std::uint64_t serverTick{};

	public:
		std::uint16_t GetOpcode() const noexcept override
		{
			return kOpcode;
		}

		bool ContainsBorrowedViews() const noexcept override
		{
			return false;
		}

		std::size_t GetEstimatedBodySize() const noexcept override
		{
			return NetworkLib::Packet::Serialization::GetSerializedSize(resultCode) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(attackSequence) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(serverTick);
		}

		void Serialize(
			NetworkLib::Packet::Serialization::FPacketWriter& writer) const override
		{
			writer.Write(resultCode);
			writer.Write(attackSequence);
			writer.Write(serverTick);
		}

		bool Deserialize(
			NetworkLib::Packet::Serialization::FPacketReader& reader) override
		{
			return reader.Read(resultCode) && reader.Read(attackSequence) && reader.Read(serverTick);
		}
	};

}
