#include "AuctionHouseServerPch.h"

#include "AuctionHouseServer/Database/FAuctionRepository.h"

#include "Connector/MySql/FMySqlConnection.h"

namespace AuctionHouseServer::Database
{
	using Connector::MySql::FindFirstRows;
	using Connector::MySql::ParseUnsigned;

	FAuctionRepository::FAuctionRepository(
		Connector::MySql::FMySqlConnection& connection)
		: m_connection(connection)
	{
	}

	bool FAuctionRepository::PrepareListing(
		const SListingPrepareRequest& request,
		SListingPrepareResult& outResult,
		std::string& outError)
	{
		std::string escapedItemData;
		std::string escapedSearchName;
		std::string escapedSellerLoginId;
		if (!m_connection.EscapeString(request.itemDataJson, escapedItemData, outError) ||
			!m_connection.EscapeString(request.searchName, escapedSearchName, outError) ||
			!m_connection.EscapeString(request.sellerLoginId, escapedSellerLoginId, outError))
		{
			return false;
		}

		std::ostringstream sql;
		sql << "CALL sp_ad_c_listing_prepare(" << request.sellerUserId << ",'" << escapedSellerLoginId << "'," << request.itemInstanceId
			<< ',' << request.itemDataId << ',' << static_cast<unsigned int>(request.itemCategory) << ',' << request.quantity << ",'"
			<< escapedItemData << "','" << escapedSearchName << "',0,0," << request.searchStr << ',' << request.searchDex << ','
			<< request.searchInt << ',' << request.searchLuk << ',' << request.currencyId << ',' << request.startPrice << ',';
		if (request.buyoutPrice == 0)
		{
			sql << "NULL";
		}
		else
		{
			sql << request.buyoutPrice;
		}
		sql << ",DATE_ADD(UTC_TIMESTAMP(6),INTERVAL " << request.durationSeconds << " SECOND)," << request.maxActiveListings << ')';

		std::vector<Connector::MySql::SMySqlResultSet> resultSets;
		if (!m_connection.ExecuteQuery(sql.str(), resultSets, outError))
		{
			return false;
		}
		const auto* resultSet = FindFirstRows(resultSets);
		if (resultSet == nullptr || resultSet->rows.size() != 1 || resultSet->rows[0].size() < 2 ||
			!ParseUnsigned(resultSet->rows[0][0], outResult.listingId) || !ParseUnsigned(resultSet->rows[0][1], outResult.version))
		{
			outError = "sp_ad_c_listing_prepare returned an invalid result.";
			return false;
		}
		return true;
	}

	bool FAuctionRepository::ActivateListing(
		const std::uint64_t listingId,
		const std::uint64_t expectedVersion,
		std::string& outError)
	{
		std::ostringstream sql;
		sql << "CALL sp_ad_u_listing_activate(" << listingId << ',' << expectedVersion << ')';
		return m_connection.Execute(sql.str(), outError);
	}

	bool FAuctionRepository::SearchListings(
		const SListingSearchQuery& query,
		std::vector<SListingSummary>& outListings,
		std::string& outError)
	{
		outListings.clear();
		if (query.itemDataIds.size() > 100)
		{
			outError = "listing search ItemDataId count exceeds 100.";
			return false;
		}
		std::ostringstream sql;
		sql << "CALL sp_ad_r_listings(" << static_cast<unsigned int>(query.itemCategory) << ",'[";
		for (std::size_t index = 0; index < query.itemDataIds.size(); ++index)
		{
			if (index != 0)
				sql << ',';
			sql << query.itemDataIds[index];
		}
		sql << "]'," << query.minStr << ',' << query.minDex << ',' << query.minInt << ',' << query.minLuk << ',' << query.sellerUserId
			<< ',' << static_cast<unsigned int>(query.sortType) << ',' << query.cursorSortValue << ',' << query.cursorListingId << ','
			<< query.limit << ')';
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
			if (row.size() < 17 || !row[2].has_value() || !row[6].has_value())
			{
				outError = "sp_ad_r_listings returned an invalid column count.";
				return false;
			}
			SListingSummary listing;
			if (!ParseUnsigned(row[0], listing.listingId) || !ParseUnsigned(row[1], listing.sellerUserId) ||
				!ParseUnsigned(row[3], listing.itemDataId) || !ParseUnsigned(row[4], listing.itemCategory) ||
				!ParseUnsigned(row[5], listing.quantity) || !ParseUnsigned(row[7], listing.str) || !ParseUnsigned(row[8], listing.dex) ||
				!ParseUnsigned(row[9], listing.intelligence) || !ParseUnsigned(row[10], listing.luk) ||
				!ParseUnsigned(row[11], listing.currencyId) || !ParseUnsigned(row[12], listing.startPrice) ||
				!ParseUnsigned(row[13], listing.currentBidPrice) || !ParseUnsigned(row[15], listing.expiresAtUnixMs) ||
				!ParseUnsigned(row[16], listing.version))
			{
				outError = "sp_ad_r_listings returned an invalid value.";
				return false;
			}
			if (row[14].has_value() && !ParseUnsigned(row[14], listing.buyoutPrice))
			{
				outError = "sp_ad_r_listings returned an invalid buyout price.";
				return false;
			}
			listing.sellerLoginId = *row[2];
			listing.name = *row[6];
			outListings.push_back(std::move(listing));
		}
		return true;
	}

	bool FAuctionRepository::GetListingDetail(
		const std::uint64_t listingId,
		SListingDetail& outListing,
		bool& outFound,
		std::string& outError)
	{
		outFound = false;
		std::ostringstream sql;
		sql << "CALL sp_ad_r_listing_detail(" << listingId << ')';
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
		if (resultSet->rows.size() != 1 || resultSet->rows[0].size() < 20)
		{
			outError = "sp_ad_r_listing_detail returned an invalid row count.";
			return false;
		}

		const auto& row = resultSet->rows[0];
		if (!row[2].has_value() || !row[7].has_value() || !row[8].has_value() || !ParseUnsigned(row[0], outListing.listingId) ||
			!ParseUnsigned(row[1], outListing.sellerUserId) || !ParseUnsigned(row[3], outListing.itemInstanceId) ||
			!ParseUnsigned(row[4], outListing.itemDataId) || !ParseUnsigned(row[5], outListing.itemCategory) ||
			!ParseUnsigned(row[6], outListing.quantity) || !ParseUnsigned(row[9], outListing.str) ||
			!ParseUnsigned(row[10], outListing.dex) || !ParseUnsigned(row[11], outListing.intelligence) ||
			!ParseUnsigned(row[12], outListing.luk) || !ParseUnsigned(row[13], outListing.currencyId) ||
			!ParseUnsigned(row[14], outListing.startPrice) || !ParseUnsigned(row[15], outListing.currentBidPrice) ||
			!ParseUnsigned(row[18], outListing.expiresAtUnixMs) || !ParseUnsigned(row[19], outListing.version))
		{
			outError = "sp_ad_r_listing_detail returned an invalid value.";
			return false;
		}
		if ((row[16].has_value() && !ParseUnsigned(row[16], outListing.buyoutPrice)) ||
			(row[17].has_value() && !ParseUnsigned(row[17], outListing.highestBidderUserId)))
		{
			outError = "sp_ad_r_listing_detail returned an invalid nullable value.";
			return false;
		}
		outListing.sellerLoginId = *row[2];
		outListing.itemDataJson = *row[7];
		outListing.name = *row[8];
		outFound = true;
		return true;
	}

	bool FAuctionRepository::SearchSaleHistory(
		const SSaleHistoryQuery& query,
		std::vector<SSaleHistorySummary>& outHistory,
		std::string& outError)
	{
		outHistory.clear();
		if (query.itemDataIds.size() > 100)
		{
			outError = "sale history ItemDataId count exceeds 100.";
			return false;
		}

		std::ostringstream sql;
		sql << "CALL sp_ad_r_sale_history(" << static_cast<unsigned int>(query.itemCategory) << ",'[";
		for (std::size_t index = 0; index < query.itemDataIds.size(); ++index)
		{
			if (index != 0)
				sql << ',';
			sql << query.itemDataIds[index];
		}
		sql << "]'," << query.minStr << ',' << query.minDex << ',' << query.minInt << ',' << query.minLuk << ','
			<< static_cast<unsigned int>(query.sortType) << ',' << query.cursorSortValue << ',' << query.cursorListingId << ','
			<< query.limit << ')';
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
			if (row.size() < 13 || !row[4].has_value())
			{
				outError = "sp_ad_r_sale_history returned an invalid column count.";
				return false;
			}
			SSaleHistorySummary history;
			if (!ParseUnsigned(row[0], history.listingId) || !ParseUnsigned(row[1], history.itemDataId) ||
				!ParseUnsigned(row[2], history.itemCategory) || !ParseUnsigned(row[3], history.quantity) ||
				!ParseUnsigned(row[5], history.str) || !ParseUnsigned(row[6], history.dex) ||
				!ParseUnsigned(row[7], history.intelligence) || !ParseUnsigned(row[8], history.luk) ||
				!ParseUnsigned(row[9], history.currencyId) || !ParseUnsigned(row[10], history.finalPrice) ||
				!ParseUnsigned(row[11], history.saleType) || !ParseUnsigned(row[12], history.soldAtUnixMs))
			{
				outError = "sp_ad_r_sale_history returned an invalid value.";
				return false;
			}
			history.name = *row[4];
			outHistory.push_back(std::move(history));
		}
		return true;
	}

	bool FAuctionRepository::GetSaleHistoryDetail(
		const std::uint64_t listingId,
		SSaleHistoryDetail& outHistory,
		bool& outFound,
		std::string& outError)
	{
		outFound = false;
		std::ostringstream sql;
		sql << "CALL sp_ad_r_sale_history_detail(" << listingId << ')';
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
		if (resultSet->rows.size() != 1 || resultSet->rows[0].size() < 16)
		{
			outError = "sp_ad_r_sale_history_detail returned an invalid row count.";
			return false;
		}

		const auto& row = resultSet->rows[0];
		if (!row[1].has_value() || !row[5].has_value() || !row[6].has_value() || !ParseUnsigned(row[0], outHistory.listingId) ||
			!ParseUnsigned(row[2], outHistory.itemDataId) || !ParseUnsigned(row[3], outHistory.itemCategory) ||
			!ParseUnsigned(row[4], outHistory.quantity) || !ParseUnsigned(row[7], outHistory.str) ||
			!ParseUnsigned(row[8], outHistory.dex) || !ParseUnsigned(row[9], outHistory.intelligence) ||
			!ParseUnsigned(row[10], outHistory.luk) || !ParseUnsigned(row[11], outHistory.currencyId) ||
			!ParseUnsigned(row[12], outHistory.startPrice) || !ParseUnsigned(row[13], outHistory.finalPrice) ||
			!ParseUnsigned(row[14], outHistory.saleType) || !ParseUnsigned(row[15], outHistory.soldAtUnixMs))
		{
			outError = "sp_ad_r_sale_history_detail returned an invalid value.";
			return false;
		}
		outHistory.sellerLoginId = *row[1];
		outHistory.itemDataJson = *row[5];
		outHistory.name = *row[6];
		outFound = true;
		return true;
	}

	bool FAuctionRepository::PrepareBid(
		const std::uint64_t listingId,
		const std::uint64_t bidderUserId,
		const std::uint64_t bidAmount,
		const std::uint64_t expectedListingVersion,
		SBidPrepareResult& outResult,
		std::string& outError)
	{
		std::ostringstream sql;
		sql << "CALL sp_ad_cu_bid_prepare(" << listingId << ',' << bidderUserId << ',' << bidAmount << ',' << expectedListingVersion << ')';
		std::vector<Connector::MySql::SMySqlResultSet> resultSets;
		if (!m_connection.ExecuteQuery(sql.str(), resultSets, outError))
		{
			return false;
		}
		const auto* resultSet = FindFirstRows(resultSets);
		if (resultSet == nullptr || resultSet->rows.size() != 1 || resultSet->rows[0].size() < 7)
		{
			outError = "sp_ad_cu_bid_prepare returned an invalid result.";
			return false;
		}
		const auto& row = resultSet->rows[0];
		return ParseUnsigned(row[0], outResult.bidId) && ParseUnsigned(row[1], outResult.additionalDebit) &&
			   ParseUnsigned(row[2], outResult.currencyId) && ParseUnsigned(row[3], outResult.previousHighestBidId) &&
			   ParseUnsigned(row[4], outResult.previousHighestBidderUserId) && ParseUnsigned(row[5], outResult.previousHighestAmount) &&
			   ParseUnsigned(row[6], outResult.preparedListingVersion);
	}

	bool FAuctionRepository::CompleteBid(
		const std::uint64_t listingId,
		const std::uint64_t bidId,
		const std::uint64_t bidderUserId,
		const std::uint64_t expectedListingVersion,
		std::uint64_t& outListingVersion,
		std::string& outError)
	{
		std::ostringstream sql;
		sql << "CALL sp_ad_u_bid_complete(" << listingId << ',' << bidId << ',' << bidderUserId << ',' << expectedListingVersion << ')';
		std::vector<Connector::MySql::SMySqlResultSet> resultSets;
		if (!m_connection.ExecuteQuery(sql.str(), resultSets, outError))
		{
			return false;
		}
		const auto* resultSet = FindFirstRows(resultSets);
		return resultSet != nullptr && resultSet->rows.size() == 1 && !resultSet->rows[0].empty() &&
			   ParseUnsigned(resultSet->rows[0][0], outListingVersion);
	}

	bool FAuctionRepository::GetOutbidClaimable(
		const std::uint64_t bidderUserId,
		const std::uint32_t limit,
		std::vector<SOutbidClaimable>& outBids,
		std::string& outError)
	{
		outBids.clear();
		std::ostringstream sql;
		sql << "CALL sp_ad_r_outbid_claimable(" << bidderUserId << ',' << limit << ')';
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
			if (row.size() < 4)
			{
				outError = "sp_ad_r_outbid_claimable returned an invalid column count.";
				return false;
			}
			SOutbidClaimable bid;
			if (!ParseUnsigned(row[0], bid.listingId) || !ParseUnsigned(row[1], bid.bidId) || !ParseUnsigned(row[2], bid.heldAmount) ||
				!ParseUnsigned(row[3], bid.newHighestAmount))
			{
				outError = "sp_ad_r_outbid_claimable returned an invalid value.";
				return false;
			}
			outBids.push_back(bid);
		}
		return true;
	}

	bool FAuctionRepository::PrepareBuyout(
		const std::uint64_t listingId,
		const std::uint64_t buyerUserId,
		const std::uint64_t expectedListingVersion,
		SBuyoutPrepareResult& outResult,
		std::string& outError)
	{
		std::ostringstream sql;
		sql << "CALL sp_ad_cu_buyout_prepare(" << listingId << ',' << buyerUserId << ',' << expectedListingVersion << ')';
		std::vector<Connector::MySql::SMySqlResultSet> resultSets;
		if (!m_connection.ExecuteQuery(sql.str(), resultSets, outError))
		{
			return false;
		}
		const auto* resultSet = FindFirstRows(resultSets);
		if (resultSet == nullptr || resultSet->rows.size() != 1 || resultSet->rows[0].size() < 12)
		{
			outError = "sp_ad_cu_buyout_prepare returned an invalid result.";
			return false;
		}
		const auto& row = resultSet->rows[0];
		if (!row[4].has_value() || !ParseUnsigned(row[0], outResult.sellerUserId) || !ParseUnsigned(row[1], outResult.itemInstanceId) ||
			!ParseUnsigned(row[2], outResult.itemDataId) || !ParseUnsigned(row[3], outResult.quantity) ||
			!ParseUnsigned(row[5], outResult.currencyId) || !ParseUnsigned(row[6], outResult.buyoutPrice) ||
			!ParseUnsigned(row[7], outResult.additionalDebit) || !ParseUnsigned(row[8], outResult.previousHighestBidId) ||
			!ParseUnsigned(row[9], outResult.previousHighestBidderUserId) || !ParseUnsigned(row[10], outResult.previousHighestAmount) ||
			!ParseUnsigned(row[11], outResult.preparedListingVersion))
		{
			outError = "sp_ad_cu_buyout_prepare returned an invalid value.";
			return false;
		}
		outResult.itemDataJson = *row[4];
		return true;
	}

	bool FAuctionRepository::CompleteBuyout(
		const std::uint64_t listingId,
		const std::uint64_t buyerUserId,
		const std::uint64_t expectedListingVersion,
		std::uint64_t& outListingVersion,
		std::string& outError)
	{
		std::ostringstream sql;
		sql << "CALL sp_ad_u_buyout_complete(" << listingId << ',' << buyerUserId << ',' << expectedListingVersion << ')';
		std::vector<Connector::MySql::SMySqlResultSet> resultSets;
		if (!m_connection.ExecuteQuery(sql.str(), resultSets, outError))
		{
			return false;
		}
		const auto* resultSet = FindFirstRows(resultSets);
		return resultSet != nullptr && resultSet->rows.size() == 1 && !resultSet->rows[0].empty() &&
			   ParseUnsigned(resultSet->rows[0][0], outListingVersion);
	}

	bool FAuctionRepository::PrepareListingCancel(
		const std::uint64_t listingId,
		const std::uint64_t sellerUserId,
		const std::uint64_t expectedListingVersion,
		SListingCancelPrepareResult& outResult,
		std::string& outError)
	{
		std::ostringstream sql;
		sql << "CALL sp_ad_u_cancel_prepare(" << listingId << ',' << sellerUserId << ',' << expectedListingVersion << ')';
		std::vector<Connector::MySql::SMySqlResultSet> resultSets;
		if (!m_connection.ExecuteQuery(sql.str(), resultSets, outError))
			return false;
		const auto* resultSet = FindFirstRows(resultSets);
		if (resultSet == nullptr || resultSet->rows.size() != 1 || resultSet->rows[0].size() < 5 || !resultSet->rows[0][3].has_value() ||
			!ParseUnsigned(resultSet->rows[0][0], outResult.itemInstanceId) ||
			!ParseUnsigned(resultSet->rows[0][1], outResult.itemDataId) || !ParseUnsigned(resultSet->rows[0][2], outResult.quantity) ||
			!ParseUnsigned(resultSet->rows[0][4], outResult.preparedListingVersion))
		{
			outError = "sp_ad_u_cancel_prepare returned an invalid result.";
			return false;
		}
		outResult.itemDataJson = *resultSet->rows[0][3];
		return true;
	}

	bool FAuctionRepository::CompleteListingCancel(
		const std::uint64_t listingId,
		const std::uint64_t sellerUserId,
		const std::uint64_t expectedListingVersion,
		std::uint64_t& outListingVersion,
		std::string& outError)
	{
		std::ostringstream sql;
		sql << "CALL sp_ad_u_cancel_complete(" << listingId << ',' << sellerUserId << ',' << expectedListingVersion << ')';
		std::vector<Connector::MySql::SMySqlResultSet> resultSets;
		if (!m_connection.ExecuteQuery(sql.str(), resultSets, outError))
			return false;
		const auto* resultSet = FindFirstRows(resultSets);
		return resultSet != nullptr && resultSet->rows.size() == 1 && !resultSet->rows[0].empty() &&
			   ParseUnsigned(resultSet->rows[0][0], outListingVersion);
	}

	bool FAuctionRepository::GetExpiredListingCandidates(
		const std::uint32_t limit,
		std::vector<std::uint64_t>& outListingIds,
		std::string& outError)
	{
		outListingIds.clear();
		std::ostringstream sql;
		sql << "CALL sp_ad_r_expired_listing_candidates(" << limit << ')';
		std::vector<Connector::MySql::SMySqlResultSet> resultSets;
		if (!m_connection.ExecuteQuery(sql.str(), resultSets, outError))
			return false;
		const auto* resultSet = FindFirstRows(resultSets);
		if (resultSet == nullptr)
			return true;
		for (const auto& row : resultSet->rows)
		{
			std::uint64_t listingId = 0;
			if (row.empty() || !ParseUnsigned(row[0], listingId))
			{
				outError = "sp_ad_r_expired_listing_candidates returned an invalid value.";
				return false;
			}
			outListingIds.push_back(listingId);
		}
		return true;
	}

	bool FAuctionRepository::PrepareExpiration(
		const std::uint64_t listingId,
		SExpirationPrepareResult& outResult,
		std::string& outError)
	{
		std::ostringstream sql;
		sql << "CALL sp_ad_u_expire_prepare(" << listingId << ')';
		std::vector<Connector::MySql::SMySqlResultSet> resultSets;
		if (!m_connection.ExecuteQuery(sql.str(), resultSets, outError))
			return false;
		const auto* resultSet = FindFirstRows(resultSets);
		if (resultSet == nullptr || resultSet->rows.size() != 1 || resultSet->rows[0].size() < 10 || !resultSet->rows[0][4].has_value() ||
			!ParseUnsigned(resultSet->rows[0][0], outResult.sellerUserId) ||
			!ParseUnsigned(resultSet->rows[0][1], outResult.itemInstanceId) ||
			!ParseUnsigned(resultSet->rows[0][2], outResult.itemDataId) || !ParseUnsigned(resultSet->rows[0][3], outResult.quantity) ||
			!ParseUnsigned(resultSet->rows[0][5], outResult.currencyId) || !ParseUnsigned(resultSet->rows[0][6], outResult.highestBidId) ||
			!ParseUnsigned(resultSet->rows[0][7], outResult.winnerUserId) || !ParseUnsigned(resultSet->rows[0][8], outResult.finalPrice) ||
			!ParseUnsigned(resultSet->rows[0][9], outResult.preparedListingVersion))
		{
			outError = "sp_ad_u_expire_prepare returned an invalid result.";
			return false;
		}
		outResult.itemDataJson = *resultSet->rows[0][4];
		return true;
	}

	bool FAuctionRepository::CompleteExpiration(
		const std::uint64_t listingId,
		const std::uint64_t winnerUserId,
		const std::uint64_t finalPrice,
		const std::uint64_t expectedListingVersion,
		std::uint64_t& outListingVersion,
		std::string& outError)
	{
		std::ostringstream sql;
		sql << "CALL sp_ad_u_expire_complete(" << listingId << ',' << winnerUserId << ',' << finalPrice << ',' << expectedListingVersion
			<< ')';
		std::vector<Connector::MySql::SMySqlResultSet> resultSets;
		if (!m_connection.ExecuteQuery(sql.str(), resultSets, outError))
			return false;
		const auto* resultSet = FindFirstRows(resultSets);
		return resultSet != nullptr && resultSet->rows.size() == 1 && !resultSet->rows[0].empty() &&
			   ParseUnsigned(resultSet->rows[0][0], outListingVersion);
	}

	bool FAuctionRepository::GetMyBids(
		const std::uint64_t userId,
		const std::uint64_t cursorBidId,
		const std::uint32_t limit,
		std::vector<SMyBid>& outBids,
		std::string& outError)
	{
		outBids.clear();
		std::ostringstream sql;
		sql << "CALL sp_ad_r_my_bids(" << userId << ',' << cursorBidId << ',' << limit << ')';
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
			if (row.size() < 12)
			{
				outError = "sp_ad_r_my_bids returned an invalid column count.";
				return false;
			}
			SMyBid bid;
			if (!ParseUnsigned(row[0], bid.bidId) || !ParseUnsigned(row[1], bid.listingId) || !ParseUnsigned(row[2], bid.currencyId) ||
				!ParseUnsigned(row[3], bid.bidAmount) || !ParseUnsigned(row[4], bid.bidState) || !ParseUnsigned(row[5], bid.bidVersion) ||
				!ParseUnsigned(row[6], bid.itemDataId) || !ParseUnsigned(row[8], bid.currentBidPrice) ||
				!ParseUnsigned(row[10], bid.listingState))
			{
				outError = "sp_ad_r_my_bids returned an invalid numeric value.";
				return false;
			}
			if (row[9].has_value() && !ParseUnsigned(row[9], bid.highestBidderUserId))
			{
				outError = "sp_ad_r_my_bids returned an invalid highest bidder.";
				return false;
			}
			outBids.push_back(std::move(bid));
		}
		return true;
	}

	bool FAuctionRepository::PrepareBidRefund(
		const std::uint64_t listingId,
		const std::uint64_t bidId,
		const std::uint64_t bidderUserId,
		const std::uint64_t expectedVersion,
		SBidRefundPrepareResult& outResult,
		std::string& outError)
	{
		std::ostringstream sql;
		sql << "CALL sp_ad_u_bid_refund_prepare(" << listingId << ',' << bidId << ',' << bidderUserId << ',' << expectedVersion << ')';
		std::vector<Connector::MySql::SMySqlResultSet> resultSets;
		if (!m_connection.ExecuteQuery(sql.str(), resultSets, outError))
		{
			return false;
		}
		const auto* resultSet = FindFirstRows(resultSets);
		if (resultSet == nullptr || resultSet->rows.size() != 1 || resultSet->rows[0].size() < 6)
		{
			outError = "sp_ad_u_bid_refund_prepare did not return one bid.";
			return false;
		}
		const auto& row = resultSet->rows[0];
		return ParseUnsigned(row[0], outResult.bidId) && ParseUnsigned(row[1], outResult.listingId) &&
			   ParseUnsigned(row[2], outResult.bidderUserId) && ParseUnsigned(row[3], outResult.currencyId) &&
			   ParseUnsigned(row[4], outResult.bidAmount) && ParseUnsigned(row[5], outResult.preparedVersion);
	}

	bool FAuctionRepository::CompleteBidRefund(
		const std::uint64_t listingId,
		const std::uint64_t bidId,
		const std::uint64_t bidderUserId,
		const std::uint64_t expectedVersion,
		std::string& outError)
	{
		std::ostringstream sql;
		sql << "CALL sp_ad_u_bid_refund_complete(" << listingId << ',' << bidId << ',' << bidderUserId << ',' << expectedVersion << ')';
		return m_connection.Execute(sql.str(), outError);
	}

	bool FAuctionRepository::RevertBidRefund(
		const std::uint64_t listingId,
		const std::uint64_t bidId,
		const std::uint64_t bidderUserId,
		const std::uint64_t expectedVersion,
		std::string& outError)
	{
		std::ostringstream sql;
		sql << "CALL sp_ad_u_bid_refund_revert(" << listingId << ',' << bidId << ',' << bidderUserId << ',' << expectedVersion << ')';
		return m_connection.Execute(sql.str(), outError);
	}
}
