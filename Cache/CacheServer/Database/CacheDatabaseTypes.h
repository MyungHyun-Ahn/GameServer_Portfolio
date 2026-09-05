#pragma once

namespace CacheServer::Database
{
	struct SCacheDatabaseConfig
	{
		bool enabled = false;
		Connector::MySql::SMySqlConnectionConfig gamePrimary;
		std::vector<Connector::MySql::SMySqlConnectionConfig> gameReplicas;
		std::uint32_t replicaReconnectCooldownMilliseconds = 60000;
	};

	struct SPlayerCurrencyRow
	{
		std::uint16_t currencyId = 0;
		std::uint64_t amount = 0;
		std::uint64_t version = 0;
	};

	struct SPlayerInventoryItemRow
	{
		std::uint64_t itemInstanceId = 0;
		std::uint32_t itemDataId = 0;
		std::uint32_t quantity = 0;
		std::string itemDataJson;
		bool equipped = false;
		bool tradable = false;
		std::uint64_t version = 0;
	};

	struct SInventoryItemMutationRow : SPlayerInventoryItemRow
	{
		std::uint32_t str = 0;
		std::uint32_t dex = 0;
		std::uint32_t intelligence = 0;
		std::uint32_t luk = 0;
	};

	struct SPlayerEquipmentMutationResult
	{
		SPlayerInventoryItemRow targetItem;
		std::optional<SPlayerInventoryItemRow> previousItem;
	};

	struct SPlayerCharacterRow
	{
		std::uint64_t characterId = 0;
		std::uint64_t userId = 0;
		std::uint32_t characterDataId = 0;
		std::uint32_t level = 0;
		std::uint64_t exp = 0;
		std::uint32_t str = 0;
		std::uint32_t dex = 0;
		std::uint32_t intelligence = 0;
		std::uint32_t luk = 0;
		std::uint32_t unspentStatPoints = 0;
		std::uint64_t progressVersion = 0;
		std::uint64_t statVersion = 0;
	};

	struct SPlayerCacheSnapshot
	{
		std::uint64_t userId = 0;
		std::optional<SPlayerCharacterRow> character;
		std::unordered_map<std::uint16_t, SPlayerCurrencyRow> currencies;
		std::unordered_map<std::uint64_t, SPlayerInventoryItemRow> inventoryItems;
	};

	struct SMailSummary
	{
		std::uint64_t mailId = 0;
		std::uint8_t mailType = 0;
		std::string subject;
		std::uint8_t state = 0;
		std::uint64_t expiresAtUnixMs = 0;
		std::uint64_t createdAtUnixMs = 0;
	};

	struct SMailAttachment
	{
		std::uint64_t attachmentId = 0;
		std::uint8_t attachmentType = 0;
		std::uint64_t itemInstanceId = 0;
		std::uint32_t itemDataId = 0;
		std::uint32_t quantity = 0;
		std::string itemDataJson;
		std::uint16_t currencyId = 0;
		std::uint64_t currencyAmount = 0;
		std::uint8_t state = 0;
	};

	struct SMailDetail
	{
		std::uint64_t mailId = 0;
		std::uint8_t mailType = 0;
		std::string subject;
		std::string body;
		std::uint8_t state = 0;
		std::uint64_t expiresAtUnixMs = 0;
		std::vector<SMailAttachment> attachments;
	};

	struct SMailClaimMutationResult
	{
		std::uint8_t attachmentType = 0;
		std::uint64_t itemInstanceId = 0;
		std::uint32_t itemDataId = 0;
		std::uint32_t quantity = 0;
		std::string itemDataJson;
		std::uint16_t currencyId = 0;
		std::uint64_t currencyAmount = 0;
		std::uint64_t currencyBalance = 0;
		std::uint64_t currencyVersion = 0;
		std::uint8_t mailState = 0;
	};
}
