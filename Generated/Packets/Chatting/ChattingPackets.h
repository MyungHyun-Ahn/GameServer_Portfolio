#pragma once

namespace Generated::Chatting
{
	class FRoomListRq final : public NetworkLib::Packet::Serialization::IContentPacket
	{
	public:
		static constexpr std::uint16_t kOpcode = 3100;

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
			return 0;
		}

		void Serialize(
			NetworkLib::Packet::Serialization::FPacketWriter& writer) const override
		{
		}

		bool Deserialize(
			NetworkLib::Packet::Serialization::FPacketReader& reader) override
		{
			return true;
		}
	};

	class FRoomListRp final : public NetworkLib::Packet::Serialization::IContentPacket
	{
	public:
		static constexpr std::uint16_t kOpcode = 3101;

		std::vector<std::uint32_t> roomIds{};
		std::vector<std::string> roomNames{};
		std::vector<std::uint32_t> participantCounts{};
		std::vector<std::uint32_t> capacities{};
		std::vector<std::uint8_t> joinableFlags{};

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
			return NetworkLib::Packet::Serialization::GetSerializedSize(roomIds) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(roomNames) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(participantCounts) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(capacities) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(joinableFlags);
		}

		void Serialize(
			NetworkLib::Packet::Serialization::FPacketWriter& writer) const override
		{
			writer.Write(roomIds);
			writer.Write(roomNames);
			writer.Write(participantCounts);
			writer.Write(capacities);
			writer.Write(joinableFlags);
		}

		bool Deserialize(
			NetworkLib::Packet::Serialization::FPacketReader& reader) override
		{
			return reader.Read(roomIds) && reader.Read(roomNames) && reader.Read(participantCounts) && reader.Read(capacities) &&
				   reader.Read(joinableFlags);
		}
	};

	class FRoomChangeRq final : public NetworkLib::Packet::Serialization::IContentPacket
	{
	public:
		static constexpr std::uint16_t kOpcode = 3102;

		std::uint32_t targetRoomId{};

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
			return NetworkLib::Packet::Serialization::GetSerializedSize(targetRoomId);
		}

		void Serialize(
			NetworkLib::Packet::Serialization::FPacketWriter& writer) const override
		{
			writer.Write(targetRoomId);
		}

		bool Deserialize(
			NetworkLib::Packet::Serialization::FPacketReader& reader) override
		{
			return reader.Read(targetRoomId);
		}
	};

	class FRoomChangeRp final : public NetworkLib::Packet::Serialization::IContentPacket
	{
	public:
		static constexpr std::uint16_t kOpcode = 3103;

		std::uint32_t previousRoomId{};
		std::uint32_t currentRoomId{};
		bool success{};
		std::uint16_t resultCode{};

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
			return NetworkLib::Packet::Serialization::GetSerializedSize(previousRoomId) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(currentRoomId) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(success) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(resultCode);
		}

		void Serialize(
			NetworkLib::Packet::Serialization::FPacketWriter& writer) const override
		{
			writer.Write(previousRoomId);
			writer.Write(currentRoomId);
			writer.Write(success);
			writer.Write(resultCode);
		}

		bool Deserialize(
			NetworkLib::Packet::Serialization::FPacketReader& reader) override
		{
			return reader.Read(previousRoomId) && reader.Read(currentRoomId) && reader.Read(success) && reader.Read(resultCode);
		}
	};

	class FChattingRq final : public NetworkLib::Packet::Serialization::IContentPacket
	{
	public:
		static constexpr std::uint16_t kOpcode = 3104;

		std::uint32_t roomId{};
		std::uint64_t clientMessageId{};
		std::uint64_t sentTick{};
		std::vector<std::uint8_t> payload{};

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
			return NetworkLib::Packet::Serialization::GetSerializedSize(roomId) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(clientMessageId) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(sentTick) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(payload);
		}

		void Serialize(
			NetworkLib::Packet::Serialization::FPacketWriter& writer) const override
		{
			writer.Write(roomId);
			writer.Write(clientMessageId);
			writer.Write(sentTick);
			writer.Write(payload);
		}

		bool Deserialize(
			NetworkLib::Packet::Serialization::FPacketReader& reader) override
		{
			return reader.Read(roomId) && reader.Read(clientMessageId) && reader.Read(sentTick) && reader.Read(payload);
		}
	};

	class FChattingRp final : public NetworkLib::Packet::Serialization::IContentPacket
	{
	public:
		static constexpr std::uint16_t kOpcode = 3105;

		bool success{};

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
			return NetworkLib::Packet::Serialization::GetSerializedSize(success);
		}

		void Serialize(
			NetworkLib::Packet::Serialization::FPacketWriter& writer) const override
		{
			writer.Write(success);
		}

		bool Deserialize(
			NetworkLib::Packet::Serialization::FPacketReader& reader) override
		{
			return reader.Read(success);
		}
	};

	class FBroadcast final : public NetworkLib::Packet::Serialization::IContentPacket
	{
	public:
		static constexpr std::uint16_t kOpcode = 3106;

		std::uint32_t roomId{};
		std::uint64_t senderUserId{};
		std::uint64_t messageId{};
		std::uint64_t sentTick{};
		std::vector<std::uint8_t> payload{};

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
			return NetworkLib::Packet::Serialization::GetSerializedSize(roomId) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(senderUserId) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(messageId) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(sentTick) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(payload);
		}

		void Serialize(
			NetworkLib::Packet::Serialization::FPacketWriter& writer) const override
		{
			writer.Write(roomId);
			writer.Write(senderUserId);
			writer.Write(messageId);
			writer.Write(sentTick);
			writer.Write(payload);
		}

		bool Deserialize(
			NetworkLib::Packet::Serialization::FPacketReader& reader) override
		{
			return reader.Read(roomId) && reader.Read(senderUserId) && reader.Read(messageId) && reader.Read(sentTick) &&
				   reader.Read(payload);
		}
	};

}
