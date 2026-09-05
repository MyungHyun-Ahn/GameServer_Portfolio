#pragma once

// Generated from RPC YAML. Keep deterministic layout; do not format by hand.
// clang-format off
namespace Cache::Protocol
{
	enum class ECacheUserLoadResult : std::uint8_t;
	enum class ECacheQueryResult : std::uint8_t;
	enum class ECacheCommandResult : std::uint8_t;
	enum class EPlayerProgressResult : std::uint8_t;
	enum class EPlayerEquipmentResult : std::uint8_t;
	struct FInventoryItem;
	struct FMailSummary;
	struct FMailAttachment;
	struct FMailDetail;
	struct FCurrencyBalance;
	struct FInventoryItemSnapshot;
	struct FMailClaimResult;
	struct FBuyoutSettlementResult;
	struct FExpirationSettlementResult;
	struct FPlayerProgressSnapshot;
	struct FEquippedItemSnapshot;
	struct FPlayerWorldSnapshot;

	inline constexpr RpcLib::Protocol::FRpcServiceId kCacheServiceId = 1;

	enum class ECacheUserLoadResult : std::uint8_t
	{
		Success = 0,
		InvalidUserId = 1,
		DatabaseError = 2,
		InvalidSnapshot = 3,
	};

	enum class ECacheQueryResult : std::uint8_t
	{
		Success = 0,
		InvalidArgument = 1,
		DatabaseError = 2,
		NotFound = 3,
	};

	enum class ECacheCommandResult : std::uint8_t
	{
		Success = 0,
		InvalidArgument = 1,
		DatabaseError = 2,
		NotFound = 3,
		ItemVersionMismatch = 4,
		ItemEquipped = 5,
		InventoryFull = 6,
		ItemInstanceConflict = 7,
		CurrencyLimitExceeded = 8,
		MailAttachmentNotClaimable = 9,
		InsufficientCurrency = 10,
		ConcurrentModification = 11,
		OutcomeUnknown = 12,
	};

	enum class EPlayerProgressResult : std::uint8_t
	{
		Success = 0,
		InvalidArgument = 1,
		DatabaseError = 2,
		CharacterDataError = 3,
		ConcurrentModification = 4,
		InsufficientStatPoints = 5,
		LevelCap = 6,
		UnauthorizedCaller = 7,
		OutcomeUnknown = 8,
	};

	enum class EPlayerEquipmentResult : std::uint8_t
	{
		Success = 0,
		InvalidArgument = 1,
		DatabaseError = 2,
		CharacterDataError = 3,
		ConcurrentModification = 4,
		UnauthorizedCaller = 5,
		OutcomeUnknown = 6,
		ItemNotFound = 7,
		ItemVersionMismatch = 8,
		NotEquipment = 9,
		EquipmentStateConflict = 10,
	};

	struct FInventoryItem final
	{
		std::uint64_t itemInstanceId{};
		std::uint32_t itemDataId{};
		std::uint32_t quantity{};
		std::string itemDataJson{};
		bool equipped{};
		bool tradable{};
		std::uint64_t version{};

		void Serialize(NetworkLib::Packet::Serialization::FPacketWriter& writer) const
		{
			if (!RpcLib::Protocol::WriteRpcValue(writer, itemInstanceId))
			{
				throw std::runtime_error("RPC struct serialization failed.");
			}
			if (!RpcLib::Protocol::WriteRpcValue(writer, itemDataId))
			{
				throw std::runtime_error("RPC struct serialization failed.");
			}
			if (!RpcLib::Protocol::WriteRpcValue(writer, quantity))
			{
				throw std::runtime_error("RPC struct serialization failed.");
			}
			if (!RpcLib::Protocol::WriteRpcValue(writer, itemDataJson))
			{
				throw std::runtime_error("RPC struct serialization failed.");
			}
			if (!RpcLib::Protocol::WriteRpcValue(writer, equipped))
			{
				throw std::runtime_error("RPC struct serialization failed.");
			}
			if (!RpcLib::Protocol::WriteRpcValue(writer, tradable))
			{
				throw std::runtime_error("RPC struct serialization failed.");
			}
			if (!RpcLib::Protocol::WriteRpcValue(writer, version))
			{
				throw std::runtime_error("RPC struct serialization failed.");
			}
		}

		bool Deserialize(NetworkLib::Packet::Serialization::FPacketReader& reader)
		{
			return RpcLib::Protocol::ReadRpcValue(reader, itemInstanceId) &&
				RpcLib::Protocol::ReadRpcValue(reader, itemDataId) &&
				RpcLib::Protocol::ReadRpcValue(reader, quantity) &&
				RpcLib::Protocol::ReadRpcValue(reader, itemDataJson) &&
				RpcLib::Protocol::ReadRpcValue(reader, equipped) &&
				RpcLib::Protocol::ReadRpcValue(reader, tradable) &&
				RpcLib::Protocol::ReadRpcValue(reader, version);
		}
	};

	struct FMailSummary final
	{
		std::uint64_t mailId{};
		std::uint8_t mailType{};
		std::string subject{};
		std::uint8_t state{};
		std::uint64_t expiresAtUnixMs{};
		std::uint64_t createdAtUnixMs{};

		void Serialize(NetworkLib::Packet::Serialization::FPacketWriter& writer) const
		{
			if (!RpcLib::Protocol::WriteRpcValue(writer, mailId))
			{
				throw std::runtime_error("RPC struct serialization failed.");
			}
			if (!RpcLib::Protocol::WriteRpcValue(writer, mailType))
			{
				throw std::runtime_error("RPC struct serialization failed.");
			}
			if (!RpcLib::Protocol::WriteRpcValue(writer, subject))
			{
				throw std::runtime_error("RPC struct serialization failed.");
			}
			if (!RpcLib::Protocol::WriteRpcValue(writer, state))
			{
				throw std::runtime_error("RPC struct serialization failed.");
			}
			if (!RpcLib::Protocol::WriteRpcValue(writer, expiresAtUnixMs))
			{
				throw std::runtime_error("RPC struct serialization failed.");
			}
			if (!RpcLib::Protocol::WriteRpcValue(writer, createdAtUnixMs))
			{
				throw std::runtime_error("RPC struct serialization failed.");
			}
		}

		bool Deserialize(NetworkLib::Packet::Serialization::FPacketReader& reader)
		{
			return RpcLib::Protocol::ReadRpcValue(reader, mailId) &&
				RpcLib::Protocol::ReadRpcValue(reader, mailType) &&
				RpcLib::Protocol::ReadRpcValue(reader, subject) &&
				RpcLib::Protocol::ReadRpcValue(reader, state) &&
				RpcLib::Protocol::ReadRpcValue(reader, expiresAtUnixMs) &&
				RpcLib::Protocol::ReadRpcValue(reader, createdAtUnixMs);
		}
	};

	struct FMailAttachment final
	{
		std::uint64_t attachmentId{};
		std::uint8_t attachmentType{};
		std::uint64_t itemInstanceId{};
		std::uint32_t itemDataId{};
		std::uint32_t quantity{};
		std::string itemDataJson{};
		std::uint16_t currencyId{};
		std::uint64_t currencyAmount{};
		std::uint8_t state{};

		void Serialize(NetworkLib::Packet::Serialization::FPacketWriter& writer) const
		{
			if (!RpcLib::Protocol::WriteRpcValue(writer, attachmentId))
			{
				throw std::runtime_error("RPC struct serialization failed.");
			}
			if (!RpcLib::Protocol::WriteRpcValue(writer, attachmentType))
			{
				throw std::runtime_error("RPC struct serialization failed.");
			}
			if (!RpcLib::Protocol::WriteRpcValue(writer, itemInstanceId))
			{
				throw std::runtime_error("RPC struct serialization failed.");
			}
			if (!RpcLib::Protocol::WriteRpcValue(writer, itemDataId))
			{
				throw std::runtime_error("RPC struct serialization failed.");
			}
			if (!RpcLib::Protocol::WriteRpcValue(writer, quantity))
			{
				throw std::runtime_error("RPC struct serialization failed.");
			}
			if (!RpcLib::Protocol::WriteRpcValue(writer, itemDataJson))
			{
				throw std::runtime_error("RPC struct serialization failed.");
			}
			if (!RpcLib::Protocol::WriteRpcValue(writer, currencyId))
			{
				throw std::runtime_error("RPC struct serialization failed.");
			}
			if (!RpcLib::Protocol::WriteRpcValue(writer, currencyAmount))
			{
				throw std::runtime_error("RPC struct serialization failed.");
			}
			if (!RpcLib::Protocol::WriteRpcValue(writer, state))
			{
				throw std::runtime_error("RPC struct serialization failed.");
			}
		}

		bool Deserialize(NetworkLib::Packet::Serialization::FPacketReader& reader)
		{
			return RpcLib::Protocol::ReadRpcValue(reader, attachmentId) &&
				RpcLib::Protocol::ReadRpcValue(reader, attachmentType) &&
				RpcLib::Protocol::ReadRpcValue(reader, itemInstanceId) &&
				RpcLib::Protocol::ReadRpcValue(reader, itemDataId) &&
				RpcLib::Protocol::ReadRpcValue(reader, quantity) &&
				RpcLib::Protocol::ReadRpcValue(reader, itemDataJson) &&
				RpcLib::Protocol::ReadRpcValue(reader, currencyId) &&
				RpcLib::Protocol::ReadRpcValue(reader, currencyAmount) &&
				RpcLib::Protocol::ReadRpcValue(reader, state);
		}
	};

	struct FMailDetail final
	{
		std::uint64_t mailId{};
		std::uint8_t mailType{};
		std::string subject{};
		std::string body{};
		std::uint8_t state{};
		std::uint64_t expiresAtUnixMs{};
		std::vector<FMailAttachment> attachments{};

		void Serialize(NetworkLib::Packet::Serialization::FPacketWriter& writer) const
		{
			if (!RpcLib::Protocol::WriteRpcValue(writer, mailId))
			{
				throw std::runtime_error("RPC struct serialization failed.");
			}
			if (!RpcLib::Protocol::WriteRpcValue(writer, mailType))
			{
				throw std::runtime_error("RPC struct serialization failed.");
			}
			if (!RpcLib::Protocol::WriteRpcValue(writer, subject))
			{
				throw std::runtime_error("RPC struct serialization failed.");
			}
			if (!RpcLib::Protocol::WriteRpcValue(writer, body))
			{
				throw std::runtime_error("RPC struct serialization failed.");
			}
			if (!RpcLib::Protocol::WriteRpcValue(writer, state))
			{
				throw std::runtime_error("RPC struct serialization failed.");
			}
			if (!RpcLib::Protocol::WriteRpcValue(writer, expiresAtUnixMs))
			{
				throw std::runtime_error("RPC struct serialization failed.");
			}
			if (!RpcLib::Protocol::WriteRpcValue(writer, attachments))
			{
				throw std::runtime_error("RPC struct serialization failed.");
			}
		}

		bool Deserialize(NetworkLib::Packet::Serialization::FPacketReader& reader)
		{
			return RpcLib::Protocol::ReadRpcValue(reader, mailId) &&
				RpcLib::Protocol::ReadRpcValue(reader, mailType) &&
				RpcLib::Protocol::ReadRpcValue(reader, subject) &&
				RpcLib::Protocol::ReadRpcValue(reader, body) &&
				RpcLib::Protocol::ReadRpcValue(reader, state) &&
				RpcLib::Protocol::ReadRpcValue(reader, expiresAtUnixMs) &&
				RpcLib::Protocol::ReadRpcValue(reader, attachments);
		}
	};

	struct FCurrencyBalance final
	{
		std::uint16_t currencyId{};
		std::uint64_t amount{};
		std::uint64_t version{};

		void Serialize(NetworkLib::Packet::Serialization::FPacketWriter& writer) const
		{
			if (!RpcLib::Protocol::WriteRpcValue(writer, currencyId))
			{
				throw std::runtime_error("RPC struct serialization failed.");
			}
			if (!RpcLib::Protocol::WriteRpcValue(writer, amount))
			{
				throw std::runtime_error("RPC struct serialization failed.");
			}
			if (!RpcLib::Protocol::WriteRpcValue(writer, version))
			{
				throw std::runtime_error("RPC struct serialization failed.");
			}
		}

		bool Deserialize(NetworkLib::Packet::Serialization::FPacketReader& reader)
		{
			return RpcLib::Protocol::ReadRpcValue(reader, currencyId) &&
				RpcLib::Protocol::ReadRpcValue(reader, amount) &&
				RpcLib::Protocol::ReadRpcValue(reader, version);
		}
	};

	struct FInventoryItemSnapshot final
	{
		std::uint64_t itemInstanceId{};
		std::uint32_t itemDataId{};
		std::uint32_t quantity{};
		std::string itemDataJson{};
		bool equipped{};
		bool tradable{};
		std::uint64_t version{};
		std::uint32_t strStat{};
		std::uint32_t dexStat{};
		std::uint32_t intStat{};
		std::uint32_t lukStat{};

		void Serialize(NetworkLib::Packet::Serialization::FPacketWriter& writer) const
		{
			if (!RpcLib::Protocol::WriteRpcValue(writer, itemInstanceId))
			{
				throw std::runtime_error("RPC struct serialization failed.");
			}
			if (!RpcLib::Protocol::WriteRpcValue(writer, itemDataId))
			{
				throw std::runtime_error("RPC struct serialization failed.");
			}
			if (!RpcLib::Protocol::WriteRpcValue(writer, quantity))
			{
				throw std::runtime_error("RPC struct serialization failed.");
			}
			if (!RpcLib::Protocol::WriteRpcValue(writer, itemDataJson))
			{
				throw std::runtime_error("RPC struct serialization failed.");
			}
			if (!RpcLib::Protocol::WriteRpcValue(writer, equipped))
			{
				throw std::runtime_error("RPC struct serialization failed.");
			}
			if (!RpcLib::Protocol::WriteRpcValue(writer, tradable))
			{
				throw std::runtime_error("RPC struct serialization failed.");
			}
			if (!RpcLib::Protocol::WriteRpcValue(writer, version))
			{
				throw std::runtime_error("RPC struct serialization failed.");
			}
			if (!RpcLib::Protocol::WriteRpcValue(writer, strStat))
			{
				throw std::runtime_error("RPC struct serialization failed.");
			}
			if (!RpcLib::Protocol::WriteRpcValue(writer, dexStat))
			{
				throw std::runtime_error("RPC struct serialization failed.");
			}
			if (!RpcLib::Protocol::WriteRpcValue(writer, intStat))
			{
				throw std::runtime_error("RPC struct serialization failed.");
			}
			if (!RpcLib::Protocol::WriteRpcValue(writer, lukStat))
			{
				throw std::runtime_error("RPC struct serialization failed.");
			}
		}

		bool Deserialize(NetworkLib::Packet::Serialization::FPacketReader& reader)
		{
			return RpcLib::Protocol::ReadRpcValue(reader, itemInstanceId) &&
				RpcLib::Protocol::ReadRpcValue(reader, itemDataId) &&
				RpcLib::Protocol::ReadRpcValue(reader, quantity) &&
				RpcLib::Protocol::ReadRpcValue(reader, itemDataJson) &&
				RpcLib::Protocol::ReadRpcValue(reader, equipped) &&
				RpcLib::Protocol::ReadRpcValue(reader, tradable) &&
				RpcLib::Protocol::ReadRpcValue(reader, version) &&
				RpcLib::Protocol::ReadRpcValue(reader, strStat) &&
				RpcLib::Protocol::ReadRpcValue(reader, dexStat) &&
				RpcLib::Protocol::ReadRpcValue(reader, intStat) &&
				RpcLib::Protocol::ReadRpcValue(reader, lukStat);
		}
	};

	struct FMailClaimResult final
	{
		std::uint8_t attachmentType{};
		std::uint64_t itemInstanceId{};
		std::uint32_t itemDataId{};
		std::uint32_t quantity{};
		std::string itemDataJson{};
		std::uint16_t currencyId{};
		std::uint64_t currencyAmount{};
		std::uint64_t currencyBalance{};
		std::uint64_t currencyVersion{};
		std::uint8_t mailState{};

		void Serialize(NetworkLib::Packet::Serialization::FPacketWriter& writer) const
		{
			if (!RpcLib::Protocol::WriteRpcValue(writer, attachmentType))
			{
				throw std::runtime_error("RPC struct serialization failed.");
			}
			if (!RpcLib::Protocol::WriteRpcValue(writer, itemInstanceId))
			{
				throw std::runtime_error("RPC struct serialization failed.");
			}
			if (!RpcLib::Protocol::WriteRpcValue(writer, itemDataId))
			{
				throw std::runtime_error("RPC struct serialization failed.");
			}
			if (!RpcLib::Protocol::WriteRpcValue(writer, quantity))
			{
				throw std::runtime_error("RPC struct serialization failed.");
			}
			if (!RpcLib::Protocol::WriteRpcValue(writer, itemDataJson))
			{
				throw std::runtime_error("RPC struct serialization failed.");
			}
			if (!RpcLib::Protocol::WriteRpcValue(writer, currencyId))
			{
				throw std::runtime_error("RPC struct serialization failed.");
			}
			if (!RpcLib::Protocol::WriteRpcValue(writer, currencyAmount))
			{
				throw std::runtime_error("RPC struct serialization failed.");
			}
			if (!RpcLib::Protocol::WriteRpcValue(writer, currencyBalance))
			{
				throw std::runtime_error("RPC struct serialization failed.");
			}
			if (!RpcLib::Protocol::WriteRpcValue(writer, currencyVersion))
			{
				throw std::runtime_error("RPC struct serialization failed.");
			}
			if (!RpcLib::Protocol::WriteRpcValue(writer, mailState))
			{
				throw std::runtime_error("RPC struct serialization failed.");
			}
		}

		bool Deserialize(NetworkLib::Packet::Serialization::FPacketReader& reader)
		{
			return RpcLib::Protocol::ReadRpcValue(reader, attachmentType) &&
				RpcLib::Protocol::ReadRpcValue(reader, itemInstanceId) &&
				RpcLib::Protocol::ReadRpcValue(reader, itemDataId) &&
				RpcLib::Protocol::ReadRpcValue(reader, quantity) &&
				RpcLib::Protocol::ReadRpcValue(reader, itemDataJson) &&
				RpcLib::Protocol::ReadRpcValue(reader, currencyId) &&
				RpcLib::Protocol::ReadRpcValue(reader, currencyAmount) &&
				RpcLib::Protocol::ReadRpcValue(reader, currencyBalance) &&
				RpcLib::Protocol::ReadRpcValue(reader, currencyVersion) &&
				RpcLib::Protocol::ReadRpcValue(reader, mailState);
		}
	};

	struct FBuyoutSettlementResult final
	{
		FCurrencyBalance currencyBalance{};
		std::uint64_t itemMailId{};
		std::uint64_t sellerMailId{};

		void Serialize(NetworkLib::Packet::Serialization::FPacketWriter& writer) const
		{
			if (!RpcLib::Protocol::WriteRpcValue(writer, currencyBalance))
			{
				throw std::runtime_error("RPC struct serialization failed.");
			}
			if (!RpcLib::Protocol::WriteRpcValue(writer, itemMailId))
			{
				throw std::runtime_error("RPC struct serialization failed.");
			}
			if (!RpcLib::Protocol::WriteRpcValue(writer, sellerMailId))
			{
				throw std::runtime_error("RPC struct serialization failed.");
			}
		}

		bool Deserialize(NetworkLib::Packet::Serialization::FPacketReader& reader)
		{
			return RpcLib::Protocol::ReadRpcValue(reader, currencyBalance) &&
				RpcLib::Protocol::ReadRpcValue(reader, itemMailId) &&
				RpcLib::Protocol::ReadRpcValue(reader, sellerMailId);
		}
	};

	struct FExpirationSettlementResult final
	{
		std::uint64_t itemMailId{};
		std::uint64_t sellerMailId{};

		void Serialize(NetworkLib::Packet::Serialization::FPacketWriter& writer) const
		{
			if (!RpcLib::Protocol::WriteRpcValue(writer, itemMailId))
			{
				throw std::runtime_error("RPC struct serialization failed.");
			}
			if (!RpcLib::Protocol::WriteRpcValue(writer, sellerMailId))
			{
				throw std::runtime_error("RPC struct serialization failed.");
			}
		}

		bool Deserialize(NetworkLib::Packet::Serialization::FPacketReader& reader)
		{
			return RpcLib::Protocol::ReadRpcValue(reader, itemMailId) &&
				RpcLib::Protocol::ReadRpcValue(reader, sellerMailId);
		}
	};

	struct FPlayerProgressSnapshot final
	{
		std::uint64_t characterId{};
		std::uint32_t characterDataId{};
		std::uint32_t level{};
		std::uint64_t exp{};
		std::uint32_t strStat{};
		std::uint32_t dexStat{};
		std::uint32_t intStat{};
		std::uint32_t lukStat{};
		std::uint32_t unspentStatPoints{};
		std::uint64_t progressVersion{};
		std::uint64_t statVersion{};

		void Serialize(NetworkLib::Packet::Serialization::FPacketWriter& writer) const
		{
			if (!RpcLib::Protocol::WriteRpcValue(writer, characterId))
			{
				throw std::runtime_error("RPC struct serialization failed.");
			}
			if (!RpcLib::Protocol::WriteRpcValue(writer, characterDataId))
			{
				throw std::runtime_error("RPC struct serialization failed.");
			}
			if (!RpcLib::Protocol::WriteRpcValue(writer, level))
			{
				throw std::runtime_error("RPC struct serialization failed.");
			}
			if (!RpcLib::Protocol::WriteRpcValue(writer, exp))
			{
				throw std::runtime_error("RPC struct serialization failed.");
			}
			if (!RpcLib::Protocol::WriteRpcValue(writer, strStat))
			{
				throw std::runtime_error("RPC struct serialization failed.");
			}
			if (!RpcLib::Protocol::WriteRpcValue(writer, dexStat))
			{
				throw std::runtime_error("RPC struct serialization failed.");
			}
			if (!RpcLib::Protocol::WriteRpcValue(writer, intStat))
			{
				throw std::runtime_error("RPC struct serialization failed.");
			}
			if (!RpcLib::Protocol::WriteRpcValue(writer, lukStat))
			{
				throw std::runtime_error("RPC struct serialization failed.");
			}
			if (!RpcLib::Protocol::WriteRpcValue(writer, unspentStatPoints))
			{
				throw std::runtime_error("RPC struct serialization failed.");
			}
			if (!RpcLib::Protocol::WriteRpcValue(writer, progressVersion))
			{
				throw std::runtime_error("RPC struct serialization failed.");
			}
			if (!RpcLib::Protocol::WriteRpcValue(writer, statVersion))
			{
				throw std::runtime_error("RPC struct serialization failed.");
			}
		}

		bool Deserialize(NetworkLib::Packet::Serialization::FPacketReader& reader)
		{
			return RpcLib::Protocol::ReadRpcValue(reader, characterId) &&
				RpcLib::Protocol::ReadRpcValue(reader, characterDataId) &&
				RpcLib::Protocol::ReadRpcValue(reader, level) &&
				RpcLib::Protocol::ReadRpcValue(reader, exp) &&
				RpcLib::Protocol::ReadRpcValue(reader, strStat) &&
				RpcLib::Protocol::ReadRpcValue(reader, dexStat) &&
				RpcLib::Protocol::ReadRpcValue(reader, intStat) &&
				RpcLib::Protocol::ReadRpcValue(reader, lukStat) &&
				RpcLib::Protocol::ReadRpcValue(reader, unspentStatPoints) &&
				RpcLib::Protocol::ReadRpcValue(reader, progressVersion) &&
				RpcLib::Protocol::ReadRpcValue(reader, statVersion);
		}
	};

	struct FEquippedItemSnapshot final
	{
		std::uint64_t itemInstanceId{};
		std::uint32_t itemDataId{};
		std::uint64_t itemVersion{};
		std::uint32_t strStat{};
		std::uint32_t dexStat{};
		std::uint32_t intStat{};
		std::uint32_t lukStat{};

		void Serialize(NetworkLib::Packet::Serialization::FPacketWriter& writer) const
		{
			if (!RpcLib::Protocol::WriteRpcValue(writer, itemInstanceId))
			{
				throw std::runtime_error("RPC struct serialization failed.");
			}
			if (!RpcLib::Protocol::WriteRpcValue(writer, itemDataId))
			{
				throw std::runtime_error("RPC struct serialization failed.");
			}
			if (!RpcLib::Protocol::WriteRpcValue(writer, itemVersion))
			{
				throw std::runtime_error("RPC struct serialization failed.");
			}
			if (!RpcLib::Protocol::WriteRpcValue(writer, strStat))
			{
				throw std::runtime_error("RPC struct serialization failed.");
			}
			if (!RpcLib::Protocol::WriteRpcValue(writer, dexStat))
			{
				throw std::runtime_error("RPC struct serialization failed.");
			}
			if (!RpcLib::Protocol::WriteRpcValue(writer, intStat))
			{
				throw std::runtime_error("RPC struct serialization failed.");
			}
			if (!RpcLib::Protocol::WriteRpcValue(writer, lukStat))
			{
				throw std::runtime_error("RPC struct serialization failed.");
			}
		}

		bool Deserialize(NetworkLib::Packet::Serialization::FPacketReader& reader)
		{
			return RpcLib::Protocol::ReadRpcValue(reader, itemInstanceId) &&
				RpcLib::Protocol::ReadRpcValue(reader, itemDataId) &&
				RpcLib::Protocol::ReadRpcValue(reader, itemVersion) &&
				RpcLib::Protocol::ReadRpcValue(reader, strStat) &&
				RpcLib::Protocol::ReadRpcValue(reader, dexStat) &&
				RpcLib::Protocol::ReadRpcValue(reader, intStat) &&
				RpcLib::Protocol::ReadRpcValue(reader, lukStat);
		}
	};

	struct FPlayerWorldSnapshot final
	{
		FPlayerProgressSnapshot progress{};
		std::vector<FEquippedItemSnapshot> equippedItems{};
		std::uint64_t equipmentVersion{};
		std::uint64_t statRevision{};

		void Serialize(NetworkLib::Packet::Serialization::FPacketWriter& writer) const
		{
			if (!RpcLib::Protocol::WriteRpcValue(writer, progress))
			{
				throw std::runtime_error("RPC struct serialization failed.");
			}
			if (!RpcLib::Protocol::WriteRpcValue(writer, equippedItems))
			{
				throw std::runtime_error("RPC struct serialization failed.");
			}
			if (!RpcLib::Protocol::WriteRpcValue(writer, equipmentVersion))
			{
				throw std::runtime_error("RPC struct serialization failed.");
			}
			if (!RpcLib::Protocol::WriteRpcValue(writer, statRevision))
			{
				throw std::runtime_error("RPC struct serialization failed.");
			}
		}

		bool Deserialize(NetworkLib::Packet::Serialization::FPacketReader& reader)
		{
			return RpcLib::Protocol::ReadRpcValue(reader, progress) &&
				RpcLib::Protocol::ReadRpcValue(reader, equippedItems) &&
				RpcLib::Protocol::ReadRpcValue(reader, equipmentVersion) &&
				RpcLib::Protocol::ReadRpcValue(reader, statRevision);
		}
	};

	struct FCachePingRpc final
	{
		static constexpr RpcLib::Protocol::FRpcServiceId kServiceId = kCacheServiceId;
		static constexpr RpcLib::Protocol::FRpcMethodId kMethodId = 1;
		static constexpr const char* kName = "Cache.CachePing";
		static constexpr bool kHasRoutingKey = true;
		static constexpr std::size_t kRoutingKeyArgumentIndex = 1;

		using FRequestArguments = std::tuple<std::uint64_t, std::uint64_t, std::uint64_t>;
		using FResponseArguments = std::tuple<std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint32_t, std::uint32_t, std::uint64_t, std::uint32_t>;
	};

	struct FCachePingNoti final
	{
		static constexpr RpcLib::Protocol::FRpcServiceId kServiceId = kCacheServiceId;
		static constexpr RpcLib::Protocol::FRpcMethodId kMethodId = 1;
		static constexpr const char* kName = "Cache.CachePing";
		static constexpr bool kHasRoutingKey = true;
		static constexpr std::size_t kRoutingKeyArgumentIndex = 1;

		using FArguments = std::tuple<std::uint64_t, std::uint64_t, std::uint64_t>;
	};

	struct FLoadCacheUserRpc final
	{
		static constexpr RpcLib::Protocol::FRpcServiceId kServiceId = kCacheServiceId;
		static constexpr RpcLib::Protocol::FRpcMethodId kMethodId = 2;
		static constexpr const char* kName = "Cache.LoadCacheUser";
		static constexpr bool kHasRoutingKey = true;
		static constexpr std::size_t kRoutingKeyArgumentIndex = 1;

		using FRequestArguments = std::tuple<std::uint64_t, std::uint64_t>;
		using FResponseArguments = std::tuple<std::uint64_t, std::uint64_t, ECacheUserLoadResult, std::uint8_t, std::uint32_t, std::uint32_t, std::uint64_t, std::uint32_t, std::uint32_t, std::uint64_t>;
	};

	struct FGetInventoryRpc final
	{
		static constexpr RpcLib::Protocol::FRpcServiceId kServiceId = kCacheServiceId;
		static constexpr RpcLib::Protocol::FRpcMethodId kMethodId = 3;
		static constexpr const char* kName = "Cache.GetInventory";
		static constexpr bool kHasRoutingKey = true;
		static constexpr std::size_t kRoutingKeyArgumentIndex = 0;

		using FRequestArguments = std::tuple<std::uint64_t, std::uint64_t, std::uint32_t>;
		using FResponseArguments = std::tuple<ECacheQueryResult, std::vector<FInventoryItem>>;
	};

	struct FGetMailListRpc final
	{
		static constexpr RpcLib::Protocol::FRpcServiceId kServiceId = kCacheServiceId;
		static constexpr RpcLib::Protocol::FRpcMethodId kMethodId = 4;
		static constexpr const char* kName = "Cache.GetMailList";
		static constexpr bool kHasRoutingKey = true;
		static constexpr std::size_t kRoutingKeyArgumentIndex = 0;

		using FRequestArguments = std::tuple<std::uint64_t, std::uint64_t, std::uint32_t>;
		using FResponseArguments = std::tuple<ECacheQueryResult, std::vector<FMailSummary>>;
	};

	struct FGetMailDetailRpc final
	{
		static constexpr RpcLib::Protocol::FRpcServiceId kServiceId = kCacheServiceId;
		static constexpr RpcLib::Protocol::FRpcMethodId kMethodId = 5;
		static constexpr const char* kName = "Cache.GetMailDetail";
		static constexpr bool kHasRoutingKey = true;
		static constexpr std::size_t kRoutingKeyArgumentIndex = 0;

		using FRequestArguments = std::tuple<std::uint64_t, std::uint64_t>;
		using FResponseArguments = std::tuple<ECacheQueryResult, FMailDetail>;
	};

	struct FGetCurrencyRpc final
	{
		static constexpr RpcLib::Protocol::FRpcServiceId kServiceId = kCacheServiceId;
		static constexpr RpcLib::Protocol::FRpcMethodId kMethodId = 6;
		static constexpr const char* kName = "Cache.GetCurrency";
		static constexpr bool kHasRoutingKey = true;
		static constexpr std::size_t kRoutingKeyArgumentIndex = 0;

		using FRequestArguments = std::tuple<std::uint64_t, std::uint16_t>;
		using FResponseArguments = std::tuple<ECacheQueryResult, FCurrencyBalance>;
	};

	struct FGetInventoryItemRpc final
	{
		static constexpr RpcLib::Protocol::FRpcServiceId kServiceId = kCacheServiceId;
		static constexpr RpcLib::Protocol::FRpcMethodId kMethodId = 7;
		static constexpr const char* kName = "Cache.GetInventoryItem";
		static constexpr bool kHasRoutingKey = true;
		static constexpr std::size_t kRoutingKeyArgumentIndex = 0;

		using FRequestArguments = std::tuple<std::uint64_t, std::uint64_t>;
		using FResponseArguments = std::tuple<ECacheQueryResult, FInventoryItemSnapshot>;
	};

	struct FCreditCurrencyRpc final
	{
		static constexpr RpcLib::Protocol::FRpcServiceId kServiceId = kCacheServiceId;
		static constexpr RpcLib::Protocol::FRpcMethodId kMethodId = 8;
		static constexpr const char* kName = "Cache.CreditCurrency";
		static constexpr bool kHasRoutingKey = true;
		static constexpr std::size_t kRoutingKeyArgumentIndex = 0;

		using FRequestArguments = std::tuple<std::uint64_t, std::uint16_t, std::uint64_t>;
		using FResponseArguments = std::tuple<ECacheCommandResult, FCurrencyBalance>;
	};

	struct FGrantInventoryItemRpc final
	{
		static constexpr RpcLib::Protocol::FRpcServiceId kServiceId = kCacheServiceId;
		static constexpr RpcLib::Protocol::FRpcMethodId kMethodId = 9;
		static constexpr const char* kName = "Cache.GrantInventoryItem";
		static constexpr bool kHasRoutingKey = true;
		static constexpr std::size_t kRoutingKeyArgumentIndex = 0;

		using FRequestArguments = std::tuple<std::uint64_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, bool>;
		using FResponseArguments = std::tuple<ECacheCommandResult, FInventoryItemSnapshot>;
	};

	struct FClaimMailAttachmentRpc final
	{
		static constexpr RpcLib::Protocol::FRpcServiceId kServiceId = kCacheServiceId;
		static constexpr RpcLib::Protocol::FRpcMethodId kMethodId = 10;
		static constexpr const char* kName = "Cache.ClaimMailAttachment";
		static constexpr bool kHasRoutingKey = true;
		static constexpr std::size_t kRoutingKeyArgumentIndex = 0;

		using FRequestArguments = std::tuple<std::uint64_t, std::uint64_t, std::uint64_t>;
		using FResponseArguments = std::tuple<ECacheCommandResult, FMailClaimResult>;
	};

	struct FConsumeInventoryItemForListingRpc final
	{
		static constexpr RpcLib::Protocol::FRpcServiceId kServiceId = kCacheServiceId;
		static constexpr RpcLib::Protocol::FRpcMethodId kMethodId = 11;
		static constexpr const char* kName = "Cache.ConsumeInventoryItemForListing";
		static constexpr bool kHasRoutingKey = true;
		static constexpr std::size_t kRoutingKeyArgumentIndex = 0;

		using FRequestArguments = std::tuple<std::uint64_t, std::uint64_t, std::uint64_t>;
		using FResponseArguments = std::tuple<ECacheCommandResult, FInventoryItemSnapshot>;
	};

	struct FDebitCurrencyRpc final
	{
		static constexpr RpcLib::Protocol::FRpcServiceId kServiceId = kCacheServiceId;
		static constexpr RpcLib::Protocol::FRpcMethodId kMethodId = 12;
		static constexpr const char* kName = "Cache.DebitCurrency";
		static constexpr bool kHasRoutingKey = true;
		static constexpr std::size_t kRoutingKeyArgumentIndex = 0;

		using FRequestArguments = std::tuple<std::uint64_t, std::uint16_t, std::uint64_t>;
		using FResponseArguments = std::tuple<ECacheCommandResult, FCurrencyBalance>;
	};

	struct FSettleBuyoutRpc final
	{
		static constexpr RpcLib::Protocol::FRpcServiceId kServiceId = kCacheServiceId;
		static constexpr RpcLib::Protocol::FRpcMethodId kMethodId = 13;
		static constexpr const char* kName = "Cache.SettleBuyout";
		static constexpr bool kHasRoutingKey = true;
		static constexpr std::size_t kRoutingKeyArgumentIndex = 0;

		using FRequestArguments = std::tuple<std::uint64_t, std::uint64_t, std::uint16_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint32_t, std::uint32_t, std::string>;
		using FResponseArguments = std::tuple<ECacheCommandResult, FBuyoutSettlementResult>;
	};

	struct FCreateListingReturnMailRpc final
	{
		static constexpr RpcLib::Protocol::FRpcServiceId kServiceId = kCacheServiceId;
		static constexpr RpcLib::Protocol::FRpcMethodId kMethodId = 14;
		static constexpr const char* kName = "Cache.CreateListingReturnMail";
		static constexpr bool kHasRoutingKey = true;
		static constexpr std::size_t kRoutingKeyArgumentIndex = 0;

		using FRequestArguments = std::tuple<std::uint64_t, std::uint64_t, std::uint32_t, std::uint32_t, std::string>;
		using FResponseArguments = std::tuple<ECacheCommandResult, std::uint64_t>;
	};

	struct FSettleExpirationRpc final
	{
		static constexpr RpcLib::Protocol::FRpcServiceId kServiceId = kCacheServiceId;
		static constexpr RpcLib::Protocol::FRpcMethodId kMethodId = 15;
		static constexpr const char* kName = "Cache.SettleExpiration";
		static constexpr bool kHasRoutingKey = true;
		static constexpr std::size_t kRoutingKeyArgumentIndex = 0;

		using FRequestArguments = std::tuple<std::uint64_t, std::uint64_t, std::uint64_t, std::uint16_t, std::uint64_t, std::uint64_t, std::uint32_t, std::uint32_t, std::string>;
		using FResponseArguments = std::tuple<ECacheCommandResult, FExpirationSettlementResult>;
	};

	struct FGetPlayerWorldSnapshotRpc final
	{
		static constexpr RpcLib::Protocol::FRpcServiceId kServiceId = kCacheServiceId;
		static constexpr RpcLib::Protocol::FRpcMethodId kMethodId = 16;
		static constexpr const char* kName = "Cache.GetPlayerWorldSnapshot";
		static constexpr bool kHasRoutingKey = true;
		static constexpr std::size_t kRoutingKeyArgumentIndex = 0;

		using FRequestArguments = std::tuple<std::uint64_t, std::uint64_t, std::uint64_t>;
		using FResponseArguments = std::tuple<EPlayerProgressResult, FPlayerWorldSnapshot>;
	};

	struct FAllocatePlayerStatRpc final
	{
		static constexpr RpcLib::Protocol::FRpcServiceId kServiceId = kCacheServiceId;
		static constexpr RpcLib::Protocol::FRpcMethodId kMethodId = 17;
		static constexpr const char* kName = "Cache.AllocatePlayerStat";
		static constexpr bool kHasRoutingKey = true;
		static constexpr std::size_t kRoutingKeyArgumentIndex = 0;

		using FRequestArguments = std::tuple<std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t>;
		using FResponseArguments = std::tuple<EPlayerProgressResult, FPlayerWorldSnapshot>;
	};

	struct FGrantPlayerExperienceRpc final
	{
		static constexpr RpcLib::Protocol::FRpcServiceId kServiceId = kCacheServiceId;
		static constexpr RpcLib::Protocol::FRpcMethodId kMethodId = 18;
		static constexpr const char* kName = "Cache.GrantPlayerExperience";
		static constexpr bool kHasRoutingKey = true;
		static constexpr std::size_t kRoutingKeyArgumentIndex = 0;

		using FRequestArguments = std::tuple<std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t>;
		using FResponseArguments = std::tuple<EPlayerProgressResult, FPlayerWorldSnapshot>;
	};

	struct FEquipPlayerItemRpc final
	{
		static constexpr RpcLib::Protocol::FRpcServiceId kServiceId = kCacheServiceId;
		static constexpr RpcLib::Protocol::FRpcMethodId kMethodId = 19;
		static constexpr const char* kName = "Cache.EquipPlayerItem";
		static constexpr bool kHasRoutingKey = true;
		static constexpr std::size_t kRoutingKeyArgumentIndex = 0;

		using FRequestArguments = std::tuple<std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t>;
		using FResponseArguments = std::tuple<EPlayerEquipmentResult, FPlayerWorldSnapshot, std::uint64_t, bool>;
	};

	struct FUnequipPlayerItemRpc final
	{
		static constexpr RpcLib::Protocol::FRpcServiceId kServiceId = kCacheServiceId;
		static constexpr RpcLib::Protocol::FRpcMethodId kMethodId = 20;
		static constexpr const char* kName = "Cache.UnequipPlayerItem";
		static constexpr bool kHasRoutingKey = true;
		static constexpr std::size_t kRoutingKeyArgumentIndex = 0;

		using FRequestArguments = std::tuple<std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t>;
		using FResponseArguments = std::tuple<EPlayerEquipmentResult, FPlayerWorldSnapshot, std::uint64_t, bool>;
	};

	struct FEquipPlayerItemV2Rpc final
	{
		static constexpr RpcLib::Protocol::FRpcServiceId kServiceId = kCacheServiceId;
		static constexpr RpcLib::Protocol::FRpcMethodId kMethodId = 21;
		static constexpr const char* kName = "Cache.EquipPlayerItemV2";
		static constexpr bool kHasRoutingKey = true;
		static constexpr std::size_t kRoutingKeyArgumentIndex = 0;

		using FRequestArguments = std::tuple<std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t>;
		using FResponseArguments = std::tuple<EPlayerEquipmentResult, FPlayerWorldSnapshot, std::uint64_t, bool, bool>;
	};

	struct FUnequipPlayerItemV2Rpc final
	{
		static constexpr RpcLib::Protocol::FRpcServiceId kServiceId = kCacheServiceId;
		static constexpr RpcLib::Protocol::FRpcMethodId kMethodId = 22;
		static constexpr const char* kName = "Cache.UnequipPlayerItemV2";
		static constexpr bool kHasRoutingKey = true;
		static constexpr std::size_t kRoutingKeyArgumentIndex = 0;

		using FRequestArguments = std::tuple<std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t>;
		using FResponseArguments = std::tuple<EPlayerEquipmentResult, FPlayerWorldSnapshot, std::uint64_t, bool, bool>;
	};
}
// clang-format on
