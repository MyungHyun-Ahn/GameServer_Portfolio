#include "AuctionHouseServerPch.h"

#include "AuctionHouseServer/Service/FMailClaimService.h"

#include "AuctionHouseServer/Database/FContentThreadDbContext.h"
#include "AuctionHouseServer/Database/FGameRepository.h"

namespace AuctionHouseServer::Service
{
	FMailClaimService::FMailClaimService(
		Database::SAuctionDatabaseConfig config)
		: m_config(std::move(config))
	{
	}

	Domain::EAuctionResultCode FMailClaimService::Execute(
		const std::uint64_t userId,
		const std::uint64_t mailId,
		const std::uint64_t attachmentId,
		Database::SMailClaimResult& outResult,
		std::string& outError) const
	{
		if (userId == 0 || mailId == 0 || attachmentId == 0)
		{
			outError = "invalid mail claim request.";
			return Domain::EAuctionResultCode::InvalidRequest;
		}

		auto& context = Database::FContentThreadDbContext::Get(m_config);
		auto* connection = context.GetGamePrimary(outError);
		if (connection == nullptr)
		{
			return Domain::EAuctionResultCode::DatabaseUnavailable;
		}
		Connector::MySql::FMySqlTransaction transaction(*connection);
		if (!transaction.Begin(outError))
			return Domain::EAuctionResultCode::DatabaseUnavailable;
		Database::FGameRepository repository(*connection);
		if (!repository.ClaimMailAttachment(
				userId, mailId, attachmentId, m_config.maxInventorySlots, m_config.maxCurrencyAmount, outResult, outError))
		{
			if (outError.find("MAIL_ATTACHMENT_NOT_CLAIMABLE") != std::string::npos)
			{
				return Domain::EAuctionResultCode::MailAttachmentNotClaimable;
			}
			if (outError.find("INVENTORY_FULL") != std::string::npos)
			{
				return Domain::EAuctionResultCode::InventoryFull;
			}
			if (outError.find("ITEM_INSTANCE_CONFLICT") != std::string::npos)
			{
				return Domain::EAuctionResultCode::ItemInstanceConflict;
			}
			if (outError.find("CURRENCY_LIMIT_EXCEEDED") != std::string::npos)
			{
				return Domain::EAuctionResultCode::CurrencyLimitExceeded;
			}
			return Domain::EAuctionResultCode::DatabaseUnavailable;
		}
		if (!transaction.Commit(outError))
		{
			return Domain::EAuctionResultCode::PartialCommit;
		}
		return Domain::EAuctionResultCode::Success;
	}
}
