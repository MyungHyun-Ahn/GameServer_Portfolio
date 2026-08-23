#pragma once

namespace Generated::Echo
{
	class FEchoRq final : public NetworkLib::Packet::Serialization::IContentPacket
	{
	public:
		static constexpr std::uint16_t kOpcode = 1000;

		void SetMessageValue(
			std::string_view value) noexcept
		{
			m_message = value;
		}

		std::string_view GetMessageValue() const noexcept
		{
			NetworkLib::Packet::View::ValidateBorrowedViewAccess(m_borrowedViewScope);
			return m_message;
		}

		void BindBorrowedViewScope(
			const std::shared_ptr<NetworkLib::Packet::View::FBorrowedViewScopeState>& scope) noexcept override
		{
			m_borrowedViewScope = scope;
		}

	public:
		std::uint16_t GetOpcode() const noexcept override
		{
			return kOpcode;
		}

		bool ContainsBorrowedViews() const noexcept override
		{
			return true;
		}

		std::size_t GetEstimatedBodySize() const noexcept override
		{
			return NetworkLib::Packet::Serialization::GetSerializedSize(m_message);
		}

		void Serialize(
			NetworkLib::Packet::Serialization::FPacketWriter& writer) const override
		{
			writer.Write(m_message);
		}

		bool Deserialize(
			NetworkLib::Packet::Serialization::FPacketReader& reader) override
		{
			return reader.Read(m_message);
		}

	private:
		std::shared_ptr<NetworkLib::Packet::View::FBorrowedViewScopeState> m_borrowedViewScope;
		std::string_view m_message{};
	};

	class FEchoRp final : public NetworkLib::Packet::Serialization::IContentPacket
	{
	public:
		static constexpr std::uint16_t kOpcode = 1001;

		void SetMessageValue(
			std::string_view value) noexcept
		{
			m_message = value;
		}

		std::string_view GetMessageValue() const noexcept
		{
			NetworkLib::Packet::View::ValidateBorrowedViewAccess(m_borrowedViewScope);
			return m_message;
		}

		void BindBorrowedViewScope(
			const std::shared_ptr<NetworkLib::Packet::View::FBorrowedViewScopeState>& scope) noexcept override
		{
			m_borrowedViewScope = scope;
		}

	public:
		std::uint16_t GetOpcode() const noexcept override
		{
			return kOpcode;
		}

		bool ContainsBorrowedViews() const noexcept override
		{
			return true;
		}

		std::size_t GetEstimatedBodySize() const noexcept override
		{
			return NetworkLib::Packet::Serialization::GetSerializedSize(m_message);
		}

		void Serialize(
			NetworkLib::Packet::Serialization::FPacketWriter& writer) const override
		{
			writer.Write(m_message);
		}

		bool Deserialize(
			NetworkLib::Packet::Serialization::FPacketReader& reader) override
		{
			return reader.Read(m_message);
		}

	private:
		std::shared_ptr<NetworkLib::Packet::View::FBorrowedViewScopeState> m_borrowedViewScope;
		std::string_view m_message{};
	};

	class FEchoNoti final : public NetworkLib::Packet::Serialization::IContentPacket
	{
	public:
		static constexpr std::uint16_t kOpcode = 1002;

		void SetMessageValue(
			std::string_view value) noexcept
		{
			m_message = value;
		}

		std::string_view GetMessageValue() const noexcept
		{
			NetworkLib::Packet::View::ValidateBorrowedViewAccess(m_borrowedViewScope);
			return m_message;
		}

		void BindBorrowedViewScope(
			const std::shared_ptr<NetworkLib::Packet::View::FBorrowedViewScopeState>& scope) noexcept override
		{
			m_borrowedViewScope = scope;
		}

	public:
		std::uint16_t GetOpcode() const noexcept override
		{
			return kOpcode;
		}

		bool ContainsBorrowedViews() const noexcept override
		{
			return true;
		}

		std::size_t GetEstimatedBodySize() const noexcept override
		{
			return NetworkLib::Packet::Serialization::GetSerializedSize(m_message);
		}

		void Serialize(
			NetworkLib::Packet::Serialization::FPacketWriter& writer) const override
		{
			writer.Write(m_message);
		}

		bool Deserialize(
			NetworkLib::Packet::Serialization::FPacketReader& reader) override
		{
			return reader.Read(m_message);
		}

	private:
		std::shared_ptr<NetworkLib::Packet::View::FBorrowedViewScopeState> m_borrowedViewScope;
		std::string_view m_message{};
	};

}
