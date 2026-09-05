#include "CacheServerPch.h"

#include "CacheServer/Database/FPlayerCacheRepository.h"

#include "Connector/MySql/FMySqlConnection.h"
#include "Connector/MySql/MySqlResultReader.h"

namespace CacheServer::Database
{
	using Connector::MySql::FindFirstRows;
	using Connector::MySql::ParseUnsigned;

	namespace
	{
		template <std::size_t TSize>
		bool HasExactColumns(
			const Connector::MySql::SMySqlResultSet& resultSet,
			const std::array<std::string_view, TSize>& expectedColumns) noexcept
		{
			if (resultSet.columnNames.size() != expectedColumns.size())
			{
				return false;
			}

			for (std::size_t index = 0; index < expectedColumns.size(); ++index)
			{
				if (resultSet.columnNames[index] != expectedColumns[index])
				{
					return false;
				}
			}
			return true;
		}

		bool ParseCurrencies(
			const Connector::MySql::SMySqlResultSet& resultSet,
			SPlayerCacheSnapshot& snapshot,
			std::string& outError)
		{
			constexpr std::array<std::string_view, 3> kExpectedColumns = {"currency_id", "amount", "version"};
			if (!HasExactColumns(resultSet, kExpectedColumns))
			{
				outError = "sp_gd_r_player_cache returned an invalid currency result schema.";
				return false;
			}

			for (const auto& row : resultSet.rows)
			{
				SPlayerCurrencyRow currency;
				if (row.size() != kExpectedColumns.size() || !Connector::MySql::ParseUnsigned(row[0], currency.currencyId) ||
					!Connector::MySql::ParseUnsigned(row[1], currency.amount) ||
					!Connector::MySql::ParseUnsigned(row[2], currency.version) || currency.currencyId == 0 || currency.version == 0)
				{
					outError = "sp_gd_r_player_cache returned an invalid currency value.";
					return false;
				}

				const std::uint16_t currencyId = currency.currencyId;
				const bool inserted = snapshot.currencies.emplace(currencyId, std::move(currency)).second;
				if (!inserted)
				{
					outError = "sp_gd_r_player_cache returned a duplicate currency_id.";
					return false;
				}
			}
			return true;
		}

		bool ParseInventoryItems(
			const Connector::MySql::SMySqlResultSet& resultSet,
			SPlayerCacheSnapshot& snapshot,
			std::string& outError)
		{
			constexpr std::array<std::string_view, 7> kExpectedColumns = {
				"item_instance_id", "item_data_id", "quantity", "item_data_json", "is_equipped", "is_tradable", "version"};
			if (!HasExactColumns(resultSet, kExpectedColumns))
			{
				outError = "sp_gd_r_player_cache returned an invalid inventory result schema.";
				return false;
			}

			for (const auto& row : resultSet.rows)
			{
				SPlayerInventoryItemRow item;
				std::uint8_t equipped = 0;
				std::uint8_t tradable = 0;
				if (row.size() != kExpectedColumns.size() || !Connector::MySql::ParseUnsigned(row[0], item.itemInstanceId) ||
					!Connector::MySql::ParseUnsigned(row[1], item.itemDataId) || !Connector::MySql::ParseUnsigned(row[2], item.quantity) ||
					!row[3].has_value() || row[3]->empty() || !Connector::MySql::ParseUnsigned(row[4], equipped) ||
					!Connector::MySql::ParseUnsigned(row[5], tradable) || !Connector::MySql::ParseUnsigned(row[6], item.version) ||
					item.itemInstanceId == 0 || item.itemDataId == 0 || item.quantity == 0 || equipped > 1 || tradable > 1 ||
					item.version == 0)
				{
					outError = "sp_gd_r_player_cache returned an invalid inventory value.";
					return false;
				}

				item.itemDataJson = *row[3];
				item.equipped = equipped != 0;
				item.tradable = tradable != 0;
				const std::uint64_t itemInstanceId = item.itemInstanceId;
				const bool inserted = snapshot.inventoryItems.emplace(itemInstanceId, std::move(item)).second;
				if (!inserted)
				{
					outError = "sp_gd_r_player_cache returned a duplicate item_instance_id.";
					return false;
				}
			}
			return true;
		}

		bool ParsePlayerCharacterResult(
			const std::vector<Connector::MySql::SMySqlResultSet>& resultSets,
			const std::string_view procedureName,
			SPlayerCharacterRow& outCharacter,
			bool& outFound,
			std::string& outError)
		{
			outCharacter = {};
			outFound = false;
			constexpr std::array<std::string_view, 14> kExpectedColumns = {"character_id",
				"user_id",
				"character_data_id",
				"level",
				"exp",
				"stat_str",
				"stat_dex",
				"stat_int",
				"stat_luk",
				"unspent_stat_points",
				"progress_version",
				"stat_version",
				"created_at",
				"updated_at"};
			if (resultSets.size() != 1 || !HasExactColumns(resultSets.front(), kExpectedColumns) || resultSets.front().rows.size() > 1)
			{
				outError = std::format("{} returned an invalid result schema.", procedureName);
				return false;
			}
			if (resultSets.front().rows.empty())
			{
				return true;
			}

			const auto& row = resultSets.front().rows.front();
			if (row.size() != kExpectedColumns.size() || !ParseUnsigned(row[0], outCharacter.characterId) ||
				!ParseUnsigned(row[1], outCharacter.userId) || !ParseUnsigned(row[2], outCharacter.characterDataId) ||
				!ParseUnsigned(row[3], outCharacter.level) || !ParseUnsigned(row[4], outCharacter.exp) ||
				!ParseUnsigned(row[5], outCharacter.str) || !ParseUnsigned(row[6], outCharacter.dex) ||
				!ParseUnsigned(row[7], outCharacter.intelligence) || !ParseUnsigned(row[8], outCharacter.luk) ||
				!ParseUnsigned(row[9], outCharacter.unspentStatPoints) || !ParseUnsigned(row[10], outCharacter.progressVersion) ||
				!ParseUnsigned(row[11], outCharacter.statVersion) || outCharacter.characterId == 0 || outCharacter.userId == 0 ||
				outCharacter.characterDataId == 0 || outCharacter.level == 0 || outCharacter.progressVersion == 0 ||
				outCharacter.statVersion == 0)
			{
				outError = std::format("{} returned an invalid character value.", procedureName);
				return false;
			}

			outFound = true;
			return true;
		}

		bool ParseMailCreationResult(
			const std::vector<Connector::MySql::SMySqlResultSet>& resultSets,
			const std::string_view procedureName,
			std::uint64_t& outMailId,
			std::string& outError)
		{
			const Connector::MySql::SMySqlResultSet* resultSet = FindFirstRows(resultSets);
			if (resultSet == nullptr || resultSet->rows.size() != 1 || resultSet->rows[0].size() < 2 ||
				!ParseUnsigned(resultSet->rows[0][0], outMailId) || outMailId == 0)
			{
				outError = std::format("{} returned an invalid result.", procedureName);
				return false;
			}
			return true;
		}

		bool EscapeMailText(
			Connector::MySql::FMySqlConnection& connection,
			const std::string& subject,
			const std::string& body,
			std::string& outEscapedSubject,
			std::string& outEscapedBody,
			std::string& outError)
		{
			return connection.EscapeString(subject, outEscapedSubject, outError) && connection.EscapeString(body, outEscapedBody, outError);
		}
	}

	FPlayerCacheRepository::FPlayerCacheRepository(
		Connector::MySql::FMySqlConnection& connection)
		: m_connection(connection)
	{
	}

	bool FPlayerCacheRepository::LoadPlayerSnapshot(
		const std::uint64_t userId,
		SPlayerCacheSnapshot& outSnapshot,
		std::string& outError)
	{
		if (userId == 0)
		{
			outError = "userId must not be zero.";
			return false;
		}

		std::vector<Connector::MySql::SMySqlResultSet> resultSets;
		const std::string sql = "CALL sp_gd_r_player_cache(" + std::to_string(userId) + ')';
		if (!m_connection.ExecuteQuery(sql, resultSets, outError))
		{
			return false;
		}

		if (resultSets.size() != 2)
		{
			outError = "sp_gd_r_player_cache did not return exactly two result sets.";
			return false;
		}

		SPlayerCacheSnapshot snapshot;
		snapshot.userId = userId;
		if (!ParseCurrencies(resultSets[0], snapshot, outError) || !ParseInventoryItems(resultSets[1], snapshot, outError))
		{
			return false;
		}

		SPlayerCharacterRow character;
		bool characterFound = false;
		if (!LoadPlayerCharacter(userId, character, characterFound, outError))
		{
			return false;
		}
		if (characterFound)
		{
			snapshot.character = character;
		}

		outSnapshot = std::move(snapshot);
		return true;
	}

	bool FPlayerCacheRepository::LoadPlayerCharacter(
		const std::uint64_t userId,
		SPlayerCharacterRow& outCharacter,
		bool& outFound,
		std::string& outError)
	{
		if (userId == 0)
		{
			outError = "LoadPlayerCharacter received an invalid userId.";
			return false;
		}

		std::vector<Connector::MySql::SMySqlResultSet> resultSets;
		if (!m_connection.ExecuteQuery("CALL sp_gd_r_player_character(" + std::to_string(userId) + ')', resultSets, outError))
		{
			return false;
		}
		return ParsePlayerCharacterResult(resultSets, "sp_gd_r_player_character", outCharacter, outFound, outError);
	}

	bool FPlayerCacheRepository::CreatePlayerCharacter(
		const std::uint64_t userId,
		const std::uint32_t characterDataId,
		const std::uint32_t initialLevel,
		const std::uint64_t initialExp,
		const std::uint32_t initialStr,
		const std::uint32_t initialDex,
		const std::uint32_t initialInt,
		const std::uint32_t initialLuk,
		const std::uint32_t initialUnspentStatPoints,
		SPlayerCharacterRow& outCharacter,
		std::string& outError)
	{
		const std::string sql = "CALL sp_gd_c_player_character(" + std::to_string(userId) + ',' + std::to_string(characterDataId) + ',' +
								std::to_string(initialLevel) + ',' + std::to_string(initialExp) + ',' + std::to_string(initialStr) + ',' +
								std::to_string(initialDex) + ',' + std::to_string(initialInt) + ',' + std::to_string(initialLuk) + ',' +
								std::to_string(initialUnspentStatPoints) + ')';
		std::vector<Connector::MySql::SMySqlResultSet> resultSets;
		if (!m_connection.ExecuteQuery(sql, resultSets, outError))
		{
			return false;
		}
		bool found = false;
		return ParsePlayerCharacterResult(resultSets, "sp_gd_c_player_character", outCharacter, found, outError) && found;
	}

	bool FPlayerCacheRepository::UpdatePlayerExperience(
		const std::uint64_t userId,
		const std::uint64_t expectedProgressVersion,
		const std::uint32_t newLevel,
		const std::uint64_t newExp,
		const std::uint32_t statPointReward,
		SPlayerCharacterRow& outCharacter,
		std::string& outError)
	{
		const std::string sql = "CALL sp_gd_u_player_experience(" + std::to_string(userId) + ',' + std::to_string(expectedProgressVersion) +
								',' + std::to_string(newLevel) + ',' + std::to_string(newExp) + ',' + std::to_string(statPointReward) + ')';
		std::vector<Connector::MySql::SMySqlResultSet> resultSets;
		if (!m_connection.ExecuteQuery(sql, resultSets, outError))
		{
			return false;
		}
		bool found = false;
		return ParsePlayerCharacterResult(resultSets, "sp_gd_u_player_experience", outCharacter, found, outError) && found;
	}

	bool FPlayerCacheRepository::AllocatePlayerStats(
		const std::uint64_t userId,
		const std::uint64_t expectedStatVersion,
		const std::uint32_t addStr,
		const std::uint32_t addDex,
		const std::uint32_t addInt,
		const std::uint32_t addLuk,
		SPlayerCharacterRow& outCharacter,
		std::string& outError)
	{
		const std::string sql = "CALL sp_gd_u_player_stat_allocation(" + std::to_string(userId) + ',' +
								std::to_string(expectedStatVersion) + ',' + std::to_string(addStr) + ',' + std::to_string(addDex) + ',' +
								std::to_string(addInt) + ',' + std::to_string(addLuk) + ')';
		std::vector<Connector::MySql::SMySqlResultSet> resultSets;
		if (!m_connection.ExecuteQuery(sql, resultSets, outError))
		{
			return false;
		}
		bool found = false;
		return ParsePlayerCharacterResult(resultSets, "sp_gd_u_player_stat_allocation", outCharacter, found, outError) && found;
	}

	bool FPlayerCacheRepository::EquipPlayerItem(
		const std::uint64_t userId,
		const std::uint64_t targetItemInstanceId,
		const std::uint64_t expectedTargetVersion,
		const std::uint64_t previousItemInstanceId,
		const std::uint64_t expectedPreviousVersion,
		SPlayerEquipmentMutationResult& outResult,
		std::string& outError)
	{
		outResult = {};
		if (userId == 0 || targetItemInstanceId == 0 || expectedTargetVersion == 0 ||
			(previousItemInstanceId == 0) != (expectedPreviousVersion == 0) || previousItemInstanceId == targetItemInstanceId)
		{
			outError = "EquipPlayerItem received an invalid argument.";
			return false;
		}

		const std::string sql = "CALL sp_gd_u_player_item_equip(" + std::to_string(userId) + ',' + std::to_string(targetItemInstanceId) +
								',' + std::to_string(expectedTargetVersion) + ',' + std::to_string(previousItemInstanceId) + ',' +
								std::to_string(expectedPreviousVersion) + ')';
		std::vector<Connector::MySql::SMySqlResultSet> resultSets;
		if (!m_connection.ExecuteQuery(sql, resultSets, outError))
		{
			return false;
		}

		constexpr std::array<std::string_view, 7> kExpectedColumns = {
			"item_instance_id", "item_data_id", "quantity", "item_data_json", "is_equipped", "is_tradable", "version"};
		const Connector::MySql::SMySqlResultSet* resultSet = FindFirstRows(resultSets);
		const std::size_t expectedRowCount = previousItemInstanceId == 0 ? 1 : 2;
		if (resultSet == nullptr || !HasExactColumns(*resultSet, kExpectedColumns) || resultSet->rows.size() != expectedRowCount)
		{
			outError = "sp_gd_u_player_item_equip returned an invalid result schema.";
			return false;
		}

		for (const auto& row : resultSet->rows)
		{
			SPlayerInventoryItemRow item;
			std::uint8_t equipped = 0;
			std::uint8_t tradable = 0;
			if (row.size() != kExpectedColumns.size() || !ParseUnsigned(row[0], item.itemInstanceId) ||
				!ParseUnsigned(row[1], item.itemDataId) || !ParseUnsigned(row[2], item.quantity) || !row[3].has_value() ||
				row[3]->empty() || !ParseUnsigned(row[4], equipped) || !ParseUnsigned(row[5], tradable) ||
				!ParseUnsigned(row[6], item.version) || item.itemInstanceId == 0 || item.itemDataId == 0 || item.quantity == 0 ||
				equipped > 1 || tradable > 1 || item.version == 0)
			{
				outError = "sp_gd_u_player_item_equip returned an invalid item value.";
				return false;
			}

			item.itemDataJson = *row[3];
			item.equipped = equipped != 0;
			item.tradable = tradable != 0;
			if (item.itemInstanceId == targetItemInstanceId && item.equipped && item.version == expectedTargetVersion + 1)
			{
				outResult.targetItem = std::move(item);
			}
			else if (item.itemInstanceId == previousItemInstanceId && !item.equipped && item.version == expectedPreviousVersion + 1)
			{
				outResult.previousItem = std::move(item);
			}
			else
			{
				outError = "sp_gd_u_player_item_equip returned an unexpected changed item.";
				return false;
			}
		}

		if (outResult.targetItem.itemInstanceId == 0 || (previousItemInstanceId != 0) != outResult.previousItem.has_value())
		{
			outError = "sp_gd_u_player_item_equip omitted a changed item.";
			return false;
		}
		return true;
	}

	bool FPlayerCacheRepository::UnequipPlayerItem(
		const std::uint64_t userId,
		const std::uint64_t itemInstanceId,
		const std::uint64_t expectedItemVersion,
		SPlayerInventoryItemRow& outItem,
		std::string& outError)
	{
		outItem = {};
		if (userId == 0 || itemInstanceId == 0 || expectedItemVersion == 0)
		{
			outError = "UnequipPlayerItem received an invalid argument.";
			return false;
		}

		const std::string sql = "CALL sp_gd_u_player_item_unequip(" + std::to_string(userId) + ',' + std::to_string(itemInstanceId) + ',' +
								std::to_string(expectedItemVersion) + ')';
		std::vector<Connector::MySql::SMySqlResultSet> resultSets;
		if (!m_connection.ExecuteQuery(sql, resultSets, outError))
		{
			return false;
		}

		constexpr std::array<std::string_view, 7> kExpectedColumns = {
			"item_instance_id", "item_data_id", "quantity", "item_data_json", "is_equipped", "is_tradable", "version"};
		const Connector::MySql::SMySqlResultSet* resultSet = FindFirstRows(resultSets);
		if (resultSet == nullptr || !HasExactColumns(*resultSet, kExpectedColumns) || resultSet->rows.size() != 1)
		{
			outError = "sp_gd_u_player_item_unequip returned an invalid result schema.";
			return false;
		}

		const auto& row = resultSet->rows.front();
		std::uint8_t equipped = 0;
		std::uint8_t tradable = 0;
		if (row.size() != kExpectedColumns.size() || !ParseUnsigned(row[0], outItem.itemInstanceId) ||
			!ParseUnsigned(row[1], outItem.itemDataId) || !ParseUnsigned(row[2], outItem.quantity) || !row[3].has_value() ||
			row[3]->empty() || !ParseUnsigned(row[4], equipped) || !ParseUnsigned(row[5], tradable) ||
			!ParseUnsigned(row[6], outItem.version) || outItem.itemInstanceId != itemInstanceId || outItem.itemDataId == 0 ||
			outItem.quantity == 0 || equipped != 0 || tradable > 1 || outItem.version != expectedItemVersion + 1)
		{
			outError = "sp_gd_u_player_item_unequip returned an invalid item value.";
			return false;
		}

		outItem.itemDataJson = *row[3];
		outItem.equipped = false;
		outItem.tradable = tradable != 0;
		return true;
	}

	bool FPlayerCacheRepository::GetMailList(
		const std::uint64_t userId,
		const std::uint64_t cursorMailId,
		const std::uint32_t limit,
		std::vector<SMailSummary>& outMails,
		std::string& outError)
	{
		outMails.clear();
		if (userId == 0 || limit == 0)
		{
			outError = "GetMailList received an invalid argument.";
			return false;
		}

		const std::string sql =
			"CALL sp_gd_r_mail_list(" + std::to_string(userId) + ',' + std::to_string(cursorMailId) + ',' + std::to_string(limit) + ')';
		std::vector<Connector::MySql::SMySqlResultSet> resultSets;
		if (!m_connection.ExecuteQuery(sql, resultSets, outError))
		{
			return false;
		}

		const Connector::MySql::SMySqlResultSet* resultSet = FindFirstRows(resultSets);
		if (resultSet == nullptr)
		{
			return true;
		}

		outMails.reserve(resultSet->rows.size());
		for (const auto& row : resultSet->rows)
		{
			SMailSummary mail;
			if (row.size() < 6 || !row[2].has_value() || !ParseUnsigned(row[0], mail.mailId) || !ParseUnsigned(row[1], mail.mailType) ||
				!ParseUnsigned(row[3], mail.state) || !ParseUnsigned(row[4], mail.expiresAtUnixMs) ||
				!ParseUnsigned(row[5], mail.createdAtUnixMs) || mail.mailId == 0)
			{
				outError = "sp_gd_r_mail_list returned an invalid value.";
				return false;
			}

			mail.subject = *row[2];
			outMails.push_back(std::move(mail));
		}
		return true;
	}

	bool FPlayerCacheRepository::GetMailDetail(
		const std::uint64_t userId,
		const std::uint64_t mailId,
		SMailDetail& outMail,
		bool& outFound,
		std::string& outError)
	{
		outMail = {};
		outFound = false;
		if (userId == 0 || mailId == 0)
		{
			outError = "GetMailDetail received an invalid argument.";
			return false;
		}

		const std::string sql = "CALL sp_gd_r_mail_detail(" + std::to_string(userId) + ',' + std::to_string(mailId) + ')';
		std::vector<Connector::MySql::SMySqlResultSet> resultSets;
		if (!m_connection.ExecuteQuery(sql, resultSets, outError))
		{
			return false;
		}

		const Connector::MySql::SMySqlResultSet* resultSet = FindFirstRows(resultSets);
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
					!ParseUnsigned(row[4], outMail.state) || !ParseUnsigned(row[5], outMail.expiresAtUnixMs) || outMail.mailId == 0)
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
				!ParseUnsigned(row[13], attachment.currencyAmount) || !ParseUnsigned(row[14], attachment.state) ||
				attachment.attachmentId == 0)
			{
				outError = "sp_gd_r_mail_detail returned invalid attachment data.";
				return false;
			}

			attachment.itemDataJson = *row[11];
			outMail.attachments.push_back(std::move(attachment));
		}
		return true;
	}

	bool FPlayerCacheRepository::CreditCurrency(
		const std::uint64_t userId,
		const std::uint16_t currencyId,
		const std::uint64_t amount,
		const std::uint64_t maxAmount,
		SPlayerCurrencyRow& outCurrency,
		std::string& outError)
	{
		outCurrency = {};
		const std::string sql = "CALL sp_gd_cu_currency_credit(" + std::to_string(userId) + ',' + std::to_string(currencyId) + ',' +
								std::to_string(amount) + ',' + std::to_string(maxAmount) + ')';
		std::vector<Connector::MySql::SMySqlResultSet> resultSets;
		if (!m_connection.ExecuteQuery(sql, resultSets, outError))
		{
			return false;
		}

		const Connector::MySql::SMySqlResultSet* resultSet = FindFirstRows(resultSets);
		if (resultSet == nullptr || resultSet->rows.size() != 1 || resultSet->rows[0].size() < 2 ||
			!ParseUnsigned(resultSet->rows[0][0], outCurrency.amount) || !ParseUnsigned(resultSet->rows[0][1], outCurrency.version) ||
			outCurrency.version == 0)
		{
			outError = "sp_gd_cu_currency_credit returned an invalid balance.";
			return false;
		}

		outCurrency.currencyId = currencyId;
		return true;
	}

	bool FPlayerCacheRepository::DebitCurrency(
		const std::uint64_t userId,
		const std::uint16_t currencyId,
		const std::uint64_t amount,
		SPlayerCurrencyRow& outCurrency,
		std::string& outError)
	{
		outCurrency = {};
		const std::string sql =
			"CALL sp_gd_u_currency_debit(" + std::to_string(userId) + ',' + std::to_string(currencyId) + ',' + std::to_string(amount) + ')';
		std::vector<Connector::MySql::SMySqlResultSet> resultSets;
		if (!m_connection.ExecuteQuery(sql, resultSets, outError))
		{
			return false;
		}

		const Connector::MySql::SMySqlResultSet* resultSet = FindFirstRows(resultSets);
		if (resultSet == nullptr || resultSet->rows.size() != 1 || resultSet->rows[0].size() < 2 ||
			!ParseUnsigned(resultSet->rows[0][0], outCurrency.amount) || !ParseUnsigned(resultSet->rows[0][1], outCurrency.version) ||
			outCurrency.version == 0)
		{
			outError = "sp_gd_u_currency_debit returned an invalid balance.";
			return false;
		}

		outCurrency.currencyId = currencyId;
		return true;
	}

	bool FPlayerCacheRepository::CreateInventoryItem(
		const std::uint64_t userId,
		const std::uint32_t itemDataId,
		const std::uint32_t quantity,
		const std::uint32_t maxStack,
		const std::uint32_t maxInventorySlots,
		const std::uint32_t str,
		const std::uint32_t dex,
		const std::uint32_t intelligence,
		const std::uint32_t luk,
		const bool tradable,
		SInventoryItemMutationRow& outItem,
		std::string& outError)
	{
		outItem = {};
		const std::string itemDataJson = std::format(R"({{"str":{},"dex":{},"int":{},"luk":{}}})", str, dex, intelligence, luk);
		std::string escapedItemDataJson;
		if (!m_connection.EscapeString(itemDataJson, escapedItemDataJson, outError))
		{
			return false;
		}

		const std::string sql = "CALL sp_gd_c_inventory_item(" + std::to_string(userId) + ',' + std::to_string(itemDataId) + ',' +
								std::to_string(quantity) + ',' + std::to_string(maxStack) + ',' + std::to_string(maxInventorySlots) + ",'" +
								escapedItemDataJson + "'," + std::to_string(tradable ? 1 : 0) + ')';
		std::vector<Connector::MySql::SMySqlResultSet> resultSets;
		if (!m_connection.ExecuteQuery(sql, resultSets, outError))
		{
			return false;
		}

		const Connector::MySql::SMySqlResultSet* resultSet = FindFirstRows(resultSets);
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
			!ParseUnsigned(row[6], outItem.version) || outItem.itemInstanceId == 0 || outItem.itemDataId == 0 || outItem.quantity == 0 ||
			equipped > 1 || itemTradable > 1 || outItem.version == 0)
		{
			outError = "sp_gd_c_inventory_item returned an invalid value.";
			return false;
		}

		outItem.itemDataJson = itemDataJson;
		outItem.equipped = equipped != 0;
		outItem.tradable = itemTradable != 0;
		outItem.str = str;
		outItem.dex = dex;
		outItem.intelligence = intelligence;
		outItem.luk = luk;
		return true;
	}

	bool FPlayerCacheRepository::RemoveInventoryItem(
		const std::uint64_t userId,
		const std::uint64_t itemInstanceId,
		const std::uint64_t expectedVersion,
		SInventoryItemMutationRow& outItem,
		std::string& outError)
	{
		outItem = {};
		const std::string sql = "CALL sp_gd_d_inventory_item(" + std::to_string(userId) + ',' + std::to_string(itemInstanceId) + ',' +
								std::to_string(expectedVersion) + ')';
		std::vector<Connector::MySql::SMySqlResultSet> resultSets;
		if (!m_connection.ExecuteQuery(sql, resultSets, outError))
		{
			return false;
		}

		const Connector::MySql::SMySqlResultSet* resultSet = FindFirstRows(resultSets);
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
			outItem.itemInstanceId == 0 || outItem.itemDataId == 0 || outItem.quantity == 0 || equipped > 1 || tradable > 1 ||
			outItem.version == 0)
		{
			outError = "sp_gd_d_inventory_item returned an invalid value.";
			return false;
		}

		outItem.itemDataJson = *row[3];
		outItem.equipped = equipped != 0;
		outItem.tradable = tradable != 0;
		return true;
	}

	bool FPlayerCacheRepository::ClaimMailAttachment(
		const std::uint64_t userId,
		const std::uint64_t mailId,
		const std::uint64_t attachmentId,
		const std::uint32_t maxInventorySlots,
		const bool itemTradable,
		const std::uint64_t maxCurrencyAmount,
		SMailClaimMutationResult& outResult,
		std::string& outError)
	{
		outResult = {};
		const std::string sql = "CALL sp_gd_cu_mail_claim_attachment(" + std::to_string(userId) + ',' + std::to_string(mailId) + ',' +
								std::to_string(attachmentId) + ',' + std::to_string(maxInventorySlots) + ',' +
								std::to_string(itemTradable ? 1 : 0) + ',' + std::to_string(maxCurrencyAmount) + ')';
		std::vector<Connector::MySql::SMySqlResultSet> resultSets;
		if (!m_connection.ExecuteQuery(sql, resultSets, outError))
		{
			return false;
		}

		const Connector::MySql::SMySqlResultSet* resultSet = FindFirstRows(resultSets);
		if (resultSet == nullptr || resultSet->rows.size() != 1 || resultSet->rows[0].size() < 10)
		{
			outError = "sp_gd_cu_mail_claim_attachment returned an invalid result.";
			return false;
		}

		const auto& row = resultSet->rows[0];
		if (!ParseUnsigned(row[0], outResult.attachmentType) || !ParseUnsigned(row[1], outResult.itemInstanceId) ||
			!ParseUnsigned(row[2], outResult.itemDataId) || !ParseUnsigned(row[3], outResult.quantity) || !row[4].has_value() ||
			!ParseUnsigned(row[5], outResult.currencyId) || !ParseUnsigned(row[6], outResult.currencyAmount) ||
			!ParseUnsigned(row[7], outResult.currencyBalance) || !ParseUnsigned(row[8], outResult.currencyVersion) ||
			!ParseUnsigned(row[9], outResult.mailState))
		{
			outError = "sp_gd_cu_mail_claim_attachment returned an invalid value.";
			return false;
		}

		outResult.itemDataJson = *row[4];
		return true;
	}

	bool FPlayerCacheRepository::CreateItemMail(
		const std::uint64_t receiverUserId,
		const std::uint64_t itemInstanceId,
		const std::uint32_t itemDataId,
		const std::uint32_t quantity,
		const std::string& itemDataJson,
		const std::uint32_t mailType,
		const std::string& subject,
		const std::string& body,
		const std::uint32_t expirationSeconds,
		std::uint64_t& outMailId,
		std::string& outError)
	{
		outMailId = 0;
		std::string escapedItemData;
		std::string escapedSubject;
		std::string escapedBody;
		if (!m_connection.EscapeString(itemDataJson, escapedItemData, outError) ||
			!EscapeMailText(m_connection, subject, body, escapedSubject, escapedBody, outError))
		{
			return false;
		}
		const std::string sql = "CALL sp_gd_c_mail_item(" + std::to_string(receiverUserId) + ',' + std::to_string(itemInstanceId) + ',' +
								std::to_string(itemDataId) + ',' + std::to_string(quantity) + ",'" + escapedItemData + "'," +
								std::to_string(mailType) + ",'" + escapedSubject + "','" + escapedBody + "'," +
								std::to_string(expirationSeconds) + ')';
		std::vector<Connector::MySql::SMySqlResultSet> resultSets;
		return m_connection.ExecuteQuery(sql, resultSets, outError) &&
			   ParseMailCreationResult(resultSets, "sp_gd_c_mail_item", outMailId, outError);
	}

	bool FPlayerCacheRepository::CreateCurrencyMail(
		const std::uint64_t receiverUserId,
		const std::uint16_t currencyId,
		const std::uint64_t amount,
		const std::uint32_t mailType,
		const std::string& subject,
		const std::string& body,
		const std::uint32_t expirationSeconds,
		std::uint64_t& outMailId,
		std::string& outError)
	{
		outMailId = 0;
		std::string escapedSubject;
		std::string escapedBody;
		if (!EscapeMailText(m_connection, subject, body, escapedSubject, escapedBody, outError))
		{
			return false;
		}
		const std::string sql = "CALL sp_gd_c_mail_currency(" + std::to_string(receiverUserId) + ',' + std::to_string(currencyId) + ',' +
								std::to_string(amount) + ',' + std::to_string(mailType) + ",'" + escapedSubject + "','" + escapedBody +
								"'," + std::to_string(expirationSeconds) + ')';
		std::vector<Connector::MySql::SMySqlResultSet> resultSets;
		return m_connection.ExecuteQuery(sql, resultSets, outError) &&
			   ParseMailCreationResult(resultSets, "sp_gd_c_mail_currency", outMailId, outError);
	}

	bool FPlayerCacheRepository::CreateItemReturnMail(
		const std::uint64_t receiverUserId,
		const std::uint64_t itemInstanceId,
		const std::uint32_t itemDataId,
		const std::uint32_t quantity,
		const std::string& itemDataJson,
		const std::uint32_t mailType,
		const std::string& subject,
		const std::string& body,
		const std::uint32_t expirationSeconds,
		std::uint64_t& outMailId,
		std::string& outError)
	{
		outMailId = 0;
		std::string escapedItemData;
		std::string escapedSubject;
		std::string escapedBody;
		if (!m_connection.EscapeString(itemDataJson, escapedItemData, outError) ||
			!EscapeMailText(m_connection, subject, body, escapedSubject, escapedBody, outError))
		{
			return false;
		}
		const std::string sql = "CALL sp_gd_c_mail_item_return(" + std::to_string(receiverUserId) + ',' + std::to_string(itemInstanceId) +
								',' + std::to_string(itemDataId) + ',' + std::to_string(quantity) + ",'" + escapedItemData + "'," +
								std::to_string(mailType) + ",'" + escapedSubject + "','" + escapedBody + "'," +
								std::to_string(expirationSeconds) + ')';
		std::vector<Connector::MySql::SMySqlResultSet> resultSets;
		return m_connection.ExecuteQuery(sql, resultSets, outError) &&
			   ParseMailCreationResult(resultSets, "sp_gd_c_mail_item_return", outMailId, outError);
	}

	bool FPlayerCacheRepository::CreateExpiredItemReturnMail(
		const std::uint64_t receiverUserId,
		const std::uint64_t itemInstanceId,
		const std::uint32_t itemDataId,
		const std::uint32_t quantity,
		const std::string& itemDataJson,
		const std::uint32_t mailType,
		const std::string& subject,
		const std::string& body,
		const std::uint32_t expirationSeconds,
		std::uint64_t& outMailId,
		std::string& outError)
	{
		outMailId = 0;
		std::string escapedItemData;
		std::string escapedSubject;
		std::string escapedBody;
		if (!m_connection.EscapeString(itemDataJson, escapedItemData, outError) ||
			!EscapeMailText(m_connection, subject, body, escapedSubject, escapedBody, outError))
		{
			return false;
		}
		const std::string sql = "CALL sp_gd_c_mail_item_expired(" + std::to_string(receiverUserId) + ',' + std::to_string(itemInstanceId) +
								',' + std::to_string(itemDataId) + ',' + std::to_string(quantity) + ",'" + escapedItemData + "'," +
								std::to_string(mailType) + ",'" + escapedSubject + "','" + escapedBody + "'," +
								std::to_string(expirationSeconds) + ')';
		std::vector<Connector::MySql::SMySqlResultSet> resultSets;
		return m_connection.ExecuteQuery(sql, resultSets, outError) &&
			   ParseMailCreationResult(resultSets, "sp_gd_c_mail_item_expired", outMailId, outError);
	}
}
