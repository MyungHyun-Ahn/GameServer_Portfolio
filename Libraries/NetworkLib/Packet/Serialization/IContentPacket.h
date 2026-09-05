#pragma once

namespace NetworkLib::Packet::Serialization
{
	enum class EContentPacketKind : std::uint8_t
	{
		Request = 0,
		Response = 1,
		Notification = 2,
		Broadcast = 3
	};

	class IContentPacket
	{
	public:
		virtual ~IContentPacket() = default;

		virtual std::uint16_t GetOpcode() const noexcept = 0;
		virtual bool ContainsBorrowedViews() const noexcept
		{
			return false;
		}

		virtual void BindBorrowedViewScope(
			const std::shared_ptr<NetworkLib::Packet::View::FBorrowedViewScopeState>&) noexcept
		{
		}

		virtual std::size_t GetEstimatedBodySize() const noexcept
		{
			return 0;
		}

		virtual void Serialize(FPacketWriter& writer) const = 0;
		virtual bool Deserialize(FPacketReader& reader) = 0;
	};

	class IResponsePacket : public IContentPacket
	{
	public:
		~IResponsePacket() override = default;
	};
}
