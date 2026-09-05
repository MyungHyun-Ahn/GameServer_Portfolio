#pragma once

namespace Generated::World
{
	class FWorldAuthRq final : public NetworkLib::Packet::Serialization::IContentPacket
	{
	public:
		static constexpr std::uint16_t kOpcode = 4998;
		static constexpr NetworkLib::Packet::Serialization::EContentPacketKind kPacketKind =
			NetworkLib::Packet::Serialization::EContentPacketKind::Request;

		std::uint64_t requestId{};
		std::string ticket{};

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
				   NetworkLib::Packet::Serialization::GetSerializedSize(ticket);
		}

		void Serialize(
			NetworkLib::Packet::Serialization::FPacketWriter& writer) const override
		{
			writer.Write(requestId);
			writer.Write(ticket);
		}

		bool Deserialize(
			NetworkLib::Packet::Serialization::FPacketReader& reader) override
		{
			return reader.Read(requestId) && reader.Read(ticket);
		}
	};

	class FWorldAuthRp final : public NetworkLib::Packet::Serialization::IResponsePacket
	{
	public:
		static constexpr std::uint16_t kOpcode = 4999;
		static constexpr NetworkLib::Packet::Serialization::EContentPacketKind kPacketKind =
			NetworkLib::Packet::Serialization::EContentPacketKind::Response;

		std::uint16_t resultCode{};
		std::uint64_t requestId{};
		std::uint64_t userId{};

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
				   NetworkLib::Packet::Serialization::GetSerializedSize(userId);
		}

		void Serialize(
			NetworkLib::Packet::Serialization::FPacketWriter& writer) const override
		{
			writer.Write(resultCode);
			writer.Write(requestId);
			writer.Write(userId);
		}

		bool Deserialize(
			NetworkLib::Packet::Serialization::FPacketReader& reader) override
		{
			return reader.Read(resultCode) && reader.Read(requestId) && reader.Read(userId);
		}
	};

	class FEquipItemRq final : public NetworkLib::Packet::Serialization::IContentPacket
	{
	public:
		static constexpr std::uint16_t kOpcode = 5007;
		static constexpr NetworkLib::Packet::Serialization::EContentPacketKind kPacketKind =
			NetworkLib::Packet::Serialization::EContentPacketKind::Request;

		std::uint64_t requestId{};
		std::uint64_t itemInstanceId{};
		std::uint64_t expectedItemVersion{};

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
				   NetworkLib::Packet::Serialization::GetSerializedSize(itemInstanceId) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(expectedItemVersion);
		}

		void Serialize(
			NetworkLib::Packet::Serialization::FPacketWriter& writer) const override
		{
			writer.Write(requestId);
			writer.Write(itemInstanceId);
			writer.Write(expectedItemVersion);
		}

		bool Deserialize(
			NetworkLib::Packet::Serialization::FPacketReader& reader) override
		{
			return reader.Read(requestId) && reader.Read(itemInstanceId) && reader.Read(expectedItemVersion);
		}
	};

	class FEquipItemRp final : public NetworkLib::Packet::Serialization::IResponsePacket
	{
	public:
		static constexpr std::uint16_t kOpcode = 5008;
		static constexpr NetworkLib::Packet::Serialization::EContentPacketKind kPacketKind =
			NetworkLib::Packet::Serialization::EContentPacketKind::Response;

		std::uint16_t resultCode{};
		std::uint64_t requestId{};
		std::uint64_t itemInstanceId{};
		std::uint64_t itemVersion{};
		bool equipped{};
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
				   NetworkLib::Packet::Serialization::GetSerializedSize(itemInstanceId) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(itemVersion) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(equipped) +
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
			writer.Write(itemInstanceId);
			writer.Write(itemVersion);
			writer.Write(equipped);
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
			return reader.Read(resultCode) && reader.Read(requestId) && reader.Read(itemInstanceId) && reader.Read(itemVersion) &&
				   reader.Read(equipped) && reader.Read(finalStr) && reader.Read(finalDex) && reader.Read(finalInt) &&
				   reader.Read(finalLuk) && reader.Read(currentHp) && reader.Read(maxHp) && reader.Read(currentMp) && reader.Read(maxMp) &&
				   reader.Read(attack) && reader.Read(defense) && reader.Read(moveSpeedMilli) && reader.Read(equipmentVersion) &&
				   reader.Read(statRevision);
		}
	};

	class FUnequipItemRq final : public NetworkLib::Packet::Serialization::IContentPacket
	{
	public:
		static constexpr std::uint16_t kOpcode = 5009;
		static constexpr NetworkLib::Packet::Serialization::EContentPacketKind kPacketKind =
			NetworkLib::Packet::Serialization::EContentPacketKind::Request;

		std::uint64_t requestId{};
		std::uint64_t itemInstanceId{};
		std::uint64_t expectedItemVersion{};

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
				   NetworkLib::Packet::Serialization::GetSerializedSize(itemInstanceId) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(expectedItemVersion);
		}

		void Serialize(
			NetworkLib::Packet::Serialization::FPacketWriter& writer) const override
		{
			writer.Write(requestId);
			writer.Write(itemInstanceId);
			writer.Write(expectedItemVersion);
		}

		bool Deserialize(
			NetworkLib::Packet::Serialization::FPacketReader& reader) override
		{
			return reader.Read(requestId) && reader.Read(itemInstanceId) && reader.Read(expectedItemVersion);
		}
	};

	class FUnequipItemRp final : public NetworkLib::Packet::Serialization::IResponsePacket
	{
	public:
		static constexpr std::uint16_t kOpcode = 5010;
		static constexpr NetworkLib::Packet::Serialization::EContentPacketKind kPacketKind =
			NetworkLib::Packet::Serialization::EContentPacketKind::Response;

		std::uint16_t resultCode{};
		std::uint64_t requestId{};
		std::uint64_t itemInstanceId{};
		std::uint64_t itemVersion{};
		bool equipped{};
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
				   NetworkLib::Packet::Serialization::GetSerializedSize(itemInstanceId) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(itemVersion) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(equipped) +
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
			writer.Write(itemInstanceId);
			writer.Write(itemVersion);
			writer.Write(equipped);
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
			return reader.Read(resultCode) && reader.Read(requestId) && reader.Read(itemInstanceId) && reader.Read(itemVersion) &&
				   reader.Read(equipped) && reader.Read(finalStr) && reader.Read(finalDex) && reader.Read(finalInt) &&
				   reader.Read(finalLuk) && reader.Read(currentHp) && reader.Read(maxHp) && reader.Read(currentMp) && reader.Read(maxMp) &&
				   reader.Read(attack) && reader.Read(defense) && reader.Read(moveSpeedMilli) && reader.Read(equipmentVersion) &&
				   reader.Read(statRevision);
		}
	};

}
