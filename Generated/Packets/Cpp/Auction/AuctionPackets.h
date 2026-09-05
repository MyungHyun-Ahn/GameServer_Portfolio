#pragma once

namespace Generated::Auction
{
	class FAuctionAuthRq final : public NetworkLib::Packet::Serialization::IContentPacket
	{
	public:
		static constexpr std::uint16_t kOpcode = 3998;
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

	class FAuctionAuthRp final : public NetworkLib::Packet::Serialization::IResponsePacket
	{
	public:
		static constexpr std::uint16_t kOpcode = 3999;
		static constexpr NetworkLib::Packet::Serialization::EContentPacketKind kPacketKind =
			NetworkLib::Packet::Serialization::EContentPacketKind::Response;

		std::uint16_t resultCode{};
		std::uint64_t requestId{};
		std::uint64_t userId{};
		std::uint32_t maxActiveListings{};
		std::uint32_t searchPageSize{};
		std::uint32_t inventoryListPageSize{};
		std::uint32_t mailListPageSize{};
		std::uint32_t minimumListingDurationSeconds{};
		std::uint32_t maximumListingDurationSeconds{};
		std::uint32_t defaultListingDurationSeconds{};
		std::uint16_t defaultCurrencyId{};
		std::uint64_t minimumBidIncrement{};
		std::uint64_t minimumListingPrice{};
		std::uint64_t maximumListingPrice{};

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
				   NetworkLib::Packet::Serialization::GetSerializedSize(userId) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(maxActiveListings) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(searchPageSize) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(inventoryListPageSize) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(mailListPageSize) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(minimumListingDurationSeconds) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(maximumListingDurationSeconds) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(defaultListingDurationSeconds) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(defaultCurrencyId) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(minimumBidIncrement) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(minimumListingPrice) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(maximumListingPrice);
		}

		void Serialize(
			NetworkLib::Packet::Serialization::FPacketWriter& writer) const override
		{
			writer.Write(resultCode);
			writer.Write(requestId);
			writer.Write(userId);
			writer.Write(maxActiveListings);
			writer.Write(searchPageSize);
			writer.Write(inventoryListPageSize);
			writer.Write(mailListPageSize);
			writer.Write(minimumListingDurationSeconds);
			writer.Write(maximumListingDurationSeconds);
			writer.Write(defaultListingDurationSeconds);
			writer.Write(defaultCurrencyId);
			writer.Write(minimumBidIncrement);
			writer.Write(minimumListingPrice);
			writer.Write(maximumListingPrice);
		}

		bool Deserialize(
			NetworkLib::Packet::Serialization::FPacketReader& reader) override
		{
			return reader.Read(resultCode) && reader.Read(requestId) && reader.Read(userId) && reader.Read(maxActiveListings) &&
				   reader.Read(searchPageSize) && reader.Read(inventoryListPageSize) && reader.Read(mailListPageSize) &&
				   reader.Read(minimumListingDurationSeconds) && reader.Read(maximumListingDurationSeconds) &&
				   reader.Read(defaultListingDurationSeconds) && reader.Read(defaultCurrencyId) && reader.Read(minimumBidIncrement) &&
				   reader.Read(minimumListingPrice) && reader.Read(maximumListingPrice);
		}
	};

	class FPingRq final : public NetworkLib::Packet::Serialization::IContentPacket
	{
	public:
		static constexpr std::uint16_t kOpcode = 4000;
		static constexpr NetworkLib::Packet::Serialization::EContentPacketKind kPacketKind =
			NetworkLib::Packet::Serialization::EContentPacketKind::Request;

		std::uint64_t requestId{};
		std::uint64_t routingKey{};
		std::uint64_t clientTimeUnixMs{};

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
				   NetworkLib::Packet::Serialization::GetSerializedSize(routingKey) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(clientTimeUnixMs);
		}

		void Serialize(
			NetworkLib::Packet::Serialization::FPacketWriter& writer) const override
		{
			writer.Write(requestId);
			writer.Write(routingKey);
			writer.Write(clientTimeUnixMs);
		}

		bool Deserialize(
			NetworkLib::Packet::Serialization::FPacketReader& reader) override
		{
			return reader.Read(requestId) && reader.Read(routingKey) && reader.Read(clientTimeUnixMs);
		}
	};

	class FPingRp final : public NetworkLib::Packet::Serialization::IResponsePacket
	{
	public:
		static constexpr std::uint16_t kOpcode = 4001;
		static constexpr NetworkLib::Packet::Serialization::EContentPacketKind kPacketKind =
			NetworkLib::Packet::Serialization::EContentPacketKind::Response;

		std::uint16_t resultCode{};
		std::uint64_t requestId{};
		std::uint64_t routingKey{};
		std::uint64_t clientTimeUnixMs{};
		std::uint64_t serverTimeUnixMs{};
		std::uint32_t shardIndex{};
		std::uint32_t shardCount{};
		std::uint64_t contentInstanceId{};
		std::uint32_t contentThreadId{};

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
				   NetworkLib::Packet::Serialization::GetSerializedSize(routingKey) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(clientTimeUnixMs) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(serverTimeUnixMs) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(shardIndex) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(shardCount) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(contentInstanceId) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(contentThreadId);
		}

		void Serialize(
			NetworkLib::Packet::Serialization::FPacketWriter& writer) const override
		{
			writer.Write(resultCode);
			writer.Write(requestId);
			writer.Write(routingKey);
			writer.Write(clientTimeUnixMs);
			writer.Write(serverTimeUnixMs);
			writer.Write(shardIndex);
			writer.Write(shardCount);
			writer.Write(contentInstanceId);
			writer.Write(contentThreadId);
		}

		bool Deserialize(
			NetworkLib::Packet::Serialization::FPacketReader& reader) override
		{
			return reader.Read(resultCode) && reader.Read(requestId) && reader.Read(routingKey) && reader.Read(clientTimeUnixMs) &&
				   reader.Read(serverTimeUnixMs) && reader.Read(shardIndex) && reader.Read(shardCount) && reader.Read(contentInstanceId) &&
				   reader.Read(contentThreadId);
		}
	};

	class FMyBidListRq final : public NetworkLib::Packet::Serialization::IContentPacket
	{
	public:
		static constexpr std::uint16_t kOpcode = 4010;
		static constexpr NetworkLib::Packet::Serialization::EContentPacketKind kPacketKind =
			NetworkLib::Packet::Serialization::EContentPacketKind::Request;

		std::uint64_t requestId{};
		std::uint64_t cursorBidId{};
		std::uint32_t limit{};

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
				   NetworkLib::Packet::Serialization::GetSerializedSize(cursorBidId) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(limit);
		}

		void Serialize(
			NetworkLib::Packet::Serialization::FPacketWriter& writer) const override
		{
			writer.Write(requestId);
			writer.Write(cursorBidId);
			writer.Write(limit);
		}

		bool Deserialize(
			NetworkLib::Packet::Serialization::FPacketReader& reader) override
		{
			return reader.Read(requestId) && reader.Read(cursorBidId) && reader.Read(limit);
		}
	};

	class FMyBidListRp final : public NetworkLib::Packet::Serialization::IResponsePacket
	{
	public:
		static constexpr std::uint16_t kOpcode = 4011;
		static constexpr NetworkLib::Packet::Serialization::EContentPacketKind kPacketKind =
			NetworkLib::Packet::Serialization::EContentPacketKind::Response;

		std::uint16_t resultCode{};
		std::uint64_t requestId{};
		std::vector<std::uint64_t> bidIds{};
		std::vector<std::uint64_t> listingIds{};
		std::vector<std::uint16_t> currencyIds{};
		std::vector<std::uint64_t> bidAmounts{};
		std::vector<std::uint8_t> bidStates{};
		std::vector<std::uint64_t> bidVersions{};
		std::vector<std::uint64_t> currentBidPrices{};
		std::vector<std::uint8_t> listingStates{};

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
				   NetworkLib::Packet::Serialization::GetSerializedSize(bidIds) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(listingIds) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(currencyIds) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(bidAmounts) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(bidStates) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(bidVersions) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(currentBidPrices) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(listingStates);
		}

		void Serialize(
			NetworkLib::Packet::Serialization::FPacketWriter& writer) const override
		{
			writer.Write(resultCode);
			writer.Write(requestId);
			writer.Write(bidIds);
			writer.Write(listingIds);
			writer.Write(currencyIds);
			writer.Write(bidAmounts);
			writer.Write(bidStates);
			writer.Write(bidVersions);
			writer.Write(currentBidPrices);
			writer.Write(listingStates);
		}

		bool Deserialize(
			NetworkLib::Packet::Serialization::FPacketReader& reader) override
		{
			return reader.Read(resultCode) && reader.Read(requestId) && reader.Read(bidIds) && reader.Read(listingIds) &&
				   reader.Read(currencyIds) && reader.Read(bidAmounts) && reader.Read(bidStates) && reader.Read(bidVersions) &&
				   reader.Read(currentBidPrices) && reader.Read(listingStates);
		}
	};

	class FInventoryListRq final : public NetworkLib::Packet::Serialization::IContentPacket
	{
	public:
		static constexpr std::uint16_t kOpcode = 4014;
		static constexpr NetworkLib::Packet::Serialization::EContentPacketKind kPacketKind =
			NetworkLib::Packet::Serialization::EContentPacketKind::Request;

		std::uint64_t requestId{};
		std::uint64_t cursorItemInstanceId{};
		std::uint32_t limit{};

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
				   NetworkLib::Packet::Serialization::GetSerializedSize(cursorItemInstanceId) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(limit);
		}

		void Serialize(
			NetworkLib::Packet::Serialization::FPacketWriter& writer) const override
		{
			writer.Write(requestId);
			writer.Write(cursorItemInstanceId);
			writer.Write(limit);
		}

		bool Deserialize(
			NetworkLib::Packet::Serialization::FPacketReader& reader) override
		{
			return reader.Read(requestId) && reader.Read(cursorItemInstanceId) && reader.Read(limit);
		}
	};

	class FInventoryListRp final : public NetworkLib::Packet::Serialization::IResponsePacket
	{
	public:
		static constexpr std::uint16_t kOpcode = 4015;
		static constexpr NetworkLib::Packet::Serialization::EContentPacketKind kPacketKind =
			NetworkLib::Packet::Serialization::EContentPacketKind::Response;

		std::uint16_t resultCode{};
		std::uint64_t requestId{};
		std::vector<std::uint64_t> itemInstanceIds{};
		std::vector<std::uint32_t> itemDataIds{};
		std::vector<std::uint32_t> quantities{};
		std::vector<std::uint8_t> equippedStates{};
		std::vector<std::uint8_t> tradableStates{};
		std::vector<std::string> itemData{};
		std::vector<std::uint64_t> versions{};

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
				   NetworkLib::Packet::Serialization::GetSerializedSize(itemInstanceIds) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(itemDataIds) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(quantities) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(equippedStates) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(tradableStates) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(itemData) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(versions);
		}

		void Serialize(
			NetworkLib::Packet::Serialization::FPacketWriter& writer) const override
		{
			writer.Write(resultCode);
			writer.Write(requestId);
			writer.Write(itemInstanceIds);
			writer.Write(itemDataIds);
			writer.Write(quantities);
			writer.Write(equippedStates);
			writer.Write(tradableStates);
			writer.Write(itemData);
			writer.Write(versions);
		}

		bool Deserialize(
			NetworkLib::Packet::Serialization::FPacketReader& reader) override
		{
			return reader.Read(resultCode) && reader.Read(requestId) && reader.Read(itemInstanceIds) && reader.Read(itemDataIds) &&
				   reader.Read(quantities) && reader.Read(equippedStates) && reader.Read(tradableStates) && reader.Read(itemData) &&
				   reader.Read(versions);
		}
	};

	class FListingRegisterRq final : public NetworkLib::Packet::Serialization::IContentPacket
	{
	public:
		static constexpr std::uint16_t kOpcode = 4016;
		static constexpr NetworkLib::Packet::Serialization::EContentPacketKind kPacketKind =
			NetworkLib::Packet::Serialization::EContentPacketKind::Request;

		std::uint64_t requestId{};
		std::uint64_t itemInstanceId{};
		std::uint64_t expectedItemVersion{};
		std::uint16_t currencyId{};
		std::uint64_t startPrice{};
		std::uint64_t buyoutPrice{};
		std::uint32_t durationSeconds{};

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
				   NetworkLib::Packet::Serialization::GetSerializedSize(expectedItemVersion) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(currencyId) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(startPrice) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(buyoutPrice) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(durationSeconds);
		}

		void Serialize(
			NetworkLib::Packet::Serialization::FPacketWriter& writer) const override
		{
			writer.Write(requestId);
			writer.Write(itemInstanceId);
			writer.Write(expectedItemVersion);
			writer.Write(currencyId);
			writer.Write(startPrice);
			writer.Write(buyoutPrice);
			writer.Write(durationSeconds);
		}

		bool Deserialize(
			NetworkLib::Packet::Serialization::FPacketReader& reader) override
		{
			return reader.Read(requestId) && reader.Read(itemInstanceId) && reader.Read(expectedItemVersion) && reader.Read(currencyId) &&
				   reader.Read(startPrice) && reader.Read(buyoutPrice) && reader.Read(durationSeconds);
		}
	};

	class FListingRegisterRp final : public NetworkLib::Packet::Serialization::IResponsePacket
	{
	public:
		static constexpr std::uint16_t kOpcode = 4017;
		static constexpr NetworkLib::Packet::Serialization::EContentPacketKind kPacketKind =
			NetworkLib::Packet::Serialization::EContentPacketKind::Response;

		std::uint16_t resultCode{};
		std::uint64_t requestId{};
		std::uint64_t listingId{};

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
				   NetworkLib::Packet::Serialization::GetSerializedSize(listingId);
		}

		void Serialize(
			NetworkLib::Packet::Serialization::FPacketWriter& writer) const override
		{
			writer.Write(resultCode);
			writer.Write(requestId);
			writer.Write(listingId);
		}

		bool Deserialize(
			NetworkLib::Packet::Serialization::FPacketReader& reader) override
		{
			return reader.Read(resultCode) && reader.Read(requestId) && reader.Read(listingId);
		}
	};

	class FListingSearchRq final : public NetworkLib::Packet::Serialization::IContentPacket
	{
	public:
		static constexpr std::uint16_t kOpcode = 4018;
		static constexpr NetworkLib::Packet::Serialization::EContentPacketKind kPacketKind =
			NetworkLib::Packet::Serialization::EContentPacketKind::Request;

		std::uint64_t requestId{};
		std::uint8_t itemCategory{};
		std::vector<std::uint32_t> itemDataIds{};
		std::uint32_t minStr{};
		std::uint32_t minDex{};
		std::uint32_t minInt{};
		std::uint32_t minLuk{};
		std::uint8_t sellerOnly{};
		std::uint8_t sortType{};
		std::uint64_t cursorSortValue{};
		std::uint64_t cursorListingId{};
		std::uint32_t limit{};

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
				   NetworkLib::Packet::Serialization::GetSerializedSize(itemCategory) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(itemDataIds) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(minStr) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(minDex) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(minInt) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(minLuk) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(sellerOnly) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(sortType) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(cursorSortValue) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(cursorListingId) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(limit);
		}

		void Serialize(
			NetworkLib::Packet::Serialization::FPacketWriter& writer) const override
		{
			writer.Write(requestId);
			writer.Write(itemCategory);
			writer.Write(itemDataIds);
			writer.Write(minStr);
			writer.Write(minDex);
			writer.Write(minInt);
			writer.Write(minLuk);
			writer.Write(sellerOnly);
			writer.Write(sortType);
			writer.Write(cursorSortValue);
			writer.Write(cursorListingId);
			writer.Write(limit);
		}

		bool Deserialize(
			NetworkLib::Packet::Serialization::FPacketReader& reader) override
		{
			return reader.Read(requestId) && reader.Read(itemCategory) && reader.Read(itemDataIds) && reader.Read(minStr) &&
				   reader.Read(minDex) && reader.Read(minInt) && reader.Read(minLuk) && reader.Read(sellerOnly) && reader.Read(sortType) &&
				   reader.Read(cursorSortValue) && reader.Read(cursorListingId) && reader.Read(limit);
		}
	};

	class FListingSearchRp final : public NetworkLib::Packet::Serialization::IResponsePacket
	{
	public:
		static constexpr std::uint16_t kOpcode = 4019;
		static constexpr NetworkLib::Packet::Serialization::EContentPacketKind kPacketKind =
			NetworkLib::Packet::Serialization::EContentPacketKind::Response;

		std::uint16_t resultCode{};
		std::uint64_t requestId{};
		std::vector<std::uint64_t> listingIds{};
		std::vector<std::uint64_t> sellerUserIds{};
		std::vector<std::string> sellerLoginIds{};
		std::vector<std::uint32_t> itemDataIds{};
		std::vector<std::uint8_t> itemCategories{};
		std::vector<std::uint32_t> quantities{};
		std::vector<std::string> names{};
		std::vector<std::uint32_t> strStats{};
		std::vector<std::uint32_t> dexStats{};
		std::vector<std::uint32_t> intStats{};
		std::vector<std::uint32_t> lukStats{};
		std::vector<std::uint16_t> currencyIds{};
		std::vector<std::uint64_t> startPrices{};
		std::vector<std::uint64_t> currentBidPrices{};
		std::vector<std::uint64_t> buyoutPrices{};
		std::vector<std::uint64_t> expiresAtUnixMs{};
		std::vector<std::uint64_t> versions{};

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
				   NetworkLib::Packet::Serialization::GetSerializedSize(listingIds) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(sellerUserIds) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(sellerLoginIds) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(itemDataIds) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(itemCategories) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(quantities) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(names) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(strStats) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(dexStats) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(intStats) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(lukStats) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(currencyIds) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(startPrices) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(currentBidPrices) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(buyoutPrices) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(expiresAtUnixMs) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(versions);
		}

		void Serialize(
			NetworkLib::Packet::Serialization::FPacketWriter& writer) const override
		{
			writer.Write(resultCode);
			writer.Write(requestId);
			writer.Write(listingIds);
			writer.Write(sellerUserIds);
			writer.Write(sellerLoginIds);
			writer.Write(itemDataIds);
			writer.Write(itemCategories);
			writer.Write(quantities);
			writer.Write(names);
			writer.Write(strStats);
			writer.Write(dexStats);
			writer.Write(intStats);
			writer.Write(lukStats);
			writer.Write(currencyIds);
			writer.Write(startPrices);
			writer.Write(currentBidPrices);
			writer.Write(buyoutPrices);
			writer.Write(expiresAtUnixMs);
			writer.Write(versions);
		}

		bool Deserialize(
			NetworkLib::Packet::Serialization::FPacketReader& reader) override
		{
			return reader.Read(resultCode) && reader.Read(requestId) && reader.Read(listingIds) && reader.Read(sellerUserIds) &&
				   reader.Read(sellerLoginIds) && reader.Read(itemDataIds) && reader.Read(itemCategories) && reader.Read(quantities) &&
				   reader.Read(names) && reader.Read(strStats) && reader.Read(dexStats) && reader.Read(intStats) && reader.Read(lukStats) &&
				   reader.Read(currencyIds) && reader.Read(startPrices) && reader.Read(currentBidPrices) && reader.Read(buyoutPrices) &&
				   reader.Read(expiresAtUnixMs) && reader.Read(versions);
		}
	};

	class FListingDetailRq final : public NetworkLib::Packet::Serialization::IContentPacket
	{
	public:
		static constexpr std::uint16_t kOpcode = 4020;
		static constexpr NetworkLib::Packet::Serialization::EContentPacketKind kPacketKind =
			NetworkLib::Packet::Serialization::EContentPacketKind::Request;

		std::uint64_t requestId{};
		std::uint64_t listingId{};

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
				   NetworkLib::Packet::Serialization::GetSerializedSize(listingId);
		}

		void Serialize(
			NetworkLib::Packet::Serialization::FPacketWriter& writer) const override
		{
			writer.Write(requestId);
			writer.Write(listingId);
		}

		bool Deserialize(
			NetworkLib::Packet::Serialization::FPacketReader& reader) override
		{
			return reader.Read(requestId) && reader.Read(listingId);
		}
	};

	class FListingDetailRp final : public NetworkLib::Packet::Serialization::IResponsePacket
	{
	public:
		static constexpr std::uint16_t kOpcode = 4021;
		static constexpr NetworkLib::Packet::Serialization::EContentPacketKind kPacketKind =
			NetworkLib::Packet::Serialization::EContentPacketKind::Response;

		std::uint16_t resultCode{};
		std::uint64_t requestId{};
		std::uint64_t listingId{};
		std::uint64_t sellerUserId{};
		std::string sellerLoginId{};
		std::uint64_t itemInstanceId{};
		std::uint32_t itemDataId{};
		std::uint8_t itemCategory{};
		std::uint32_t quantity{};
		std::string itemData{};
		std::string name{};
		std::uint32_t strStat{};
		std::uint32_t dexStat{};
		std::uint32_t intStat{};
		std::uint32_t lukStat{};
		std::uint16_t currencyId{};
		std::uint64_t startPrice{};
		std::uint64_t currentBidPrice{};
		std::uint64_t buyoutPrice{};
		std::uint64_t highestBidderUserId{};
		std::uint64_t expiresAtUnixMs{};
		std::uint64_t version{};

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
				   NetworkLib::Packet::Serialization::GetSerializedSize(listingId) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(sellerUserId) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(sellerLoginId) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(itemInstanceId) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(itemDataId) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(itemCategory) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(quantity) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(itemData) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(name) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(strStat) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(dexStat) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(intStat) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(lukStat) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(currencyId) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(startPrice) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(currentBidPrice) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(buyoutPrice) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(highestBidderUserId) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(expiresAtUnixMs) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(version);
		}

		void Serialize(
			NetworkLib::Packet::Serialization::FPacketWriter& writer) const override
		{
			writer.Write(resultCode);
			writer.Write(requestId);
			writer.Write(listingId);
			writer.Write(sellerUserId);
			writer.Write(sellerLoginId);
			writer.Write(itemInstanceId);
			writer.Write(itemDataId);
			writer.Write(itemCategory);
			writer.Write(quantity);
			writer.Write(itemData);
			writer.Write(name);
			writer.Write(strStat);
			writer.Write(dexStat);
			writer.Write(intStat);
			writer.Write(lukStat);
			writer.Write(currencyId);
			writer.Write(startPrice);
			writer.Write(currentBidPrice);
			writer.Write(buyoutPrice);
			writer.Write(highestBidderUserId);
			writer.Write(expiresAtUnixMs);
			writer.Write(version);
		}

		bool Deserialize(
			NetworkLib::Packet::Serialization::FPacketReader& reader) override
		{
			return reader.Read(resultCode) && reader.Read(requestId) && reader.Read(listingId) && reader.Read(sellerUserId) &&
				   reader.Read(sellerLoginId) && reader.Read(itemInstanceId) && reader.Read(itemDataId) && reader.Read(itemCategory) &&
				   reader.Read(quantity) && reader.Read(itemData) && reader.Read(name) && reader.Read(strStat) && reader.Read(dexStat) &&
				   reader.Read(intStat) && reader.Read(lukStat) && reader.Read(currencyId) && reader.Read(startPrice) &&
				   reader.Read(currentBidPrice) && reader.Read(buyoutPrice) && reader.Read(highestBidderUserId) &&
				   reader.Read(expiresAtUnixMs) && reader.Read(version);
		}
	};

	class FBidRq final : public NetworkLib::Packet::Serialization::IContentPacket
	{
	public:
		static constexpr std::uint16_t kOpcode = 4022;
		static constexpr NetworkLib::Packet::Serialization::EContentPacketKind kPacketKind =
			NetworkLib::Packet::Serialization::EContentPacketKind::Request;

		std::uint64_t requestId{};
		std::uint64_t listingId{};
		std::uint64_t bidAmount{};
		std::uint64_t expectedListingVersion{};

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
				   NetworkLib::Packet::Serialization::GetSerializedSize(listingId) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(bidAmount) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(expectedListingVersion);
		}

		void Serialize(
			NetworkLib::Packet::Serialization::FPacketWriter& writer) const override
		{
			writer.Write(requestId);
			writer.Write(listingId);
			writer.Write(bidAmount);
			writer.Write(expectedListingVersion);
		}

		bool Deserialize(
			NetworkLib::Packet::Serialization::FPacketReader& reader) override
		{
			return reader.Read(requestId) && reader.Read(listingId) && reader.Read(bidAmount) && reader.Read(expectedListingVersion);
		}
	};

	class FBidRp final : public NetworkLib::Packet::Serialization::IResponsePacket
	{
	public:
		static constexpr std::uint16_t kOpcode = 4023;
		static constexpr NetworkLib::Packet::Serialization::EContentPacketKind kPacketKind =
			NetworkLib::Packet::Serialization::EContentPacketKind::Response;

		std::uint16_t resultCode{};
		std::uint64_t requestId{};
		std::uint64_t listingId{};
		std::uint64_t bidId{};
		std::uint64_t bidAmount{};
		std::uint64_t additionalDebit{};
		std::uint64_t currencyBalance{};
		std::uint64_t listingVersion{};

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
				   NetworkLib::Packet::Serialization::GetSerializedSize(listingId) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(bidId) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(bidAmount) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(additionalDebit) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(currencyBalance) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(listingVersion);
		}

		void Serialize(
			NetworkLib::Packet::Serialization::FPacketWriter& writer) const override
		{
			writer.Write(resultCode);
			writer.Write(requestId);
			writer.Write(listingId);
			writer.Write(bidId);
			writer.Write(bidAmount);
			writer.Write(additionalDebit);
			writer.Write(currencyBalance);
			writer.Write(listingVersion);
		}

		bool Deserialize(
			NetworkLib::Packet::Serialization::FPacketReader& reader) override
		{
			return reader.Read(resultCode) && reader.Read(requestId) && reader.Read(listingId) && reader.Read(bidId) &&
				   reader.Read(bidAmount) && reader.Read(additionalDebit) && reader.Read(currencyBalance) && reader.Read(listingVersion);
		}
	};

	class FAuctionOutbidNoti final : public NetworkLib::Packet::Serialization::IContentPacket
	{
	public:
		static constexpr std::uint16_t kOpcode = 4024;
		static constexpr NetworkLib::Packet::Serialization::EContentPacketKind kPacketKind =
			NetworkLib::Packet::Serialization::EContentPacketKind::Notification;

		std::uint64_t listingId{};
		std::uint64_t bidId{};
		std::uint64_t heldAmount{};
		std::uint64_t newHighestAmount{};

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
			return NetworkLib::Packet::Serialization::GetSerializedSize(listingId) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(bidId) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(heldAmount) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(newHighestAmount);
		}

		void Serialize(
			NetworkLib::Packet::Serialization::FPacketWriter& writer) const override
		{
			writer.Write(listingId);
			writer.Write(bidId);
			writer.Write(heldAmount);
			writer.Write(newHighestAmount);
		}

		bool Deserialize(
			NetworkLib::Packet::Serialization::FPacketReader& reader) override
		{
			return reader.Read(listingId) && reader.Read(bidId) && reader.Read(heldAmount) && reader.Read(newHighestAmount);
		}
	};

	class FBuyoutRq final : public NetworkLib::Packet::Serialization::IContentPacket
	{
	public:
		static constexpr std::uint16_t kOpcode = 4025;
		static constexpr NetworkLib::Packet::Serialization::EContentPacketKind kPacketKind =
			NetworkLib::Packet::Serialization::EContentPacketKind::Request;

		std::uint64_t requestId{};
		std::uint64_t listingId{};
		std::uint64_t expectedListingVersion{};

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
				   NetworkLib::Packet::Serialization::GetSerializedSize(listingId) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(expectedListingVersion);
		}

		void Serialize(
			NetworkLib::Packet::Serialization::FPacketWriter& writer) const override
		{
			writer.Write(requestId);
			writer.Write(listingId);
			writer.Write(expectedListingVersion);
		}

		bool Deserialize(
			NetworkLib::Packet::Serialization::FPacketReader& reader) override
		{
			return reader.Read(requestId) && reader.Read(listingId) && reader.Read(expectedListingVersion);
		}
	};

	class FBuyoutRp final : public NetworkLib::Packet::Serialization::IResponsePacket
	{
	public:
		static constexpr std::uint16_t kOpcode = 4026;
		static constexpr NetworkLib::Packet::Serialization::EContentPacketKind kPacketKind =
			NetworkLib::Packet::Serialization::EContentPacketKind::Response;

		std::uint16_t resultCode{};
		std::uint64_t requestId{};
		std::uint64_t listingId{};
		std::uint64_t buyoutPrice{};
		std::uint64_t additionalDebit{};
		std::uint64_t currencyBalance{};
		std::uint64_t itemMailId{};
		std::uint64_t sellerMailId{};
		std::uint64_t listingVersion{};

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
				   NetworkLib::Packet::Serialization::GetSerializedSize(listingId) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(buyoutPrice) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(additionalDebit) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(currencyBalance) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(itemMailId) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(sellerMailId) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(listingVersion);
		}

		void Serialize(
			NetworkLib::Packet::Serialization::FPacketWriter& writer) const override
		{
			writer.Write(resultCode);
			writer.Write(requestId);
			writer.Write(listingId);
			writer.Write(buyoutPrice);
			writer.Write(additionalDebit);
			writer.Write(currencyBalance);
			writer.Write(itemMailId);
			writer.Write(sellerMailId);
			writer.Write(listingVersion);
		}

		bool Deserialize(
			NetworkLib::Packet::Serialization::FPacketReader& reader) override
		{
			return reader.Read(resultCode) && reader.Read(requestId) && reader.Read(listingId) && reader.Read(buyoutPrice) &&
				   reader.Read(additionalDebit) && reader.Read(currencyBalance) && reader.Read(itemMailId) && reader.Read(sellerMailId) &&
				   reader.Read(listingVersion);
		}
	};

	class FMailListRq final : public NetworkLib::Packet::Serialization::IContentPacket
	{
	public:
		static constexpr std::uint16_t kOpcode = 4027;
		static constexpr NetworkLib::Packet::Serialization::EContentPacketKind kPacketKind =
			NetworkLib::Packet::Serialization::EContentPacketKind::Request;

		std::uint64_t requestId{};
		std::uint64_t cursorMailId{};
		std::uint32_t limit{};

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
				   NetworkLib::Packet::Serialization::GetSerializedSize(cursorMailId) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(limit);
		}

		void Serialize(
			NetworkLib::Packet::Serialization::FPacketWriter& writer) const override
		{
			writer.Write(requestId);
			writer.Write(cursorMailId);
			writer.Write(limit);
		}

		bool Deserialize(
			NetworkLib::Packet::Serialization::FPacketReader& reader) override
		{
			return reader.Read(requestId) && reader.Read(cursorMailId) && reader.Read(limit);
		}
	};

	class FMailListRp final : public NetworkLib::Packet::Serialization::IResponsePacket
	{
	public:
		static constexpr std::uint16_t kOpcode = 4028;
		static constexpr NetworkLib::Packet::Serialization::EContentPacketKind kPacketKind =
			NetworkLib::Packet::Serialization::EContentPacketKind::Response;

		std::uint16_t resultCode{};
		std::uint64_t requestId{};
		std::vector<std::uint64_t> mailIds{};
		std::vector<std::uint8_t> mailTypes{};
		std::vector<std::string> subjects{};
		std::vector<std::uint8_t> states{};
		std::vector<std::uint64_t> expiresAtUnixMs{};
		std::vector<std::uint64_t> createdAtUnixMs{};

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
				   NetworkLib::Packet::Serialization::GetSerializedSize(mailIds) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(mailTypes) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(subjects) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(states) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(expiresAtUnixMs) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(createdAtUnixMs);
		}

		void Serialize(
			NetworkLib::Packet::Serialization::FPacketWriter& writer) const override
		{
			writer.Write(resultCode);
			writer.Write(requestId);
			writer.Write(mailIds);
			writer.Write(mailTypes);
			writer.Write(subjects);
			writer.Write(states);
			writer.Write(expiresAtUnixMs);
			writer.Write(createdAtUnixMs);
		}

		bool Deserialize(
			NetworkLib::Packet::Serialization::FPacketReader& reader) override
		{
			return reader.Read(resultCode) && reader.Read(requestId) && reader.Read(mailIds) && reader.Read(mailTypes) &&
				   reader.Read(subjects) && reader.Read(states) && reader.Read(expiresAtUnixMs) && reader.Read(createdAtUnixMs);
		}
	};

	class FMailDetailRq final : public NetworkLib::Packet::Serialization::IContentPacket
	{
	public:
		static constexpr std::uint16_t kOpcode = 4029;
		static constexpr NetworkLib::Packet::Serialization::EContentPacketKind kPacketKind =
			NetworkLib::Packet::Serialization::EContentPacketKind::Request;

		std::uint64_t requestId{};
		std::uint64_t mailId{};

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
				   NetworkLib::Packet::Serialization::GetSerializedSize(mailId);
		}

		void Serialize(
			NetworkLib::Packet::Serialization::FPacketWriter& writer) const override
		{
			writer.Write(requestId);
			writer.Write(mailId);
		}

		bool Deserialize(
			NetworkLib::Packet::Serialization::FPacketReader& reader) override
		{
			return reader.Read(requestId) && reader.Read(mailId);
		}
	};

	class FMailDetailRp final : public NetworkLib::Packet::Serialization::IResponsePacket
	{
	public:
		static constexpr std::uint16_t kOpcode = 4030;
		static constexpr NetworkLib::Packet::Serialization::EContentPacketKind kPacketKind =
			NetworkLib::Packet::Serialization::EContentPacketKind::Response;

		std::uint16_t resultCode{};
		std::uint64_t requestId{};
		std::uint64_t mailId{};
		std::uint8_t mailType{};
		std::string subject{};
		std::string body{};
		std::uint8_t state{};
		std::uint64_t expiresAtUnixMs{};
		std::vector<std::uint64_t> attachmentIds{};
		std::vector<std::uint8_t> attachmentTypes{};
		std::vector<std::uint64_t> itemInstanceIds{};
		std::vector<std::uint32_t> itemDataIds{};
		std::vector<std::uint32_t> quantities{};
		std::vector<std::string> itemData{};
		std::vector<std::uint16_t> currencyIds{};
		std::vector<std::uint64_t> currencyAmounts{};
		std::vector<std::uint8_t> attachmentStates{};

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
				   NetworkLib::Packet::Serialization::GetSerializedSize(mailId) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(mailType) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(subject) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(body) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(state) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(expiresAtUnixMs) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(attachmentIds) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(attachmentTypes) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(itemInstanceIds) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(itemDataIds) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(quantities) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(itemData) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(currencyIds) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(currencyAmounts) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(attachmentStates);
		}

		void Serialize(
			NetworkLib::Packet::Serialization::FPacketWriter& writer) const override
		{
			writer.Write(resultCode);
			writer.Write(requestId);
			writer.Write(mailId);
			writer.Write(mailType);
			writer.Write(subject);
			writer.Write(body);
			writer.Write(state);
			writer.Write(expiresAtUnixMs);
			writer.Write(attachmentIds);
			writer.Write(attachmentTypes);
			writer.Write(itemInstanceIds);
			writer.Write(itemDataIds);
			writer.Write(quantities);
			writer.Write(itemData);
			writer.Write(currencyIds);
			writer.Write(currencyAmounts);
			writer.Write(attachmentStates);
		}

		bool Deserialize(
			NetworkLib::Packet::Serialization::FPacketReader& reader) override
		{
			return reader.Read(resultCode) && reader.Read(requestId) && reader.Read(mailId) && reader.Read(mailType) &&
				   reader.Read(subject) && reader.Read(body) && reader.Read(state) && reader.Read(expiresAtUnixMs) &&
				   reader.Read(attachmentIds) && reader.Read(attachmentTypes) && reader.Read(itemInstanceIds) && reader.Read(itemDataIds) &&
				   reader.Read(quantities) && reader.Read(itemData) && reader.Read(currencyIds) && reader.Read(currencyAmounts) &&
				   reader.Read(attachmentStates);
		}
	};

	class FMailClaimRq final : public NetworkLib::Packet::Serialization::IContentPacket
	{
	public:
		static constexpr std::uint16_t kOpcode = 4031;
		static constexpr NetworkLib::Packet::Serialization::EContentPacketKind kPacketKind =
			NetworkLib::Packet::Serialization::EContentPacketKind::Request;

		std::uint64_t requestId{};
		std::uint64_t mailId{};
		std::uint64_t attachmentId{};

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
				   NetworkLib::Packet::Serialization::GetSerializedSize(mailId) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(attachmentId);
		}

		void Serialize(
			NetworkLib::Packet::Serialization::FPacketWriter& writer) const override
		{
			writer.Write(requestId);
			writer.Write(mailId);
			writer.Write(attachmentId);
		}

		bool Deserialize(
			NetworkLib::Packet::Serialization::FPacketReader& reader) override
		{
			return reader.Read(requestId) && reader.Read(mailId) && reader.Read(attachmentId);
		}
	};

	class FMailClaimRp final : public NetworkLib::Packet::Serialization::IResponsePacket
	{
	public:
		static constexpr std::uint16_t kOpcode = 4032;
		static constexpr NetworkLib::Packet::Serialization::EContentPacketKind kPacketKind =
			NetworkLib::Packet::Serialization::EContentPacketKind::Response;

		std::uint16_t resultCode{};
		std::uint64_t requestId{};
		std::uint64_t mailId{};
		std::uint64_t attachmentId{};
		std::uint8_t attachmentType{};
		std::uint64_t itemInstanceId{};
		std::uint32_t itemDataId{};
		std::uint32_t quantity{};
		std::uint16_t currencyId{};
		std::uint64_t currencyAmount{};
		std::uint64_t currencyBalance{};
		std::uint8_t mailState{};

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
				   NetworkLib::Packet::Serialization::GetSerializedSize(mailId) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(attachmentId) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(attachmentType) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(itemInstanceId) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(itemDataId) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(quantity) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(currencyId) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(currencyAmount) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(currencyBalance) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(mailState);
		}

		void Serialize(
			NetworkLib::Packet::Serialization::FPacketWriter& writer) const override
		{
			writer.Write(resultCode);
			writer.Write(requestId);
			writer.Write(mailId);
			writer.Write(attachmentId);
			writer.Write(attachmentType);
			writer.Write(itemInstanceId);
			writer.Write(itemDataId);
			writer.Write(quantity);
			writer.Write(currencyId);
			writer.Write(currencyAmount);
			writer.Write(currencyBalance);
			writer.Write(mailState);
		}

		bool Deserialize(
			NetworkLib::Packet::Serialization::FPacketReader& reader) override
		{
			return reader.Read(resultCode) && reader.Read(requestId) && reader.Read(mailId) && reader.Read(attachmentId) &&
				   reader.Read(attachmentType) && reader.Read(itemInstanceId) && reader.Read(itemDataId) && reader.Read(quantity) &&
				   reader.Read(currencyId) && reader.Read(currencyAmount) && reader.Read(currencyBalance) && reader.Read(mailState);
		}
	};

	class FListingCancelRq final : public NetworkLib::Packet::Serialization::IContentPacket
	{
	public:
		static constexpr std::uint16_t kOpcode = 4033;
		static constexpr NetworkLib::Packet::Serialization::EContentPacketKind kPacketKind =
			NetworkLib::Packet::Serialization::EContentPacketKind::Request;

		std::uint64_t requestId{};
		std::uint64_t listingId{};
		std::uint64_t expectedListingVersion{};

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
				   NetworkLib::Packet::Serialization::GetSerializedSize(listingId) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(expectedListingVersion);
		}

		void Serialize(
			NetworkLib::Packet::Serialization::FPacketWriter& writer) const override
		{
			writer.Write(requestId);
			writer.Write(listingId);
			writer.Write(expectedListingVersion);
		}

		bool Deserialize(
			NetworkLib::Packet::Serialization::FPacketReader& reader) override
		{
			return reader.Read(requestId) && reader.Read(listingId) && reader.Read(expectedListingVersion);
		}
	};

	class FListingCancelRp final : public NetworkLib::Packet::Serialization::IResponsePacket
	{
	public:
		static constexpr std::uint16_t kOpcode = 4034;
		static constexpr NetworkLib::Packet::Serialization::EContentPacketKind kPacketKind =
			NetworkLib::Packet::Serialization::EContentPacketKind::Response;

		std::uint16_t resultCode{};
		std::uint64_t requestId{};
		std::uint64_t listingId{};
		std::uint64_t returnMailId{};
		std::uint64_t listingVersion{};

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
				   NetworkLib::Packet::Serialization::GetSerializedSize(listingId) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(returnMailId) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(listingVersion);
		}

		void Serialize(
			NetworkLib::Packet::Serialization::FPacketWriter& writer) const override
		{
			writer.Write(resultCode);
			writer.Write(requestId);
			writer.Write(listingId);
			writer.Write(returnMailId);
			writer.Write(listingVersion);
		}

		bool Deserialize(
			NetworkLib::Packet::Serialization::FPacketReader& reader) override
		{
			return reader.Read(resultCode) && reader.Read(requestId) && reader.Read(listingId) && reader.Read(returnMailId) &&
				   reader.Read(listingVersion);
		}
	};

	class FAuctionWonNoti final : public NetworkLib::Packet::Serialization::IContentPacket
	{
	public:
		static constexpr std::uint16_t kOpcode = 4035;
		static constexpr NetworkLib::Packet::Serialization::EContentPacketKind kPacketKind =
			NetworkLib::Packet::Serialization::EContentPacketKind::Notification;

		std::uint64_t listingId{};
		std::uint64_t bidId{};
		std::uint64_t finalPrice{};
		std::uint64_t itemMailId{};

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
			return NetworkLib::Packet::Serialization::GetSerializedSize(listingId) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(bidId) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(finalPrice) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(itemMailId);
		}

		void Serialize(
			NetworkLib::Packet::Serialization::FPacketWriter& writer) const override
		{
			writer.Write(listingId);
			writer.Write(bidId);
			writer.Write(finalPrice);
			writer.Write(itemMailId);
		}

		bool Deserialize(
			NetworkLib::Packet::Serialization::FPacketReader& reader) override
		{
			return reader.Read(listingId) && reader.Read(bidId) && reader.Read(finalPrice) && reader.Read(itemMailId);
		}
	};

	class FDebugCheatRq final : public NetworkLib::Packet::Serialization::IContentPacket
	{
	public:
		static constexpr std::uint16_t kOpcode = 4036;
		static constexpr NetworkLib::Packet::Serialization::EContentPacketKind kPacketKind =
			NetworkLib::Packet::Serialization::EContentPacketKind::Request;

		std::uint64_t requestId{};
		std::uint8_t cheatType{};
		std::uint64_t amount{};
		std::uint32_t itemDataId{};
		std::uint32_t strStat{};
		std::uint32_t dexStat{};
		std::uint32_t intStat{};
		std::uint32_t lukStat{};

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
				   NetworkLib::Packet::Serialization::GetSerializedSize(cheatType) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(amount) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(itemDataId) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(strStat) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(dexStat) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(intStat) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(lukStat);
		}

		void Serialize(
			NetworkLib::Packet::Serialization::FPacketWriter& writer) const override
		{
			writer.Write(requestId);
			writer.Write(cheatType);
			writer.Write(amount);
			writer.Write(itemDataId);
			writer.Write(strStat);
			writer.Write(dexStat);
			writer.Write(intStat);
			writer.Write(lukStat);
		}

		bool Deserialize(
			NetworkLib::Packet::Serialization::FPacketReader& reader) override
		{
			return reader.Read(requestId) && reader.Read(cheatType) && reader.Read(amount) && reader.Read(itemDataId) &&
				   reader.Read(strStat) && reader.Read(dexStat) && reader.Read(intStat) && reader.Read(lukStat);
		}
	};

	class FDebugCheatRp final : public NetworkLib::Packet::Serialization::IResponsePacket
	{
	public:
		static constexpr std::uint16_t kOpcode = 4037;
		static constexpr NetworkLib::Packet::Serialization::EContentPacketKind kPacketKind =
			NetworkLib::Packet::Serialization::EContentPacketKind::Response;

		std::uint16_t resultCode{};
		std::uint64_t requestId{};
		std::uint8_t cheatType{};
		std::uint64_t currencyBalance{};
		std::uint64_t itemInstanceId{};
		std::string message{};

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
				   NetworkLib::Packet::Serialization::GetSerializedSize(cheatType) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(currencyBalance) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(itemInstanceId) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(message);
		}

		void Serialize(
			NetworkLib::Packet::Serialization::FPacketWriter& writer) const override
		{
			writer.Write(resultCode);
			writer.Write(requestId);
			writer.Write(cheatType);
			writer.Write(currencyBalance);
			writer.Write(itemInstanceId);
			writer.Write(message);
		}

		bool Deserialize(
			NetworkLib::Packet::Serialization::FPacketReader& reader) override
		{
			return reader.Read(resultCode) && reader.Read(requestId) && reader.Read(cheatType) && reader.Read(currencyBalance) &&
				   reader.Read(itemInstanceId) && reader.Read(message);
		}
	};

	class FSaleHistorySearchRq final : public NetworkLib::Packet::Serialization::IContentPacket
	{
	public:
		static constexpr std::uint16_t kOpcode = 4038;
		static constexpr NetworkLib::Packet::Serialization::EContentPacketKind kPacketKind =
			NetworkLib::Packet::Serialization::EContentPacketKind::Request;

		std::uint64_t requestId{};
		std::uint8_t itemCategory{};
		std::vector<std::uint32_t> itemDataIds{};
		std::uint32_t minStr{};
		std::uint32_t minDex{};
		std::uint32_t minInt{};
		std::uint32_t minLuk{};
		std::uint8_t sortType{};
		std::uint64_t cursorSortValue{};
		std::uint64_t cursorListingId{};
		std::uint32_t limit{};

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
				   NetworkLib::Packet::Serialization::GetSerializedSize(itemCategory) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(itemDataIds) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(minStr) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(minDex) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(minInt) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(minLuk) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(sortType) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(cursorSortValue) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(cursorListingId) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(limit);
		}

		void Serialize(
			NetworkLib::Packet::Serialization::FPacketWriter& writer) const override
		{
			writer.Write(requestId);
			writer.Write(itemCategory);
			writer.Write(itemDataIds);
			writer.Write(minStr);
			writer.Write(minDex);
			writer.Write(minInt);
			writer.Write(minLuk);
			writer.Write(sortType);
			writer.Write(cursorSortValue);
			writer.Write(cursorListingId);
			writer.Write(limit);
		}

		bool Deserialize(
			NetworkLib::Packet::Serialization::FPacketReader& reader) override
		{
			return reader.Read(requestId) && reader.Read(itemCategory) && reader.Read(itemDataIds) && reader.Read(minStr) &&
				   reader.Read(minDex) && reader.Read(minInt) && reader.Read(minLuk) && reader.Read(sortType) &&
				   reader.Read(cursorSortValue) && reader.Read(cursorListingId) && reader.Read(limit);
		}
	};

	class FSaleHistorySearchRp final : public NetworkLib::Packet::Serialization::IResponsePacket
	{
	public:
		static constexpr std::uint16_t kOpcode = 4039;
		static constexpr NetworkLib::Packet::Serialization::EContentPacketKind kPacketKind =
			NetworkLib::Packet::Serialization::EContentPacketKind::Response;

		std::uint16_t resultCode{};
		std::uint64_t requestId{};
		std::vector<std::uint64_t> listingIds{};
		std::vector<std::uint32_t> itemDataIds{};
		std::vector<std::uint8_t> itemCategories{};
		std::vector<std::uint32_t> quantities{};
		std::vector<std::string> names{};
		std::vector<std::uint32_t> strStats{};
		std::vector<std::uint32_t> dexStats{};
		std::vector<std::uint32_t> intStats{};
		std::vector<std::uint32_t> lukStats{};
		std::vector<std::uint16_t> currencyIds{};
		std::vector<std::uint64_t> finalPrices{};
		std::vector<std::uint8_t> saleTypes{};
		std::vector<std::uint64_t> soldAtUnixMs{};

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
				   NetworkLib::Packet::Serialization::GetSerializedSize(listingIds) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(itemDataIds) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(itemCategories) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(quantities) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(names) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(strStats) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(dexStats) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(intStats) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(lukStats) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(currencyIds) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(finalPrices) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(saleTypes) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(soldAtUnixMs);
		}

		void Serialize(
			NetworkLib::Packet::Serialization::FPacketWriter& writer) const override
		{
			writer.Write(resultCode);
			writer.Write(requestId);
			writer.Write(listingIds);
			writer.Write(itemDataIds);
			writer.Write(itemCategories);
			writer.Write(quantities);
			writer.Write(names);
			writer.Write(strStats);
			writer.Write(dexStats);
			writer.Write(intStats);
			writer.Write(lukStats);
			writer.Write(currencyIds);
			writer.Write(finalPrices);
			writer.Write(saleTypes);
			writer.Write(soldAtUnixMs);
		}

		bool Deserialize(
			NetworkLib::Packet::Serialization::FPacketReader& reader) override
		{
			return reader.Read(resultCode) && reader.Read(requestId) && reader.Read(listingIds) && reader.Read(itemDataIds) &&
				   reader.Read(itemCategories) && reader.Read(quantities) && reader.Read(names) && reader.Read(strStats) &&
				   reader.Read(dexStats) && reader.Read(intStats) && reader.Read(lukStats) && reader.Read(currencyIds) &&
				   reader.Read(finalPrices) && reader.Read(saleTypes) && reader.Read(soldAtUnixMs);
		}
	};

	class FSaleHistoryDetailRq final : public NetworkLib::Packet::Serialization::IContentPacket
	{
	public:
		static constexpr std::uint16_t kOpcode = 4040;
		static constexpr NetworkLib::Packet::Serialization::EContentPacketKind kPacketKind =
			NetworkLib::Packet::Serialization::EContentPacketKind::Request;

		std::uint64_t requestId{};
		std::uint64_t listingId{};

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
				   NetworkLib::Packet::Serialization::GetSerializedSize(listingId);
		}

		void Serialize(
			NetworkLib::Packet::Serialization::FPacketWriter& writer) const override
		{
			writer.Write(requestId);
			writer.Write(listingId);
		}

		bool Deserialize(
			NetworkLib::Packet::Serialization::FPacketReader& reader) override
		{
			return reader.Read(requestId) && reader.Read(listingId);
		}
	};

	class FSaleHistoryDetailRp final : public NetworkLib::Packet::Serialization::IResponsePacket
	{
	public:
		static constexpr std::uint16_t kOpcode = 4041;
		static constexpr NetworkLib::Packet::Serialization::EContentPacketKind kPacketKind =
			NetworkLib::Packet::Serialization::EContentPacketKind::Response;

		std::uint16_t resultCode{};
		std::uint64_t requestId{};
		std::uint64_t listingId{};
		std::string sellerLoginId{};
		std::uint32_t itemDataId{};
		std::uint8_t itemCategory{};
		std::uint32_t quantity{};
		std::string itemData{};
		std::string name{};
		std::uint32_t strStat{};
		std::uint32_t dexStat{};
		std::uint32_t intStat{};
		std::uint32_t lukStat{};
		std::uint16_t currencyId{};
		std::uint64_t startPrice{};
		std::uint64_t finalPrice{};
		std::uint8_t saleType{};
		std::uint64_t soldAtUnixMs{};

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
				   NetworkLib::Packet::Serialization::GetSerializedSize(listingId) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(sellerLoginId) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(itemDataId) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(itemCategory) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(quantity) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(itemData) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(name) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(strStat) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(dexStat) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(intStat) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(lukStat) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(currencyId) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(startPrice) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(finalPrice) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(saleType) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(soldAtUnixMs);
		}

		void Serialize(
			NetworkLib::Packet::Serialization::FPacketWriter& writer) const override
		{
			writer.Write(resultCode);
			writer.Write(requestId);
			writer.Write(listingId);
			writer.Write(sellerLoginId);
			writer.Write(itemDataId);
			writer.Write(itemCategory);
			writer.Write(quantity);
			writer.Write(itemData);
			writer.Write(name);
			writer.Write(strStat);
			writer.Write(dexStat);
			writer.Write(intStat);
			writer.Write(lukStat);
			writer.Write(currencyId);
			writer.Write(startPrice);
			writer.Write(finalPrice);
			writer.Write(saleType);
			writer.Write(soldAtUnixMs);
		}

		bool Deserialize(
			NetworkLib::Packet::Serialization::FPacketReader& reader) override
		{
			return reader.Read(resultCode) && reader.Read(requestId) && reader.Read(listingId) && reader.Read(sellerLoginId) &&
				   reader.Read(itemDataId) && reader.Read(itemCategory) && reader.Read(quantity) && reader.Read(itemData) &&
				   reader.Read(name) && reader.Read(strStat) && reader.Read(dexStat) && reader.Read(intStat) && reader.Read(lukStat) &&
				   reader.Read(currencyId) && reader.Read(startPrice) && reader.Read(finalPrice) && reader.Read(saleType) &&
				   reader.Read(soldAtUnixMs);
		}
	};

	class FBidRefundRq final : public NetworkLib::Packet::Serialization::IContentPacket
	{
	public:
		static constexpr std::uint16_t kOpcode = 4012;
		static constexpr NetworkLib::Packet::Serialization::EContentPacketKind kPacketKind =
			NetworkLib::Packet::Serialization::EContentPacketKind::Request;

		std::uint64_t requestId{};
		std::uint64_t listingId{};
		std::uint64_t bidId{};
		std::uint64_t expectedBidVersion{};

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
				   NetworkLib::Packet::Serialization::GetSerializedSize(listingId) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(bidId) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(expectedBidVersion);
		}

		void Serialize(
			NetworkLib::Packet::Serialization::FPacketWriter& writer) const override
		{
			writer.Write(requestId);
			writer.Write(listingId);
			writer.Write(bidId);
			writer.Write(expectedBidVersion);
		}

		bool Deserialize(
			NetworkLib::Packet::Serialization::FPacketReader& reader) override
		{
			return reader.Read(requestId) && reader.Read(listingId) && reader.Read(bidId) && reader.Read(expectedBidVersion);
		}
	};

	class FBidRefundRp final : public NetworkLib::Packet::Serialization::IResponsePacket
	{
	public:
		static constexpr std::uint16_t kOpcode = 4013;
		static constexpr NetworkLib::Packet::Serialization::EContentPacketKind kPacketKind =
			NetworkLib::Packet::Serialization::EContentPacketKind::Response;

		std::uint16_t resultCode{};
		std::uint64_t requestId{};
		std::uint64_t bidId{};
		std::uint64_t refundedAmount{};
		std::uint64_t currencyBalance{};
		std::uint8_t bidState{};
		std::uint64_t bidVersion{};

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
				   NetworkLib::Packet::Serialization::GetSerializedSize(bidId) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(refundedAmount) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(currencyBalance) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(bidState) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(bidVersion);
		}

		void Serialize(
			NetworkLib::Packet::Serialization::FPacketWriter& writer) const override
		{
			writer.Write(resultCode);
			writer.Write(requestId);
			writer.Write(bidId);
			writer.Write(refundedAmount);
			writer.Write(currencyBalance);
			writer.Write(bidState);
			writer.Write(bidVersion);
		}

		bool Deserialize(
			NetworkLib::Packet::Serialization::FPacketReader& reader) override
		{
			return reader.Read(resultCode) && reader.Read(requestId) && reader.Read(bidId) && reader.Read(refundedAmount) &&
				   reader.Read(currencyBalance) && reader.Read(bidState) && reader.Read(bidVersion);
		}
	};

}
