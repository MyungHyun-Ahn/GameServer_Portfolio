#pragma once

namespace Connector::MySql
{
	class FMySqlConnection;
}

namespace CacheServer::Database
{
	struct SPlayerCacheSnapshot;

	class FPlayerCacheRepository final
	{
	public:
		explicit FPlayerCacheRepository(Connector::MySql::FMySqlConnection& connection);

		bool LoadPlayerSnapshot(std::uint64_t userId, SPlayerCacheSnapshot& outSnapshot, std::string& outError);
		bool LoadPlayerCharacter(std::uint64_t userId, SPlayerCharacterRow& outCharacter, bool& outFound, std::string& outError);
		bool CreatePlayerCharacter(std::uint64_t userId,
			std::uint32_t characterDataId,
			std::uint32_t initialLevel,
			std::uint64_t initialExp,
			std::uint32_t initialStr,
			std::uint32_t initialDex,
			std::uint32_t initialInt,
			std::uint32_t initialLuk,
			std::uint32_t initialUnspentStatPoints,
			SPlayerCharacterRow& outCharacter,
			std::string& outError);
		bool UpdatePlayerExperience(std::uint64_t userId,
			std::uint64_t expectedProgressVersion,
			std::uint32_t newLevel,
			std::uint64_t newExp,
			std::uint32_t statPointReward,
			SPlayerCharacterRow& outCharacter,
			std::string& outError);
		bool AllocatePlayerStats(std::uint64_t userId,
			std::uint64_t expectedStatVersion,
			std::uint32_t addStr,
			std::uint32_t addDex,
			std::uint32_t addInt,
			std::uint32_t addLuk,
			SPlayerCharacterRow& outCharacter,
			std::string& outError);
		bool EquipPlayerItem(std::uint64_t userId,
			std::uint64_t targetItemInstanceId,
			std::uint64_t expectedTargetVersion,
			std::uint64_t previousItemInstanceId,
			std::uint64_t expectedPreviousVersion,
			SPlayerEquipmentMutationResult& outResult,
			std::string& outError);
		bool UnequipPlayerItem(std::uint64_t userId,
			std::uint64_t itemInstanceId,
			std::uint64_t expectedItemVersion,
			SPlayerInventoryItemRow& outItem,
			std::string& outError);
		bool GetMailList(std::uint64_t userId,
			std::uint64_t cursorMailId,
			std::uint32_t limit,
			std::vector<SMailSummary>& outMails,
			std::string& outError);
		bool GetMailDetail(std::uint64_t userId, std::uint64_t mailId, SMailDetail& outMail, bool& outFound, std::string& outError);
		bool CreditCurrency(std::uint64_t userId,
			std::uint16_t currencyId,
			std::uint64_t amount,
			std::uint64_t maxAmount,
			SPlayerCurrencyRow& outCurrency,
			std::string& outError);
		bool DebitCurrency(std::uint64_t userId,
			std::uint16_t currencyId,
			std::uint64_t amount,
			SPlayerCurrencyRow& outCurrency,
			std::string& outError);
		bool CreateInventoryItem(std::uint64_t userId,
			std::uint32_t itemDataId,
			std::uint32_t quantity,
			std::uint32_t maxStack,
			std::uint32_t maxInventorySlots,
			std::uint32_t str,
			std::uint32_t dex,
			std::uint32_t intelligence,
			std::uint32_t luk,
			bool tradable,
			SInventoryItemMutationRow& outItem,
			std::string& outError);
		bool RemoveInventoryItem(std::uint64_t userId,
			std::uint64_t itemInstanceId,
			std::uint64_t expectedVersion,
			SInventoryItemMutationRow& outItem,
			std::string& outError);
		bool ClaimMailAttachment(std::uint64_t userId,
			std::uint64_t mailId,
			std::uint64_t attachmentId,
			std::uint32_t maxInventorySlots,
			bool itemTradable,
			std::uint64_t maxCurrencyAmount,
			SMailClaimMutationResult& outResult,
			std::string& outError);
		bool CreateItemMail(std::uint64_t receiverUserId,
			std::uint64_t itemInstanceId,
			std::uint32_t itemDataId,
			std::uint32_t quantity,
			const std::string& itemDataJson,
			std::uint32_t mailType,
			const std::string& subject,
			const std::string& body,
			std::uint32_t expirationSeconds,
			std::uint64_t& outMailId,
			std::string& outError);
		bool CreateCurrencyMail(std::uint64_t receiverUserId,
			std::uint16_t currencyId,
			std::uint64_t amount,
			std::uint32_t mailType,
			const std::string& subject,
			const std::string& body,
			std::uint32_t expirationSeconds,
			std::uint64_t& outMailId,
			std::string& outError);
		bool CreateItemReturnMail(std::uint64_t receiverUserId,
			std::uint64_t itemInstanceId,
			std::uint32_t itemDataId,
			std::uint32_t quantity,
			const std::string& itemDataJson,
			std::uint32_t mailType,
			const std::string& subject,
			const std::string& body,
			std::uint32_t expirationSeconds,
			std::uint64_t& outMailId,
			std::string& outError);
		bool CreateExpiredItemReturnMail(std::uint64_t receiverUserId,
			std::uint64_t itemInstanceId,
			std::uint32_t itemDataId,
			std::uint32_t quantity,
			const std::string& itemDataJson,
			std::uint32_t mailType,
			const std::string& subject,
			const std::string& body,
			std::uint32_t expirationSeconds,
			std::uint64_t& outMailId,
			std::string& outError);

	private:
		Connector::MySql::FMySqlConnection& m_connection;
	};
}
