#pragma once

namespace NetworkLib::Packet::Serialization
{
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
}
