#pragma once

namespace Connector::MySql
{
	class FMySqlConnection;
}

namespace AuctionHouseServer::Database
{
	class FGameRepository final
	{
	public:
		explicit FGameRepository(Connector::MySql::FMySqlConnection& connection);

		bool CreateInventoryItem(std::uint64_t userId,
			std::uint32_t itemDataId,
			std::uint32_t quantity,
			std::uint32_t maxStack,
			std::uint32_t str,
			std::uint32_t dex,
			std::uint32_t intelligence,
			std::uint32_t luk,
			bool tradable,
			SInventoryItem& outItem,
			std::string& outError);

		bool GetInventoryItems(std::uint64_t userId,
			std::uint64_t cursorItemInstanceId,
			std::uint32_t limit,
			std::vector<SInventoryItem>& outItems,
			std::string& outError);

		bool RemoveInventoryItem(std::uint64_t userId,
			std::uint64_t itemInstanceId,
			std::uint64_t expectedVersion,
			SInventoryItem& outItem,
			std::string& outError);

		bool CreditCurrency(std::uint64_t userId,
			std::uint16_t currencyId,
			std::uint64_t amount,
			std::uint64_t maxAmount,
			std::uint64_t& outBalance,
			std::string& outError);
		bool DebitCurrency(std::uint64_t userId,
			std::uint16_t currencyId,
			std::uint64_t amount,
			std::uint64_t& outBalance,
			std::string& outError);
		bool CreateItemMail(std::uint64_t receiverUserId,
			std::uint64_t itemInstanceId,
			std::uint32_t itemDataId,
			std::uint32_t quantity,
			const std::string& itemDataJson,
			std::uint64_t& outMailId,
			std::string& outError);
		bool CreateCurrencyMail(std::uint64_t receiverUserId,
			std::uint16_t currencyId,
			std::uint64_t amount,
			std::uint64_t& outMailId,
			std::string& outError);
		bool CreateItemReturnMail(std::uint64_t receiverUserId,
			std::uint64_t itemInstanceId,
			std::uint32_t itemDataId,
			std::uint32_t quantity,
			const std::string& itemDataJson,
			std::uint64_t& outMailId,
			std::string& outError);
		bool CreateExpiredItemReturnMail(std::uint64_t receiverUserId,
			std::uint64_t itemInstanceId,
			std::uint32_t itemDataId,
			std::uint32_t quantity,
			const std::string& itemDataJson,
			std::uint64_t& outMailId,
			std::string& outError);
		bool GetMailList(std::uint64_t receiverUserId,
			std::uint64_t cursorMailId,
			std::uint32_t limit,
			std::vector<SMailSummary>& outMails,
			std::string& outError);
		bool GetMailDetail(std::uint64_t receiverUserId, std::uint64_t mailId, SMailDetail& outMail, bool& outFound, std::string& outError);
		bool ClaimMailAttachment(std::uint64_t receiverUserId,
			std::uint64_t mailId,
			std::uint64_t attachmentId,
			std::uint32_t maxInventorySlots,
			std::uint64_t maxCurrencyAmount,
			SMailClaimResult& outResult,
			std::string& outError);

	private:
		Connector::MySql::FMySqlConnection& m_connection;
	};
}
