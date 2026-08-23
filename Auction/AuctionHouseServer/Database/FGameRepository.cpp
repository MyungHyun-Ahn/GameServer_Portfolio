#include "AuctionHouseServerPch.h"

#include "AuctionHouseServer/Database/FGameRepository.h"

#include "Connector/MySql/FMySqlConnection.h"

namespace AuctionHouseServer::Database
{
	using Connector::MySql::FindFirstRows;
	using Connector::MySql::ParseUnsigned;

	FGameRepository::FGameRepository(
		Connector::MySql::FMySqlConnection& connection)
		: m_connection(connection)
	{
	}

	bool FGameRepository::CreateInventoryItem(
		const std::uint64_t userId,
		const std::uint32_t itemDataId,
		const std::uint32_t quantity,
		const std::uint32_t maxStack,
		const std::uint32_t str,
		const std::uint32_t dex,
		const std::uint32_t intelligence,
		const std::uint32_t luk,
		const bool tradable,
		SInventoryItem& outItem,
		std::string& outError)
	{
		std::ostringstream sql;
		sql << "CALL sp_gd_c_inventory_item(" << userId << ',' << itemDataId << ',' << quantity << ',' << maxStack << ",JSON_OBJECT('str',"
			<< str << ",'dex'," << dex << ",'int'," << intelligence << ",'luk'," << luk << ")," << (tradable ? 1 : 0) << ')';
		std::vector<Connector::MySql::SMySqlResultSet> resultSets;
		if (!m_connection.ExecuteQuery(sql.str(), resultSets, outError))
		{
			return false;
		}

		const auto* resultSet = FindFirstRows(resultSets);
		if (resultSet == nullptr || resultSet->rows.size() != 1 || resultSet->rows[0].size() < 7)
		{
			outError = "sp_gd_c_inventory_item did not return one item.";
			return false;
		}

		const auto& row = resultSet->rows[0];
		std::uint8_t equipped = 0;
		std::uint8_t itemTradable = 0;
		if (!ParseUnsigned(row[0], outItem.itemInstanceId) || !ParseUnsigned(row[2], outItem.itemDataId) ||
			!ParseUnsigned(row[3], outItem.quantity) || !ParseUnsigned(row[4], equipped) || !ParseUnsigned(row[5], itemTradable) ||
			!ParseUnsigned(row[6], outItem.version))
		{
			outError = "sp_gd_c_inventory_item returned an invalid value.";
			return false;
		}
		outItem.equipped = equipped != 0;
		outItem.tradable = itemTradable != 0;
		return true;
	}

	bool FGameRepository::GetInventoryItems(
		const std::uint64_t userId,
		const std::uint64_t cursorItemInstanceId,
		const std::uint32_t limit,
		std::vector<SInventoryItem>& outItems,
		std::string& outError)
	{
		outItems.clear();
		std::ostringstream sql;
		sql << "CALL sp_gd_r_inventory_items(" << userId << ',' << cursorItemInstanceId << ',' << limit << ')';
		std::vector<Connector::MySql::SMySqlResultSet> resultSets;
		if (!m_connection.ExecuteQuery(sql.str(), resultSets, outError))
		{
			return false;
		}

		const auto* resultSet = FindFirstRows(resultSets);
		if (resultSet == nullptr)
		{
			return true;
		}
		for (const auto& row : resultSet->rows)
		{
			if (row.size() < 7)
			{
				outError = "sp_gd_r_inventory_items returned an invalid column count.";
				return false;
			}

			SInventoryItem item;
			std::uint8_t equipped = 0;
			std::uint8_t tradable = 0;
			if (!ParseUnsigned(row[0], item.itemInstanceId) || !ParseUnsigned(row[1], item.itemDataId) ||
				!ParseUnsigned(row[2], item.quantity) || !ParseUnsigned(row[3], equipped) || !ParseUnsigned(row[4], tradable) ||
				!row[5].has_value() || !ParseUnsigned(row[6], item.version) || equipped > 1 || tradable > 1)
			{
				outError = "sp_gd_r_inventory_items returned an invalid value.";
				return false;
			}
			item.equipped = equipped != 0;
			item.tradable = tradable != 0;
			item.itemDataJson = *row[5];
			outItems.push_back(std::move(item));
		}
		return true;
	}

	bool FGameRepository::RemoveInventoryItem(
		const std::uint64_t userId,
		const std::uint64_t itemInstanceId,
		const std::uint64_t expectedVersion,
		SInventoryItem& outItem,
		std::string& outError)
	{
		std::ostringstream sql;
		sql << "CALL sp_gd_d_inventory_item(" << userId << ',' << itemInstanceId << ',' << expectedVersion << ')';
		std::vector<Connector::MySql::SMySqlResultSet> resultSets;
		if (!m_connection.ExecuteQuery(sql.str(), resultSets, outError))
		{
			return false;
		}

		const auto* resultSet = FindFirstRows(resultSets);
		if (resultSet == nullptr || resultSet->rows.size() != 1 || resultSet->rows[0].size() < 11)
		{
			outError = "sp_gd_d_inventory_item did not return one item.";
			return false;
		}

		const auto& row = resultSet->rows[0];
		std::uint8_t equipped = 0;
		std::uint8_t tradable = 0;
		if (!ParseUnsigned(row[0], outItem.itemInstanceId) || !ParseUnsigned(row[1], outItem.itemDataId) ||
			!ParseUnsigned(row[2], outItem.quantity) || !row[3].has_value() || !ParseUnsigned(row[4], equipped) ||
			!ParseUnsigned(row[5], tradable) || !ParseUnsigned(row[6], outItem.version) || !ParseUnsigned(row[7], outItem.str) ||
			!ParseUnsigned(row[8], outItem.dex) || !ParseUnsigned(row[9], outItem.intelligence) || !ParseUnsigned(row[10], outItem.luk) ||
			equipped > 1 || tradable > 1)
		{
			outError = "sp_gd_d_inventory_item returned an invalid value.";
			return false;
		}
		outItem.itemDataJson = *row[3];
		outItem.equipped = equipped != 0;
		outItem.tradable = tradable != 0;
		return true;
	}

	bool FGameRepository::CreditCurrency(
		const std::uint64_t userId,
		const std::uint16_t currencyId,
		const std::uint64_t amount,
		const std::uint64_t maxAmount,
		std::uint64_t& outBalance,
		std::string& outError)
	{
		std::ostringstream sql;
		sql << "CALL sp_gd_cu_currency_credit(" << userId << ',' << currencyId << ',' << amount << ',' << maxAmount << ')';
		std::vector<Connector::MySql::SMySqlResultSet> resultSets;
		if (!m_connection.ExecuteQuery(sql.str(), resultSets, outError))
		{
			return false;
		}
		for (const auto& resultSet : resultSets)
		{
			if (resultSet.rows.empty() || resultSet.rows[0].empty() || !resultSet.rows[0][0].has_value())
			{
				continue;
			}
			const std::string& value = *resultSet.rows[0][0];
			const auto parsed = std::from_chars(value.data(), value.data() + value.size(), outBalance);
			if (parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size())
			{
				return true;
			}
		}
		outError = "sp_gd_cu_currency_credit did not return a balance.";
		return false;
	}

	bool FGameRepository::DebitCurrency(
		const std::uint64_t userId,
		const std::uint16_t currencyId,
		const std::uint64_t amount,
		std::uint64_t& outBalance,
		std::string& outError)
	{
		std::ostringstream sql;
		sql << "CALL sp_gd_u_currency_debit(" << userId << ',' << currencyId << ',' << amount << ')';
		std::vector<Connector::MySql::SMySqlResultSet> resultSets;
		if (!m_connection.ExecuteQuery(sql.str(), resultSets, outError))
		{
			return false;
		}
		const auto* resultSet = FindFirstRows(resultSets);
		if (resultSet == nullptr || resultSet->rows.size() != 1 || resultSet->rows[0].empty() ||
			!ParseUnsigned(resultSet->rows[0][0], outBalance))
		{
			outError = "sp_gd_u_currency_debit did not return a balance.";
			return false;
		}
		return true;
	}

	bool FGameRepository::CreateItemMail(
		const std::uint64_t receiverUserId,
		const std::uint64_t itemInstanceId,
		const std::uint32_t itemDataId,
		const std::uint32_t quantity,
		const std::string& itemDataJson,
		std::uint64_t& outMailId,
		std::string& outError)
	{
		std::string escapedItemData;
		if (!m_connection.EscapeString(itemDataJson, escapedItemData, outError))
		{
			return false;
		}
		std::ostringstream sql;
		sql << "CALL sp_gd_c_mail_item(" << receiverUserId << ',' << itemInstanceId << ',' << itemDataId << ',' << quantity << ",'"
			<< escapedItemData << "')";
		std::vector<Connector::MySql::SMySqlResultSet> resultSets;
		if (!m_connection.ExecuteQuery(sql.str(), resultSets, outError))
		{
			return false;
		}
		const auto* resultSet = FindFirstRows(resultSets);
		if (resultSet == nullptr || resultSet->rows.size() != 1 || resultSet->rows[0].size() < 2 ||
			!ParseUnsigned(resultSet->rows[0][0], outMailId))
		{
			outError = "sp_gd_c_mail_item returned an invalid result.";
			return false;
		}
		return true;
	}

	bool FGameRepository::CreateCurrencyMail(
		const std::uint64_t receiverUserId,
		const std::uint16_t currencyId,
		const std::uint64_t amount,
		std::uint64_t& outMailId,
		std::string& outError)
	{
		std::ostringstream sql;
		sql << "CALL sp_gd_c_mail_currency(" << receiverUserId << ',' << currencyId << ',' << amount << ')';
		std::vector<Connector::MySql::SMySqlResultSet> resultSets;
		if (!m_connection.ExecuteQuery(sql.str(), resultSets, outError))
		{
			return false;
		}
		const auto* resultSet = FindFirstRows(resultSets);
		if (resultSet == nullptr || resultSet->rows.size() != 1 || resultSet->rows[0].size() < 2 ||
			!ParseUnsigned(resultSet->rows[0][0], outMailId))
		{
			outError = "sp_gd_c_mail_currency returned an invalid result.";
			return false;
		}
		return true;
	}

	bool FGameRepository::CreateItemReturnMail(
		const std::uint64_t receiverUserId,
		const std::uint64_t itemInstanceId,
		const std::uint32_t itemDataId,
		const std::uint32_t quantity,
		const std::string& itemDataJson,
		std::uint64_t& outMailId,
		std::string& outError)
	{
		std::string escapedItemData;
		if (!m_connection.EscapeString(itemDataJson, escapedItemData, outError))
			return false;
		std::ostringstream sql;
		sql << "CALL sp_gd_c_mail_item_return(" << receiverUserId << ',' << itemInstanceId << ',' << itemDataId << ',' << quantity << ",'"
			<< escapedItemData << "')";
		std::vector<Connector::MySql::SMySqlResultSet> resultSets;
		if (!m_connection.ExecuteQuery(sql.str(), resultSets, outError))
			return false;
		const auto* resultSet = FindFirstRows(resultSets);
		if (resultSet == nullptr || resultSet->rows.size() != 1 || resultSet->rows[0].size() < 2 ||
			!ParseUnsigned(resultSet->rows[0][0], outMailId))
		{
			outError = "sp_gd_c_mail_item_return returned an invalid result.";
			return false;
		}
		return true;
	}

	bool FGameRepository::CreateExpiredItemReturnMail(
		const std::uint64_t receiverUserId,
		const std::uint64_t itemInstanceId,
		const std::uint32_t itemDataId,
		const std::uint32_t quantity,
		const std::string& itemDataJson,
		std::uint64_t& outMailId,
		std::string& outError)
	{
		std::string escapedItemData;
		if (!m_connection.EscapeString(itemDataJson, escapedItemData, outError))
			return false;
		std::ostringstream sql;
		sql << "CALL sp_gd_c_mail_item_expired(" << receiverUserId << ',' << itemInstanceId << ',' << itemDataId << ',' << quantity << ",'"
			<< escapedItemData << "')";
		std::vector<Connector::MySql::SMySqlResultSet> resultSets;
		if (!m_connection.ExecuteQuery(sql.str(), resultSets, outError))
			return false;
		const auto* resultSet = FindFirstRows(resultSets);
		if (resultSet == nullptr || resultSet->rows.size() != 1 || resultSet->rows[0].size() < 2 ||
			!ParseUnsigned(resultSet->rows[0][0], outMailId))
		{
			outError = "sp_gd_c_mail_item_expired returned an invalid result.";
			return false;
		}
		return true;
	}

	bool FGameRepository::GetMailList(
		const std::uint64_t receiverUserId,
		const std::uint64_t cursorMailId,
		const std::uint32_t limit,
		std::vector<SMailSummary>& outMails,
		std::string& outError)
	{
		outMails.clear();
		std::ostringstream sql;
		sql << "CALL sp_gd_r_mail_list(" << receiverUserId << ',' << cursorMailId << ',' << limit << ')';
		std::vector<Connector::MySql::SMySqlResultSet> resultSets;
		if (!m_connection.ExecuteQuery(sql.str(), resultSets, outError))
		{
			return false;
		}
		const auto* resultSet = FindFirstRows(resultSets);
		if (resultSet == nullptr)
		{
			return true;
		}
		for (const auto& row : resultSet->rows)
		{
			SMailSummary mail;
			if (row.size() < 6 || !row[2].has_value() || !ParseUnsigned(row[0], mail.mailId) || !ParseUnsigned(row[1], mail.mailType) ||
				!ParseUnsigned(row[3], mail.state) || !ParseUnsigned(row[4], mail.expiresAtUnixMs) ||
				!ParseUnsigned(row[5], mail.createdAtUnixMs))
			{
				outError = "sp_gd_r_mail_list returned an invalid value.";
				return false;
			}
			mail.subject = *row[2];
			outMails.push_back(std::move(mail));
		}
		return true;
	}

	bool FGameRepository::GetMailDetail(
		const std::uint64_t receiverUserId,
		const std::uint64_t mailId,
		SMailDetail& outMail,
		bool& outFound,
		std::string& outError)
	{
		outFound = false;
		outMail = {};
		std::ostringstream sql;
		sql << "CALL sp_gd_r_mail_detail(" << receiverUserId << ',' << mailId << ')';
		std::vector<Connector::MySql::SMySqlResultSet> resultSets;
		if (!m_connection.ExecuteQuery(sql.str(), resultSets, outError))
		{
			return false;
		}
		const auto* resultSet = FindFirstRows(resultSets);
		if (resultSet == nullptr)
		{
			return true;
		}
		for (const auto& row : resultSet->rows)
		{
			if (row.size() < 15 || !row[2].has_value() || !row[3].has_value())
			{
				outError = "sp_gd_r_mail_detail returned an invalid column count.";
				return false;
			}
			if (!outFound)
			{
				if (!ParseUnsigned(row[0], outMail.mailId) || !ParseUnsigned(row[1], outMail.mailType) ||
					!ParseUnsigned(row[4], outMail.state) || !ParseUnsigned(row[5], outMail.expiresAtUnixMs))
				{
					outError = "sp_gd_r_mail_detail returned invalid mail data.";
					return false;
				}
				outMail.subject = *row[2];
				outMail.body = *row[3];
				outFound = true;
			}
			if (!row[6].has_value())
			{
				continue;
			}
			SMailAttachment attachment;
			if (!ParseUnsigned(row[6], attachment.attachmentId) || !ParseUnsigned(row[7], attachment.attachmentType) ||
				!ParseUnsigned(row[8], attachment.itemInstanceId) || !ParseUnsigned(row[9], attachment.itemDataId) ||
				!ParseUnsigned(row[10], attachment.quantity) || !row[11].has_value() || !ParseUnsigned(row[12], attachment.currencyId) ||
				!ParseUnsigned(row[13], attachment.currencyAmount) || !ParseUnsigned(row[14], attachment.state))
			{
				outError = "sp_gd_r_mail_detail returned invalid attachment data.";
				return false;
			}
			attachment.itemDataJson = *row[11];
			outMail.attachments.push_back(std::move(attachment));
		}
		return true;
	}

	bool FGameRepository::ClaimMailAttachment(
		const std::uint64_t receiverUserId,
		const std::uint64_t mailId,
		const std::uint64_t attachmentId,
		const std::uint32_t maxInventorySlots,
		const std::uint64_t maxCurrencyAmount,
		SMailClaimResult& outResult,
		std::string& outError)
	{
		std::ostringstream sql;
		sql << "CALL sp_gd_cu_mail_claim_attachment(" << receiverUserId << ',' << mailId << ',' << attachmentId << ',' << maxInventorySlots
			<< ',' << maxCurrencyAmount << ')';
		std::vector<Connector::MySql::SMySqlResultSet> resultSets;
		if (!m_connection.ExecuteQuery(sql.str(), resultSets, outError))
		{
			return false;
		}
		const auto* resultSet = FindFirstRows(resultSets);
		if (resultSet == nullptr || resultSet->rows.size() != 1 || resultSet->rows[0].size() < 8)
		{
			outError = "sp_gd_cu_mail_claim_attachment returned an invalid result.";
			return false;
		}
		const auto& row = resultSet->rows[0];
		if (!ParseUnsigned(row[0], outResult.attachmentType) || !ParseUnsigned(row[1], outResult.itemInstanceId) ||
			!ParseUnsigned(row[2], outResult.itemDataId) || !ParseUnsigned(row[3], outResult.quantity) ||
			!ParseUnsigned(row[4], outResult.currencyId) || !ParseUnsigned(row[5], outResult.currencyAmount) ||
			!ParseUnsigned(row[6], outResult.currencyBalance) || !ParseUnsigned(row[7], outResult.mailState))
		{
			outError = "sp_gd_cu_mail_claim_attachment returned an invalid value.";
			return false;
		}
		return true;
	}
}
