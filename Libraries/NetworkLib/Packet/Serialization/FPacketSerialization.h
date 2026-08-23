#pragma once

namespace NetworkLib::Packet::Serialization
{
	class FOutgoingContentPacket
	{
	public:
		FOutgoingContentPacket() noexcept = default;

		explicit FOutgoingContentPacket(
			NetworkLib::Packet::Buffer::FPacketBuffer* packetBuffer,
			const std::size_t bodyOffset) noexcept
			: m_packetBuffer(packetBuffer)
			, m_bodyOffset(bodyOffset)
		{
		}

		~FOutgoingContentPacket() noexcept
		{
			Reset();
		}

		FOutgoingContentPacket(const FOutgoingContentPacket&) = delete;
		FOutgoingContentPacket& operator=(const FOutgoingContentPacket&) = delete;

		FOutgoingContentPacket(
			FOutgoingContentPacket&& other) noexcept
			: m_packetBuffer(std::exchange(other.m_packetBuffer, nullptr))
			, m_bodyOffset(std::exchange(other.m_bodyOffset, 0))
		{
		}

		FOutgoingContentPacket& operator=(
			FOutgoingContentPacket&& other) noexcept
		{
			if (this != &other)
			{
				Reset();
				m_packetBuffer = std::exchange(other.m_packetBuffer, nullptr);
				m_bodyOffset = std::exchange(other.m_bodyOffset, 0);
			}

			return *this;
		}

	public:
		bool IsValid() const noexcept
		{
			return m_packetBuffer != nullptr;
		}

		const char* GetPayloadData() const noexcept
		{
			if (m_packetBuffer == nullptr)
			{
				return nullptr;
			}

			const std::vector<char>& buffer = m_packetBuffer->GetBuffer();
			return buffer.empty() ? nullptr : buffer.data();
		}

		std::int32_t GetPayloadLength() const noexcept
		{
			if (m_packetBuffer == nullptr)
			{
				return 0;
			}

			return static_cast<std::int32_t>(m_packetBuffer->GetBuffer().size());
		}

		std::int32_t GetBodyLength() const noexcept
		{
			if (m_packetBuffer == nullptr)
			{
				return 0;
			}

			const std::vector<char>& buffer = m_packetBuffer->GetBuffer();
			return static_cast<std::int32_t>(buffer.size() > m_bodyOffset ? buffer.size() - m_bodyOffset : 0);
		}

		std::vector<char> MoveBuffer() noexcept
		{
			std::vector<char> buffer;
			if (m_packetBuffer != nullptr)
			{
				buffer = std::move(m_packetBuffer->GetBuffer());
				NetworkLib::Packet::Buffer::FPacketBuffer::Release(std::exchange(m_packetBuffer, nullptr));
				m_bodyOffset = 0;
			}

			return buffer;
		}

		NetworkLib::Packet::Buffer::FPacketBuffer* ReleaseBuffer() noexcept
		{
			m_bodyOffset = 0;
			return std::exchange(m_packetBuffer, nullptr);
		}

	private:
		void Reset() noexcept
		{
			if (m_packetBuffer != nullptr)
			{
				NetworkLib::Packet::Buffer::FPacketBuffer::Release(m_packetBuffer);
				m_packetBuffer = nullptr;
				m_bodyOffset = 0;
			}
		}

	private:
		NetworkLib::Packet::Buffer::FPacketBuffer* m_packetBuffer = nullptr;
		std::size_t m_bodyOffset = 0;
	};

	inline FOutgoingContentPacket BuildOutgoingContentPacket(
		std::uint16_t opcode,
		FPacketWriter&& writer)
	{
		const std::size_t bodyOffset = writer.GetFrontSize();
		NetworkLib::Packet::Buffer::FPacketBuffer* packetBuffer = writer.ReleaseBuffer();
		if (packetBuffer == nullptr)
		{
			return {};
		}

		if (bodyOffset < sizeof(NetworkLib::Packet::Framing::SContentHeader))
		{
			NetworkLib::Packet::Buffer::FPacketBuffer::Release(packetBuffer);
			return {};
		}

		NetworkLib::Packet::Framing::SContentHeader contentHeader{};
		contentHeader.opcode = opcode;
		std::memcpy(packetBuffer->GetBuffer().data(), &contentHeader, sizeof(NetworkLib::Packet::Framing::SContentHeader));

		return FOutgoingContentPacket(packetBuffer, bodyOffset);
	}

	template <typename TPacket>
	inline FOutgoingContentPacket BuildOutgoingContentPacket(
		const TPacket& packet)
	{
		FPacketWriter writer;
		writer.ReserveFront(sizeof(NetworkLib::Packet::Framing::SContentHeader));
		writer.ReserveAdditional(packet.GetEstimatedBodySize());
		packet.Serialize(writer);
		return BuildOutgoingContentPacket(packet.GetOpcode(), std::move(writer));
	}

	template <typename TValue>
	inline std::size_t GetSerializedSize(
		const TValue&) noexcept
		requires CPacketWritableScalar<TValue>
	{
		return sizeof(TValue);
	}

	inline std::size_t GetSerializedSize(
		const std::string& value) noexcept
	{
		return sizeof(std::uint32_t) + value.size();
	}

	inline std::size_t GetSerializedSize(
		const std::string_view value) noexcept
	{
		return sizeof(std::uint32_t) + value.size();
	}

	inline std::size_t GetSerializedSize(
		const std::span<const std::uint8_t> value) noexcept
	{
		return sizeof(std::uint32_t) + value.size_bytes();
	}

	template <typename TValue>
	inline std::size_t GetSerializedSize(
		const std::vector<TValue>& values) noexcept
	{
		std::size_t totalSize = sizeof(std::uint32_t);
		if constexpr (CPacketWritableScalar<TValue>)
		{
			return totalSize + sizeof(TValue) * values.size();
		}

		for (const TValue& value : values)
		{
			totalSize += GetSerializedSize(value);
		}

		return totalSize;
	}

	template <typename TValue, std::size_t N>
	inline std::size_t GetSerializedSize(
		const std::array<TValue, N>& values) noexcept
	{
		if constexpr (CPacketWritableScalar<TValue>)
		{
			return sizeof(TValue) * values.size();
		}

		std::size_t totalSize = 0;
		for (const TValue& value : values)
		{
			totalSize += GetSerializedSize(value);
		}

		return totalSize;
	}

	template <typename TKey, typename TValue, typename TCompare, typename TAllocator>
	inline std::size_t GetSerializedSize(
		const std::map<TKey, TValue, TCompare, TAllocator>& values) noexcept
	{
		std::size_t totalSize = sizeof(std::uint32_t);
		for (const auto& [key, value] : values)
		{
			totalSize += GetSerializedSize(key);
			totalSize += GetSerializedSize(value);
		}

		return totalSize;
	}

	template <typename TKey, typename TValue, typename THash, typename TKeyEqual, typename TAllocator>
	inline std::size_t GetSerializedSize(
		const std::unordered_map<TKey, TValue, THash, TKeyEqual, TAllocator>& values) noexcept
	{
		std::size_t totalSize = sizeof(std::uint32_t);
		for (const auto& [key, value] : values)
		{
			totalSize += GetSerializedSize(key);
			totalSize += GetSerializedSize(value);
		}

		return totalSize;
	}

	template <typename TPacket>
	inline std::vector<char> SerializeContentBody(
		const TPacket& packet)
	{
		FPacketWriter writer;
		writer.ReserveAdditional(packet.GetEstimatedBodySize());
		packet.Serialize(writer);
		return writer.MoveBuffer();
	}

	template <typename TPacket>
	inline bool DeserializeContentPacket(
		const char* payload,
		std::size_t payloadLength,
		TPacket& outPacket)
	{
		FPacketReader reader(payload, payloadLength);
		return outPacket.Deserialize(reader) && reader.IsAtEnd();
	}

	template <typename TPacket>
	inline bool DeserializeContentPacket(
		const NetworkLib::Packet::View::FPacketView& packetView,
		TPacket& outPacket)
	{
		if (packetView.opcode != TPacket::kOpcode)
		{
			return false;
		}

		return DeserializeContentPacket(packetView.payload, static_cast<std::size_t>(packetView.payloadLength), outPacket);
	}

	inline std::vector<char> BuildContentPayload(
		std::uint16_t opcode,
		std::vector<char>&& bodyBuffer)
	{
		NetworkLib::Packet::Framing::SContentHeader contentHeader{};
		contentHeader.opcode = opcode;

		std::vector<char> payloadBuffer;
		payloadBuffer.resize(sizeof(NetworkLib::Packet::Framing::SContentHeader) + bodyBuffer.size());
		std::memcpy(payloadBuffer.data(), &contentHeader, sizeof(NetworkLib::Packet::Framing::SContentHeader));
		if (!bodyBuffer.empty())
		{
			std::memcpy(payloadBuffer.data() + sizeof(NetworkLib::Packet::Framing::SContentHeader), bodyBuffer.data(), bodyBuffer.size());
		}

		return payloadBuffer;
	}

	template <typename TPacket>
	inline std::vector<char> SerializeContentPacket(
		const TPacket& packet)
	{
		return BuildOutgoingContentPacket(packet).MoveBuffer();
	}

	inline bool TryParseContentPacketView(
		const NetworkLib::Packet::View::FPacketView& transportPacketView,
		NetworkLib::Packet::View::FPacketView& outPacketView)
	{
		if (transportPacketView.payload == nullptr ||
			transportPacketView.payloadLength < static_cast<std::int32_t>(sizeof(NetworkLib::Packet::Framing::SContentHeader)))
		{
			return false;
		}

		NetworkLib::Packet::Framing::SContentHeader contentHeader{};
		std::memcpy(&contentHeader, transportPacketView.payload, sizeof(NetworkLib::Packet::Framing::SContentHeader));

		outPacketView = transportPacketView;
		outPacketView.opcode = contentHeader.opcode;
		outPacketView.payload = transportPacketView.payload + sizeof(NetworkLib::Packet::Framing::SContentHeader);
		outPacketView.payloadLength =
			transportPacketView.payloadLength - static_cast<std::int32_t>(sizeof(NetworkLib::Packet::Framing::SContentHeader));
		return true;
	}

	template <typename TPacket>
	inline bool SendContentPacket(
		NetworkLib::IServer& server,
		std::uint64_t sessionId,
		const TPacket& packet)
	{
		return server.SendPacket(sessionId, BuildOutgoingContentPacket(packet));
	}
}
