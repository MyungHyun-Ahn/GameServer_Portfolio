#include "CacheServerPch.h"

#include "CacheServer/Contents/FPlayerCacheContent.h"

#include "CacheServer/Contents/ContentTypes.h"
#include "CacheServer/Database/FContentThreadDbContext.h"
#include "CacheServer/Database/FPlayerCacheRepository.h"
#include "Connector/MySql/FMySqlTransaction.h"
#include "ContentsRuntime/Bridge/IContentBridge.h"

#include <charconv>
#include <cctype>

namespace CacheServer::Contents
{
	namespace
	{
		constexpr std::uint64_t kFnvOffsetBasis = 14695981039346656037ULL;
		constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

		struct SBuiltEquipmentState final
		{
			std::vector<Cache::Protocol::FEquippedItemSnapshot> equippedItems;
			std::uint64_t equipmentVersion = kFnvOffsetBasis;
		};

		void HashCombine(
			std::uint64_t& hash,
			const std::uint64_t value) noexcept
		{
			for (std::uint32_t byteIndex = 0; byteIndex < sizeof(value); ++byteIndex)
			{
				hash ^= (value >> (byteIndex * 8)) & 0xFFULL;
				hash *= kFnvPrime;
			}
		}

		bool TryReadInstancePrimaryStats(
			const std::string& itemDataJson,
			Domain::SPrimaryStats& outStats) noexcept
		{
			outStats = {};
			if (itemDataJson.size() < 2 || itemDataJson.front() != '{' || itemDataJson.back() != '}')
			{
				return false;
			}

			const auto readStat = [&itemDataJson](const std::string_view name, std::uint32_t& outValue)
			{
				const std::string key = '"' + std::string(name) + '"';
				std::size_t position = itemDataJson.find(key);
				if (position == std::string::npos)
				{
					outValue = 0;
					return true;
				}
				position = itemDataJson.find(':', position + key.size());
				if (position == std::string::npos)
				{
					return false;
				}
				++position;
				while (position < itemDataJson.size() && std::isspace(static_cast<unsigned char>(itemDataJson[position])) != 0)
				{
					++position;
				}

				std::uint64_t parsedValue = 0;
				const char* begin = itemDataJson.data() + position;
				const char* end = itemDataJson.data() + itemDataJson.size();
				const auto parsed = std::from_chars(begin, end, parsedValue);
				if (parsed.ec != std::errc{} || parsed.ptr == begin || parsedValue > std::numeric_limits<std::uint32_t>::max())
				{
					return false;
				}
				const char* trailing = parsed.ptr;
				while (trailing < end && std::isspace(static_cast<unsigned char>(*trailing)) != 0)
				{
					++trailing;
				}
				if (trailing == end || (*trailing != ',' && *trailing != '}'))
				{
					return false;
				}
				outValue = static_cast<std::uint32_t>(parsedValue);
				return true;
			};

			return readStat("str", outStats.str) && readStat("dex", outStats.dex) && readStat("int", outStats.intelligence) &&
				   readStat("luk", outStats.luk);
		}

		bool TryBuildEquipmentState(
			const Domain::FCachedInventoryItemMap& inventoryItems,
			const GameData::Item::FItemDataTable& itemDataTable,
			SBuiltEquipmentState& outState,
			std::string& outError)
		{
			outState = {};
			std::vector<const Domain::SCachedInventoryItem*> equippedItems;
			for (const auto& [itemInstanceId, item] : inventoryItems)
			{
				(void)itemInstanceId;
				if (item.equipped)
				{
					equippedItems.push_back(&item);
				}
			}
			std::ranges::sort(equippedItems, {}, &Domain::SCachedInventoryItem::itemInstanceId);

			std::unordered_set<GameData::Common::EEquipmentSlot> occupiedSlots;
			outState.equippedItems.reserve(equippedItems.size());
			for (const Domain::SCachedInventoryItem* item : equippedItems)
			{
				const GameData::Item::SItemTemplate* itemTemplate = itemDataTable.Find(item->itemDataId);
				if (itemTemplate == nullptr || itemTemplate->category != GameData::Common::EItemCategory::Equipment ||
					itemTemplate->equipmentSlot == GameData::Common::EEquipmentSlot::None)
				{
					outError = "equipped inventory item references invalid equipment GameData.";
					return false;
				}
				if (!occupiedSlots.emplace(itemTemplate->equipmentSlot).second)
				{
					outError = "multiple equipped items occupy the same EquipmentSlot.";
					return false;
				}

				Domain::SPrimaryStats instanceStats;
				if (!TryReadInstancePrimaryStats(item->itemDataJson, instanceStats))
				{
					outError = "equipped inventory item contains invalid instance stats.";
					return false;
				}

				Cache::Protocol::FEquippedItemSnapshot snapshot;
				snapshot.itemInstanceId = item->itemInstanceId;
				snapshot.itemDataId = item->itemDataId;
				snapshot.itemVersion = item->version;
				snapshot.strStat = instanceStats.str;
				snapshot.dexStat = instanceStats.dex;
				snapshot.intStat = instanceStats.intelligence;
				snapshot.lukStat = instanceStats.luk;
				outState.equippedItems.push_back(snapshot);

				HashCombine(outState.equipmentVersion, item->itemInstanceId);
				HashCombine(outState.equipmentVersion, item->itemDataId);
				HashCombine(outState.equipmentVersion, item->version);
			}

			outError.clear();
			return true;
		}

		std::uint64_t GetUnixTimeMilliseconds() noexcept
		{
			return static_cast<std::uint64_t>(
				std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
		}

		Cache::Protocol::ECacheQueryResult ToQueryResult(
			const Cache::Protocol::ECacheUserLoadResult loadResult) noexcept
		{
			if (loadResult == Cache::Protocol::ECacheUserLoadResult::InvalidUserId)
			{
				return Cache::Protocol::ECacheQueryResult::InvalidArgument;
			}
			return Cache::Protocol::ECacheQueryResult::DatabaseError;
		}

		Cache::Protocol::ECacheCommandResult ToCommandResult(
			const std::string& error) noexcept
		{
			using Cache::Protocol::ECacheCommandResult;
			if (error.find("INVENTORY_ITEM_NOT_FOUND") != std::string::npos)
			{
				return ECacheCommandResult::NotFound;
			}
			if (error.find("ITEM_VERSION_MISMATCH") != std::string::npos)
			{
				return ECacheCommandResult::ItemVersionMismatch;
			}
			if (error.find("ITEM_EQUIPPED") != std::string::npos)
			{
				return ECacheCommandResult::ItemEquipped;
			}
			if (error.find("INVENTORY_FULL") != std::string::npos)
			{
				return ECacheCommandResult::InventoryFull;
			}
			if (error.find("ITEM_INSTANCE_CONFLICT") != std::string::npos)
			{
				return ECacheCommandResult::ItemInstanceConflict;
			}
			if (error.find("CURRENCY_LIMIT_EXCEEDED") != std::string::npos)
			{
				return ECacheCommandResult::CurrencyLimitExceeded;
			}
			if (error.find("MAIL_ATTACHMENT_NOT_CLAIMABLE") != std::string::npos)
			{
				return ECacheCommandResult::MailAttachmentNotClaimable;
			}
			if (error.find("INSUFFICIENT_CURRENCY") != std::string::npos)
			{
				return ECacheCommandResult::InsufficientCurrency;
			}
			if (error.find("CONCURRENT_MODIFICATION") != std::string::npos)
			{
				return ECacheCommandResult::ConcurrentModification;
			}
			return ECacheCommandResult::DatabaseError;
		}

		bool ToRpcInventoryItem(
			const Domain::SCachedInventoryItem& source,
			Cache::Protocol::FInventoryItemSnapshot& outItem) noexcept
		{
			outItem.itemInstanceId = source.itemInstanceId;
			outItem.itemDataId = source.itemDataId;
			outItem.quantity = source.quantity;
			outItem.itemDataJson = source.itemDataJson;
			outItem.equipped = source.equipped;
			outItem.tradable = source.tradable;
			outItem.version = source.version;
			Domain::SPrimaryStats stats;
			if (!TryReadInstancePrimaryStats(source.itemDataJson, stats))
			{
				return false;
			}
			outItem.strStat = stats.str;
			outItem.dexStat = stats.dex;
			outItem.intStat = stats.intelligence;
			outItem.lukStat = stats.luk;
			return true;
		}

		Domain::SPlayerProgress ToDomainProgress(
			const Database::SPlayerCharacterRow& source) noexcept
		{
			Domain::SPlayerProgress progress;
			progress.characterId = source.characterId;
			progress.characterDataId = source.characterDataId;
			progress.level = source.level;
			progress.exp = source.exp;
			progress.persistentPrimary = {source.str, source.dex, source.intelligence, source.luk};
			progress.unspentStatPoints = source.unspentStatPoints;
			progress.progressVersion = source.progressVersion;
			progress.statVersion = source.statVersion;
			return progress;
		}

		bool ToRpcPlayerWorldSnapshot(
			const Domain::FCacheUser& user,
			const GameData::Item::FItemDataTable& itemDataTable,
			Cache::Protocol::FPlayerWorldSnapshot& outSnapshot,
			std::string& outError)
		{
			outSnapshot = {};
			const Domain::SPlayerProgress& progress = user.GetProgress();
			outSnapshot.progress.characterId = progress.characterId;
			outSnapshot.progress.characterDataId = progress.characterDataId;
			outSnapshot.progress.level = progress.level;
			outSnapshot.progress.exp = progress.exp;
			outSnapshot.progress.strStat = progress.persistentPrimary.str;
			outSnapshot.progress.dexStat = progress.persistentPrimary.dex;
			outSnapshot.progress.intStat = progress.persistentPrimary.intelligence;
			outSnapshot.progress.lukStat = progress.persistentPrimary.luk;
			outSnapshot.progress.unspentStatPoints = progress.unspentStatPoints;
			outSnapshot.progress.progressVersion = progress.progressVersion;
			outSnapshot.progress.statVersion = progress.statVersion;

			SBuiltEquipmentState equipmentState;
			if (!TryBuildEquipmentState(user.GetInventoryItems(), itemDataTable, equipmentState, outError))
			{
				return false;
			}
			const Domain::SPlayerStateRevision& revision = user.GetStateRevision();
			if (revision.equipmentVersion == 0 || revision.statRevision == 0 ||
				revision.equipmentVersion != equipmentState.equipmentVersion)
			{
				outError = "cached equipment state does not match its revision.";
				return false;
			}

			outSnapshot.equippedItems = std::move(equipmentState.equippedItems);
			outSnapshot.equipmentVersion = revision.equipmentVersion;
			outSnapshot.statRevision = revision.statRevision;
			outError.clear();
			return true;
		}

		Cache::Protocol::FInventoryItemSnapshot ToRpcInventoryItem(
			const Database::SInventoryItemMutationRow& source)
		{
			Cache::Protocol::FInventoryItemSnapshot item;
			item.itemInstanceId = source.itemInstanceId;
			item.itemDataId = source.itemDataId;
			item.quantity = source.quantity;
			item.itemDataJson = source.itemDataJson;
			item.equipped = source.equipped;
			item.tradable = source.tradable;
			item.version = source.version;
			item.strStat = source.str;
			item.dexStat = source.dex;
			item.intStat = source.intelligence;
			item.lukStat = source.luk;
			return item;
		}

		Domain::SCachedInventoryItem ToCachedInventoryItem(
			const Database::SPlayerInventoryItemRow& source)
		{
			Domain::SCachedInventoryItem item;
			item.itemInstanceId = source.itemInstanceId;
			item.itemDataId = source.itemDataId;
			item.quantity = source.quantity;
			item.itemDataJson = source.itemDataJson;
			item.equipped = source.equipped;
			item.tradable = source.tradable;
			item.version = source.version;
			return item;
		}

		template <typename TRpc>
		bool IsResponseWithinPayloadLimit(
			const typename TRpc::FResponseArguments& arguments) noexcept
		{
			try
			{
				NetworkLib::Packet::Serialization::FPacketWriter writer;
				return RpcLib::Protocol::WriteRpcArguments(writer, arguments) &&
					   writer.GetBodySize() <= RpcLib::Protocol::kMaxRpcPayloadBytes;
			}
			catch (...)
			{
				return false;
			}
		}
	}

	FPlayerCacheContent::FPlayerCacheContent(
		std::shared_ptr<Foundation::ILogger> logger,
		const ContentsRuntime::Core::FContentInstanceId contentInstanceId,
		const std::uint32_t shardIndex,
		const std::uint32_t shardCount,
		const std::uint64_t maxPacketQueueDepth,
		RpcLib::Session::FRpcSessionRegistry& sessionRegistry,
		RpcLib::Call::FRpcRequestIdGenerator& requestIdGenerator,
		std::atomic<std::uint64_t>& ownerGenerationSequence,
		RpcLib::Transport::FServerRpcTransport& transport,
		Database::SCacheDatabaseConfig databaseConfig,
		std::shared_ptr<const GameData::Character::FCharacterDataTable> characterDataTable,
		std::shared_ptr<const GameData::CharacterLevel::FCharacterLevelDataTable> characterLevelDataTable,
		std::shared_ptr<const GameData::Item::FItemDataTable> itemDataTable,
		std::shared_ptr<const GameData::InventoryPolicy::FInventoryPolicyTable> inventoryPolicyTable,
		std::shared_ptr<const GameData::Currency::FCurrencyDataTable> currencyDataTable,
		std::shared_ptr<const GameData::MailPolicy::FMailPolicyTable> mailPolicyTable,
		std::shared_ptr<const GameData::MailTemplate::FMailTemplateTable> mailTemplateTable,
		SPlayerCachePolicy cachePolicy,
		const bool faultInjectionCreditBeforeDatabaseTransaction,
		const bool faultInjectionCreditAfterCommitDisconnect,
		const std::uint32_t faultInjectionCreditBeforeDatabaseDelayMilliseconds,
		const std::uint32_t faultInjectionCreditAfterCommitDelayMilliseconds)
		: m_logger(std::move(logger))
		, m_contentInstanceId(contentInstanceId)
		, m_shardIndex(shardIndex)
		, m_shardCount(shardCount)
		, m_maxPacketQueueDepth(maxPacketQueueDepth)
		, m_sessionRegistry(sessionRegistry)
		, m_ownerGenerationSequence(ownerGenerationSequence)
		, m_transport(transport)
		, m_rpcCommon(sessionRegistry, m_dispatcher, requestIdGenerator, transport, contentInstanceId)
		, m_databaseConfig(std::move(databaseConfig))
		, m_characterDataTable(std::move(characterDataTable))
		, m_characterLevelDataTable(std::move(characterLevelDataTable))
		, m_itemDataTable(std::move(itemDataTable))
		, m_inventoryPolicyTable(std::move(inventoryPolicyTable))
		, m_currencyDataTable(std::move(currencyDataTable))
		, m_mailPolicyTable(std::move(mailPolicyTable))
		, m_mailTemplateTable(std::move(mailTemplateTable))
		, m_cachePolicy(cachePolicy)
		, m_faultInjectionCreditBeforeDatabaseTransaction(faultInjectionCreditBeforeDatabaseTransaction)
		, m_faultInjectionCreditAfterCommitDisconnect(faultInjectionCreditAfterCommitDisconnect)
		, m_faultInjectionCreditBeforeDatabaseDelayMilliseconds(faultInjectionCreditBeforeDatabaseDelayMilliseconds)
		, m_faultInjectionCreditAfterCommitDelayMilliseconds(faultInjectionCreditAfterCommitDelayMilliseconds)
	{
		if (m_characterDataTable == nullptr || m_characterLevelDataTable == nullptr || m_itemDataTable == nullptr ||
			m_inventoryPolicyTable == nullptr || m_currencyDataTable == nullptr || m_mailPolicyTable == nullptr ||
			m_mailTemplateTable == nullptr)
		{
			throw std::invalid_argument("player cache GameData tables must not be null.");
		}

		const bool pingRegistered = m_rpcCommon.Register<Cache::Protocol::FCachePingRpc>(
			[this](RpcLib::Dispatch::TRpcReply<Cache::Protocol::FCachePingRpc>& reply,
				const std::uint64_t sequence,
				const std::uint64_t userId,
				const std::uint64_t clientTimeUnixMs)
			{
				HandlePing(reply, sequence, userId, clientTimeUnixMs);
			});
		const bool loadUserRegistered = m_rpcCommon.Register<Cache::Protocol::FLoadCacheUserRpc>(
			[this](const RpcLib::Dispatch::FRpcCallContext& context,
				RpcLib::Dispatch::TRpcReply<Cache::Protocol::FLoadCacheUserRpc>& reply,
				const std::uint64_t sequence,
				const std::uint64_t userId)
			{
				HandleLoadUser(context, reply, sequence, userId);
			});
		const bool getInventoryRegistered = m_rpcCommon.Register<Cache::Protocol::FGetInventoryRpc>(
			[this](const RpcLib::Dispatch::FRpcCallContext& context,
				RpcLib::Dispatch::TRpcReply<Cache::Protocol::FGetInventoryRpc>& reply,
				const std::uint64_t userId,
				const std::uint64_t cursorItemInstanceId,
				const std::uint32_t limit)
			{
				HandleGetInventory(context, reply, userId, cursorItemInstanceId, limit);
			});
		const bool getMailListRegistered = m_rpcCommon.Register<Cache::Protocol::FGetMailListRpc>(
			[this](const RpcLib::Dispatch::FRpcCallContext& context,
				RpcLib::Dispatch::TRpcReply<Cache::Protocol::FGetMailListRpc>& reply,
				const std::uint64_t userId,
				const std::uint64_t cursorMailId,
				const std::uint32_t limit)
			{
				HandleGetMailList(context, reply, userId, cursorMailId, limit);
			});
		const bool getMailDetailRegistered = m_rpcCommon.Register<Cache::Protocol::FGetMailDetailRpc>(
			[this](const RpcLib::Dispatch::FRpcCallContext& context,
				RpcLib::Dispatch::TRpcReply<Cache::Protocol::FGetMailDetailRpc>& reply,
				const std::uint64_t userId,
				const std::uint64_t mailId)
			{
				HandleGetMailDetail(context, reply, userId, mailId);
			});
		const bool getCurrencyRegistered = m_rpcCommon.Register<Cache::Protocol::FGetCurrencyRpc>(
			[this](const RpcLib::Dispatch::FRpcCallContext& context,
				RpcLib::Dispatch::TRpcReply<Cache::Protocol::FGetCurrencyRpc>& reply,
				const std::uint64_t userId,
				const std::uint16_t currencyId)
			{
				HandleGetCurrency(context, reply, userId, currencyId);
			});
		const bool getInventoryItemRegistered = m_rpcCommon.Register<Cache::Protocol::FGetInventoryItemRpc>(
			[this](const RpcLib::Dispatch::FRpcCallContext& context,
				RpcLib::Dispatch::TRpcReply<Cache::Protocol::FGetInventoryItemRpc>& reply,
				const std::uint64_t userId,
				const std::uint64_t itemInstanceId)
			{
				HandleGetInventoryItem(context, reply, userId, itemInstanceId);
			});
		const bool creditCurrencyRegistered = m_rpcCommon.Register<Cache::Protocol::FCreditCurrencyRpc>(
			[this](const RpcLib::Dispatch::FRpcCallContext& context,
				RpcLib::Dispatch::TRpcReply<Cache::Protocol::FCreditCurrencyRpc>& reply,
				const std::uint64_t userId,
				const std::uint16_t currencyId,
				const std::uint64_t amount)
			{
				HandleCreditCurrency(context, reply, userId, currencyId, amount);
			});
		const bool grantInventoryItemRegistered = m_rpcCommon.Register<Cache::Protocol::FGrantInventoryItemRpc>(
			[this](const RpcLib::Dispatch::FRpcCallContext& context,
				RpcLib::Dispatch::TRpcReply<Cache::Protocol::FGrantInventoryItemRpc>& reply,
				const std::uint64_t userId,
				const std::uint32_t itemDataId,
				const std::uint32_t quantity,
				const std::uint32_t maxStack,
				const std::uint32_t str,
				const std::uint32_t dex,
				const std::uint32_t intelligence,
				const std::uint32_t luk,
				const bool tradable)
			{
				HandleGrantInventoryItem(context, reply, userId, itemDataId, quantity, maxStack, str, dex, intelligence, luk, tradable);
			});
		const bool claimMailAttachmentRegistered = m_rpcCommon.Register<Cache::Protocol::FClaimMailAttachmentRpc>(
			[this](const RpcLib::Dispatch::FRpcCallContext& context,
				RpcLib::Dispatch::TRpcReply<Cache::Protocol::FClaimMailAttachmentRpc>& reply,
				const std::uint64_t userId,
				const std::uint64_t mailId,
				const std::uint64_t attachmentId)
			{
				HandleClaimMailAttachment(context, reply, userId, mailId, attachmentId);
			});
		const bool consumeInventoryItemRegistered = m_rpcCommon.Register<Cache::Protocol::FConsumeInventoryItemForListingRpc>(
			[this](const RpcLib::Dispatch::FRpcCallContext& context,
				RpcLib::Dispatch::TRpcReply<Cache::Protocol::FConsumeInventoryItemForListingRpc>& reply,
				const std::uint64_t userId,
				const std::uint64_t itemInstanceId,
				const std::uint64_t expectedVersion)
			{
				HandleConsumeInventoryItemForListing(context, reply, userId, itemInstanceId, expectedVersion);
			});
		const bool debitCurrencyRegistered = m_rpcCommon.Register<Cache::Protocol::FDebitCurrencyRpc>(
			[this](const RpcLib::Dispatch::FRpcCallContext& context,
				RpcLib::Dispatch::TRpcReply<Cache::Protocol::FDebitCurrencyRpc>& reply,
				const std::uint64_t userId,
				const std::uint16_t currencyId,
				const std::uint64_t amount)
			{
				HandleDebitCurrency(context, reply, userId, currencyId, amount);
			});
		const bool settleBuyoutRegistered = m_rpcCommon.Register<Cache::Protocol::FSettleBuyoutRpc>(
			[this](const RpcLib::Dispatch::FRpcCallContext& context,
				RpcLib::Dispatch::TRpcReply<Cache::Protocol::FSettleBuyoutRpc>& reply,
				const std::uint64_t buyerUserId,
				const std::uint64_t sellerUserId,
				const std::uint16_t currencyId,
				const std::uint64_t additionalDebit,
				const std::uint64_t buyoutPrice,
				const std::uint64_t itemInstanceId,
				const std::uint32_t itemDataId,
				const std::uint32_t quantity,
				const std::string& itemDataJson)
			{
				HandleSettleBuyout(context,
					reply,
					buyerUserId,
					sellerUserId,
					currencyId,
					additionalDebit,
					buyoutPrice,
					itemInstanceId,
					itemDataId,
					quantity,
					itemDataJson);
			});
		const bool createListingReturnMailRegistered = m_rpcCommon.Register<Cache::Protocol::FCreateListingReturnMailRpc>(
			[this](const RpcLib::Dispatch::FRpcCallContext& context,
				RpcLib::Dispatch::TRpcReply<Cache::Protocol::FCreateListingReturnMailRpc>& reply,
				const std::uint64_t sellerUserId,
				const std::uint64_t itemInstanceId,
				const std::uint32_t itemDataId,
				const std::uint32_t quantity,
				const std::string& itemDataJson)
			{
				HandleCreateListingReturnMail(context, reply, sellerUserId, itemInstanceId, itemDataId, quantity, itemDataJson);
			});
		const bool settleExpirationRegistered = m_rpcCommon.Register<Cache::Protocol::FSettleExpirationRpc>(
			[this](const RpcLib::Dispatch::FRpcCallContext& context,
				RpcLib::Dispatch::TRpcReply<Cache::Protocol::FSettleExpirationRpc>& reply,
				const std::uint64_t primaryUserId,
				const std::uint64_t sellerUserId,
				const std::uint64_t winnerUserId,
				const std::uint16_t currencyId,
				const std::uint64_t finalPrice,
				const std::uint64_t itemInstanceId,
				const std::uint32_t itemDataId,
				const std::uint32_t quantity,
				const std::string& itemDataJson)
			{
				HandleSettleExpiration(context,
					reply,
					primaryUserId,
					sellerUserId,
					winnerUserId,
					currencyId,
					finalPrice,
					itemInstanceId,
					itemDataId,
					quantity,
					itemDataJson);
			});
		const bool getPlayerWorldSnapshotRegistered = m_rpcCommon.Register<Cache::Protocol::FGetPlayerWorldSnapshotRpc>(
			[this](const RpcLib::Dispatch::FRpcCallContext& context,
				RpcLib::Dispatch::TRpcReply<Cache::Protocol::FGetPlayerWorldSnapshotRpc>& reply,
				const std::uint64_t userId,
				const std::uint64_t gameClientSessionId,
				const std::uint64_t ownerGeneration)
			{
				HandleGetPlayerWorldSnapshot(context, reply, userId, gameClientSessionId, ownerGeneration);
			});
		const bool allocatePlayerStatRegistered = m_rpcCommon.Register<Cache::Protocol::FAllocatePlayerStatRpc>(
			[this](const RpcLib::Dispatch::FRpcCallContext& context,
				RpcLib::Dispatch::TRpcReply<Cache::Protocol::FAllocatePlayerStatRpc>& reply,
				const std::uint64_t userId,
				const std::uint64_t gameClientSessionId,
				const std::uint64_t ownerGeneration,
				const std::uint64_t expectedStatVersion,
				const std::uint32_t addStr,
				const std::uint32_t addDex,
				const std::uint32_t addInt,
				const std::uint32_t addLuk)
			{
				HandleAllocatePlayerStat(
					context, reply, userId, gameClientSessionId, ownerGeneration, expectedStatVersion, addStr, addDex, addInt, addLuk);
			});
		const bool grantPlayerExperienceRegistered = m_rpcCommon.Register<Cache::Protocol::FGrantPlayerExperienceRpc>(
			[this](const RpcLib::Dispatch::FRpcCallContext& context,
				RpcLib::Dispatch::TRpcReply<Cache::Protocol::FGrantPlayerExperienceRpc>& reply,
				const std::uint64_t userId,
				const std::uint64_t gameClientSessionId,
				const std::uint64_t ownerGeneration,
				const std::uint64_t expectedProgressVersion,
				const std::uint64_t amount)
			{
				HandleGrantPlayerExperience(context, reply, userId, gameClientSessionId, ownerGeneration, expectedProgressVersion, amount);
			});
		const bool equipPlayerItemRegistered = m_rpcCommon.Register<Cache::Protocol::FEquipPlayerItemRpc>(
			[this](const RpcLib::Dispatch::FRpcCallContext& context,
				RpcLib::Dispatch::TRpcReply<Cache::Protocol::FEquipPlayerItemRpc>& reply,
				const std::uint64_t userId,
				const std::uint64_t gameClientSessionId,
				const std::uint64_t ownerGeneration,
				const std::uint64_t itemInstanceId,
				const std::uint64_t expectedItemVersion,
				const std::uint64_t expectedStatRevision,
				const std::uint64_t expectedEquipmentVersion)
			{
				HandleEquipPlayerItem(context,
					reply,
					userId,
					gameClientSessionId,
					ownerGeneration,
					itemInstanceId,
					expectedItemVersion,
					expectedStatRevision,
					expectedEquipmentVersion);
			});
		const bool unequipPlayerItemRegistered = m_rpcCommon.Register<Cache::Protocol::FUnequipPlayerItemRpc>(
			[this](const RpcLib::Dispatch::FRpcCallContext& context,
				RpcLib::Dispatch::TRpcReply<Cache::Protocol::FUnequipPlayerItemRpc>& reply,
				const std::uint64_t userId,
				const std::uint64_t gameClientSessionId,
				const std::uint64_t ownerGeneration,
				const std::uint64_t itemInstanceId,
				const std::uint64_t expectedItemVersion,
				const std::uint64_t expectedStatRevision,
				const std::uint64_t expectedEquipmentVersion)
			{
				HandleUnequipPlayerItem(context,
					reply,
					userId,
					gameClientSessionId,
					ownerGeneration,
					itemInstanceId,
					expectedItemVersion,
					expectedStatRevision,
					expectedEquipmentVersion);
			});
		const bool equipPlayerItemV2Registered = m_rpcCommon.Register<Cache::Protocol::FEquipPlayerItemV2Rpc>(
			[this](const RpcLib::Dispatch::FRpcCallContext& context,
				RpcLib::Dispatch::TRpcReply<Cache::Protocol::FEquipPlayerItemV2Rpc>& reply,
				const std::uint64_t userId,
				const std::uint64_t gameClientSessionId,
				const std::uint64_t ownerGeneration,
				const std::uint64_t itemInstanceId,
				const std::uint64_t expectedItemVersion,
				const std::uint64_t expectedStatRevision,
				const std::uint64_t expectedEquipmentVersion)
			{
				HandleEquipPlayerItemV2(context,
					reply,
					userId,
					gameClientSessionId,
					ownerGeneration,
					itemInstanceId,
					expectedItemVersion,
					expectedStatRevision,
					expectedEquipmentVersion);
			});
		const bool unequipPlayerItemV2Registered = m_rpcCommon.Register<Cache::Protocol::FUnequipPlayerItemV2Rpc>(
			[this](const RpcLib::Dispatch::FRpcCallContext& context,
				RpcLib::Dispatch::TRpcReply<Cache::Protocol::FUnequipPlayerItemV2Rpc>& reply,
				const std::uint64_t userId,
				const std::uint64_t gameClientSessionId,
				const std::uint64_t ownerGeneration,
				const std::uint64_t itemInstanceId,
				const std::uint64_t expectedItemVersion,
				const std::uint64_t expectedStatRevision,
				const std::uint64_t expectedEquipmentVersion)
			{
				HandleUnequipPlayerItemV2(context,
					reply,
					userId,
					gameClientSessionId,
					ownerGeneration,
					itemInstanceId,
					expectedItemVersion,
					expectedStatRevision,
					expectedEquipmentVersion);
			});
		const bool pingNotificationRegistered = m_rpcCommon.RegisterNotification<Cache::Protocol::FCachePingNoti>(
			[this](const RpcLib::Dispatch::FRpcCallContext& context,
				const std::uint64_t sequence,
				const std::uint64_t userId,
				const std::uint64_t clientTimeUnixMs)
			{
				HandlePingNotification(context, sequence, userId, clientTimeUnixMs);
			});
		const bool enterUserRegistered = m_rpcCommon.Register<ServerProtocol::UserPresence::FEnterUserRpc>(
			[this](const RpcLib::Dispatch::FRpcCallContext& context,
				RpcLib::Dispatch::TRpcReply<ServerProtocol::UserPresence::FEnterUserRpc>& reply,
				const std::uint64_t userId,
				const std::uint64_t localClientSessionId)
			{
				HandleEnterUser(context, reply, userId, localClientSessionId);
			});
		const bool leaveUserRegistered = m_rpcCommon.Register<ServerProtocol::UserPresence::FLeaveUserRpc>(
			[this](const RpcLib::Dispatch::FRpcCallContext& context,
				RpcLib::Dispatch::TRpcReply<ServerProtocol::UserPresence::FLeaveUserRpc>& reply,
				const std::uint64_t userId,
				const std::uint64_t localClientSessionId,
				const std::uint64_t ownerGeneration)
			{
				HandleLeaveUser(context, reply, userId, localClientSessionId, ownerGeneration);
			});
		const bool renewUserRegistered = m_rpcCommon.Register<ServerProtocol::UserPresence::FRenewUserRpc>(
			[this](const RpcLib::Dispatch::FRpcCallContext& context,
				RpcLib::Dispatch::TRpcReply<ServerProtocol::UserPresence::FRenewUserRpc>& reply,
				const std::uint64_t userId,
				const std::uint64_t localClientSessionId,
				const std::uint64_t ownerGeneration)
			{
				HandleRenewUser(context, reply, userId, localClientSessionId, ownerGeneration);
			});
		if (!pingRegistered || !loadUserRegistered || !getInventoryRegistered || !getMailListRegistered || !getMailDetailRegistered ||
			!getCurrencyRegistered || !getInventoryItemRegistered || !creditCurrencyRegistered || !grantInventoryItemRegistered ||
			!claimMailAttachmentRegistered || !consumeInventoryItemRegistered || !debitCurrencyRegistered || !settleBuyoutRegistered ||
			!createListingReturnMailRegistered || !settleExpirationRegistered || !getPlayerWorldSnapshotRegistered ||
			!allocatePlayerStatRegistered || !grantPlayerExperienceRegistered || !equipPlayerItemRegistered ||
			!unequipPlayerItemRegistered || !equipPlayerItemV2Registered || !unequipPlayerItemV2Registered || !pingNotificationRegistered ||
			!enterUserRegistered || !leaveUserRegistered || !renewUserRegistered)
		{
			throw std::runtime_error("Cache RPC registration failed.");
		}

		m_nextMaintenanceAt = std::chrono::steady_clock::now() + m_cachePolicy.maintenanceInterval;
	}

	ContentsRuntime::Core::FContentId FPlayerCacheContent::GetContentId() const noexcept
	{
		return kPlayerCacheContentId;
	}

	ContentsRuntime::Core::FContentInstanceId FPlayerCacheContent::GetContentInstanceId() const noexcept
	{
		return m_contentInstanceId;
	}

	std::uint64_t FPlayerCacheContent::GetMaxPacketQueueDepth() const noexcept
	{
		return m_maxPacketQueueDepth;
	}

	void FPlayerCacheContent::OnEnter(
		const std::uint64_t,
		const std::uint64_t,
		ContentsRuntime::Bridge::IContentBridge&)
	{
	}

	void FPlayerCacheContent::OnLeave(
		const std::uint64_t,
		const std::uint64_t,
		ContentsRuntime::Bridge::IContentBridge&)
	{
	}

	void FPlayerCacheContent::OnPacket(
		const std::uint64_t sessionId,
		const std::uint64_t,
		const std::uint16_t opcode,
		const std::span<const char> payload,
		ContentsRuntime::Bridge::IContentBridge& bridge)
	{
		if (opcode == static_cast<std::uint16_t>(RpcLib::Protocol::ERpcWireOpcode::Response))
		{
			RpcLib::Protocol::FRpcResponse response;
			if (!RpcLib::Protocol::DeserializeRpcResponse(payload, response))
			{
				Log(Foundation::ELogLevel::Warn, "player cache response deserialize failed. sessionId={}", sessionId);
				return;
			}

			const auto completionResult = m_rpcCommon.ProcessResponse(sessionId, response);
			if (completionResult != RpcLib::Protocol::ERpcCompletionResult::Completed)
			{
				Log(Foundation::ELogLevel::Warn,
					"RPC response completion failed. sessionId={} requestId={} result={}",
					sessionId,
					response.requestId,
					static_cast<std::uint8_t>(completionResult));
			}
			return;
		}

		if (opcode == static_cast<std::uint16_t>(RpcLib::Protocol::ERpcWireOpcode::Notification))
		{
			RpcLib::Protocol::FRpcNotification notification;
			if (!RpcLib::Protocol::DeserializeRpcNotification(payload, notification))
			{
				Log(Foundation::ELogLevel::Warn, "player cache notification deserialize failed. sessionId={}", sessionId);
				return;
			}

			if (GetPlayerCacheShardIndex(notification.routingKey, m_shardCount) != m_shardIndex)
			{
				Log(Foundation::ELogLevel::Error,
					"RPC notification reached wrong shard. serviceId={} methodId={} routingKey={} shardIndex={}",
					notification.serviceId,
					notification.methodId,
					notification.routingKey,
					m_shardIndex);
				return;
			}

			const auto dispatchResult = m_rpcCommon.DispatchNotification(sessionId, notification);
			if (dispatchResult != RpcLib::Protocol::ERpcNotificationDispatchResult::Dispatched)
			{
				Log(Foundation::ELogLevel::Warn,
					"RPC notification dispatch failed. sessionId={} serviceId={} methodId={} result={}",
					sessionId,
					notification.serviceId,
					notification.methodId,
					static_cast<std::uint8_t>(dispatchResult));
			}
			return;
		}

		if (opcode != static_cast<std::uint16_t>(RpcLib::Protocol::ERpcWireOpcode::Request))
		{
			Log(Foundation::ELogLevel::Warn, "player cache received unexpected opcode. opcode={}", opcode);
			return;
		}

		const std::shared_ptr<RpcLib::Session::FRpcSession> session = m_sessionRegistry.Find(sessionId);
		if (session == nullptr || !session->IsReady())
		{
			Log(Foundation::ELogLevel::Warn, "player cache rejected a non-ready RPC session. sessionId={}", sessionId);
			return;
		}

		RpcLib::Protocol::FRpcRequest request;
		if (!RpcLib::Protocol::DeserializeRpcRequest(payload, request))
		{
			Log(Foundation::ELogLevel::Warn, "player cache request deserialize failed. sessionId={}", sessionId);
			return;
		}

		if (GetPlayerCacheShardIndex(request.routingKey, m_shardCount) != m_shardIndex)
		{
			Log(Foundation::ELogLevel::Error,
				"RPC request reached wrong shard. requestId={} routingKey={} shardIndex={}",
				request.requestId,
				request.routingKey,
				m_shardIndex);
			return;
		}

		const RpcLib::Protocol::FRpcResponse response = m_rpcCommon.DispatchRequest(sessionId, request);
		if (m_disconnectBeforeResponseRequestId.has_value() && *m_disconnectBeforeResponseRequestId == request.requestId)
		{
			m_disconnectBeforeResponseRequestId.reset();
			Log(Foundation::ELogLevel::Error,
				"Cache RPC fault injected. operation=CreditCurrency "
				"stage=AfterGameDB.Commit.BeforeRpcResponse action=Disconnect faultInjected=true rpcRequestId={} sessionId={}",
				request.requestId,
				sessionId);
			bridge.DisconnectSession(sessionId);
			return;
		}
		if (!m_transport.SendResponse(sessionId, response))
		{
			Log(Foundation::ELogLevel::Error, "RPC response send failed. requestId={}", request.requestId);
		}
	}

	void FPlayerCacheContent::OnFrame(
		const int,
		ContentsRuntime::Bridge::IContentBridge&)
	{
		const auto now = std::chrono::steady_clock::now();
		m_rpcCommon.ProcessTimeouts(now);
		if (now < m_nextMaintenanceAt)
		{
			return;
		}

		m_nextMaintenanceAt = now + m_cachePolicy.maintenanceInterval;
		RunMaintenance(now);
	}

	void FPlayerCacheContent::HandlePing(
		RpcLib::Dispatch::TRpcReply<Cache::Protocol::FCachePingRpc>& reply,
		const std::uint64_t sequence,
		const std::uint64_t userId,
		const std::uint64_t clientTimeUnixMs)
	{
		const std::uint64_t serverTimeUnixMs = GetUnixTimeMilliseconds();
		const std::uint32_t workerThreadId = static_cast<std::uint32_t>(GetCurrentThreadId());
		reply.Send(sequence, userId, clientTimeUnixMs, serverTimeUnixMs, m_shardIndex, m_shardCount, m_contentInstanceId, workerThreadId);

		Log(Foundation::ELogLevel::Info,
			"CachePing handled. sequence={} userId={} shardIndex={} contentInstanceId={} workerThreadId={}",
			sequence,
			userId,
			m_shardIndex,
			m_contentInstanceId,
			workerThreadId);
	}

	void FPlayerCacheContent::HandlePingNotification(
		const RpcLib::Dispatch::FRpcCallContext& context,
		const std::uint64_t sequence,
		const std::uint64_t userId,
		const std::uint64_t clientTimeUnixMs)
	{
		if (context.routingKey != userId)
		{
			Log(Foundation::ELogLevel::Warn,
				"CachePing notification routing mismatch. sequence={} userId={} routingKey={}",
				sequence,
				userId,
				context.routingKey);
			return;
		}

		Log(Foundation::ELogLevel::Info,
			"CachePing notification handled. sequence={} userId={} clientTimeUnixMs={} routingKey={} peerServerType={} "
			"peerServerInstanceId={}",
			sequence,
			userId,
			clientTimeUnixMs,
			context.routingKey,
			static_cast<std::uint16_t>(context.peerServerType),
			context.peerServerInstanceId);
	}

	void FPlayerCacheContent::HandleLoadUser(
		const RpcLib::Dispatch::FRpcCallContext& context,
		RpcLib::Dispatch::TRpcReply<Cache::Protocol::FLoadCacheUserRpc>& reply,
		const std::uint64_t sequence,
		const std::uint64_t userId)
	{
		bool loadedFromDatabase = false;
		Cache::Protocol::ECacheUserLoadResult result = Cache::Protocol::ECacheUserLoadResult::Success;
		std::string error;
		Domain::FCacheUser* user = nullptr;
		if (context.routingKey != userId)
		{
			result = Cache::Protocol::ECacheUserLoadResult::InvalidUserId;
			error = "RPC routingKey and payload userId do not match.";
		}
		else
		{
			user = GetOrLoadUser(userId, loadedFromDatabase, result, error);
		}

		std::uint32_t currencyCount = 0;
		std::uint32_t inventoryItemCount = 0;
		std::uint64_t loadedAtUnixMs = 0;
		if (user != nullptr)
		{
			currencyCount = static_cast<std::uint32_t>(user->GetCurrencyCount());
			inventoryItemCount = static_cast<std::uint32_t>(user->GetInventoryItemCount());
			loadedAtUnixMs = static_cast<std::uint64_t>(
				std::chrono::duration_cast<std::chrono::milliseconds>(user->GetLoadedAt().time_since_epoch()).count());
		}

		reply.Send(sequence,
			userId,
			result,
			static_cast<std::uint8_t>(loadedFromDatabase ? 1 : 0),
			m_shardIndex,
			m_shardCount,
			m_contentInstanceId,
			currencyCount,
			inventoryItemCount,
			loadedAtUnixMs);

		if (result != Cache::Protocol::ECacheUserLoadResult::Success)
		{
			Log(Foundation::ELogLevel::Error,
				"GetOrLoadUser failed. userId={} shardIndex={} result={} error={}",
				userId,
				m_shardIndex,
				static_cast<std::uint8_t>(result),
				error);
		}
	}

	void FPlayerCacheContent::HandleGetInventory(
		const RpcLib::Dispatch::FRpcCallContext& context,
		RpcLib::Dispatch::TRpcReply<Cache::Protocol::FGetInventoryRpc>& reply,
		const std::uint64_t userId,
		const std::uint64_t cursorItemInstanceId,
		const std::uint32_t limit)
	{
		using Cache::Protocol::ECacheQueryResult;
		using Cache::Protocol::FGetInventoryRpc;
		using Cache::Protocol::FInventoryItem;

		ECacheQueryResult result = ECacheQueryResult::Success;
		std::vector<FInventoryItem> items;
		std::string error;
		if (!IsAuthorizedDataQueryCaller(context) || userId == 0 || context.routingKey != userId || limit == 0 ||
			limit > m_inventoryPolicyTable->Get().inventoryListPageSize)
		{
			result = ECacheQueryResult::InvalidArgument;
			error = "GetInventory received an unauthorized caller or invalid argument.";
		}
		else
		{
			bool loadedFromDatabase = false;
			Cache::Protocol::ECacheUserLoadResult loadResult = Cache::Protocol::ECacheUserLoadResult::Success;
			Domain::FCacheUser* user = GetOrLoadUser(userId, loadedFromDatabase, loadResult, error);
			if (user == nullptr)
			{
				result = ToQueryResult(loadResult);
			}
			else
			{
				std::vector<std::uint64_t> itemIds;
				itemIds.reserve(user->GetInventoryItemCount());
				for (const auto& [itemInstanceId, item] : user->GetInventoryItems())
				{
					(void)item;
					if (cursorItemInstanceId == 0 || itemInstanceId < cursorItemInstanceId)
					{
						itemIds.push_back(itemInstanceId);
					}
				}

				std::sort(itemIds.begin(), itemIds.end(), std::greater<>());
				if (itemIds.size() > limit)
				{
					itemIds.resize(limit);
				}

				items.reserve(itemIds.size());
				for (const std::uint64_t itemInstanceId : itemIds)
				{
					const Domain::SCachedInventoryItem& cachedItem = user->GetInventoryItems().at(itemInstanceId);
					FInventoryItem item;
					item.itemInstanceId = cachedItem.itemInstanceId;
					item.itemDataId = cachedItem.itemDataId;
					item.quantity = cachedItem.quantity;
					item.itemDataJson = cachedItem.itemDataJson;
					item.equipped = cachedItem.equipped;
					item.tradable = cachedItem.tradable;
					item.version = cachedItem.version;
					items.push_back(std::move(item));
				}
			}
		}

		const FGetInventoryRpc::FResponseArguments responseArguments(result, items);
		if (!IsResponseWithinPayloadLimit<FGetInventoryRpc>(responseArguments))
		{
			result = ECacheQueryResult::DatabaseError;
			items.clear();
			error = "GetInventory response exceeds the RPC payload limit.";
		}
		reply.Send(result, std::move(items));

		if (result != ECacheQueryResult::Success)
		{
			Log(Foundation::ELogLevel::Warn,
				"GetInventory failed. userId={} result={} error={}",
				userId,
				static_cast<std::uint8_t>(result),
				error);
		}
	}

	void FPlayerCacheContent::HandleGetMailList(
		const RpcLib::Dispatch::FRpcCallContext& context,
		RpcLib::Dispatch::TRpcReply<Cache::Protocol::FGetMailListRpc>& reply,
		const std::uint64_t userId,
		const std::uint64_t cursorMailId,
		const std::uint32_t limit)
	{
		using Cache::Protocol::ECacheQueryResult;
		using Cache::Protocol::FGetMailListRpc;
		using Cache::Protocol::FMailSummary;

		ECacheQueryResult result = ECacheQueryResult::Success;
		std::vector<FMailSummary> mails;
		std::string error;
		if (!IsAuthorizedDataQueryCaller(context) || userId == 0 || context.routingKey != userId || limit == 0 ||
			limit > m_mailPolicyTable->Get().mailListPageSize)
		{
			result = ECacheQueryResult::InvalidArgument;
			error = "GetMailList received an unauthorized caller or invalid argument.";
		}
		else if (!m_databaseConfig.enabled)
		{
			result = ECacheQueryResult::DatabaseError;
			error = "GameDB is disabled.";
		}
		else
		{
			auto& databaseContext = Database::FContentThreadDbContext::Get(m_databaseConfig);
			std::vector<Database::SMailSummary> databaseMails;
			bool usedPrimary = false;
			if (!databaseContext.ExecuteGameReadWithPrimaryFallback(
					false,
					[&](Connector::MySql::FMySqlConnection& connection, std::string& operationError)
					{
						return Database::FPlayerCacheRepository(connection)
							.GetMailList(userId, cursorMailId, limit, databaseMails, operationError);
					},
					usedPrimary,
					error))
			{
				result = ECacheQueryResult::DatabaseError;
			}
			else
			{
				mails.reserve(databaseMails.size());
				for (auto& databaseMail : databaseMails)
				{
					FMailSummary mail;
					mail.mailId = databaseMail.mailId;
					mail.mailType = databaseMail.mailType;
					mail.subject = std::move(databaseMail.subject);
					mail.state = databaseMail.state;
					mail.expiresAtUnixMs = databaseMail.expiresAtUnixMs;
					mail.createdAtUnixMs = databaseMail.createdAtUnixMs;
					mails.push_back(std::move(mail));
				}

				if (usedPrimary)
				{
					Log(Foundation::ELogLevel::Warn, "GetMailList replica unavailable; primary fallback succeeded. userId={}", userId);
				}
			}
		}

		const FGetMailListRpc::FResponseArguments responseArguments(result, mails);
		if (!IsResponseWithinPayloadLimit<FGetMailListRpc>(responseArguments))
		{
			result = ECacheQueryResult::DatabaseError;
			mails.clear();
			error = "GetMailList response exceeds the RPC payload limit.";
		}
		reply.Send(result, std::move(mails));

		if (result != ECacheQueryResult::Success)
		{
			Log(Foundation::ELogLevel::Warn,
				"GetMailList failed. userId={} result={} error={}",
				userId,
				static_cast<std::uint8_t>(result),
				error);
		}
	}

	void FPlayerCacheContent::HandleGetMailDetail(
		const RpcLib::Dispatch::FRpcCallContext& context,
		RpcLib::Dispatch::TRpcReply<Cache::Protocol::FGetMailDetailRpc>& reply,
		const std::uint64_t userId,
		const std::uint64_t mailId)
	{
		using Cache::Protocol::ECacheQueryResult;
		using Cache::Protocol::FGetMailDetailRpc;
		using Cache::Protocol::FMailAttachment;
		using Cache::Protocol::FMailDetail;

		ECacheQueryResult result = ECacheQueryResult::Success;
		FMailDetail mail;
		std::string error;
		if (!IsAuthorizedDataQueryCaller(context) || userId == 0 || mailId == 0 || context.routingKey != userId)
		{
			result = ECacheQueryResult::InvalidArgument;
			error = "GetMailDetail received an unauthorized caller or invalid argument.";
		}
		else if (!m_databaseConfig.enabled)
		{
			result = ECacheQueryResult::DatabaseError;
			error = "GameDB is disabled.";
		}
		else
		{
			auto& databaseContext = Database::FContentThreadDbContext::Get(m_databaseConfig);
			Database::SMailDetail databaseMail;
			bool found = false;
			bool usedPrimary = false;
			if (!databaseContext.ExecuteGameReadWithPrimaryFallback(
					false,
					[&](Connector::MySql::FMySqlConnection& connection, std::string& operationError)
					{
						return Database::FPlayerCacheRepository(connection)
							.GetMailDetail(userId, mailId, databaseMail, found, operationError);
					},
					usedPrimary,
					error))
			{
				result = ECacheQueryResult::DatabaseError;
			}
			else if (!found)
			{
				result = ECacheQueryResult::NotFound;
			}
			else
			{
				mail.mailId = databaseMail.mailId;
				mail.mailType = databaseMail.mailType;
				mail.subject = std::move(databaseMail.subject);
				mail.body = std::move(databaseMail.body);
				mail.state = databaseMail.state;
				mail.expiresAtUnixMs = databaseMail.expiresAtUnixMs;
				mail.attachments.reserve(databaseMail.attachments.size());
				for (auto& databaseAttachment : databaseMail.attachments)
				{
					FMailAttachment attachment;
					attachment.attachmentId = databaseAttachment.attachmentId;
					attachment.attachmentType = databaseAttachment.attachmentType;
					attachment.itemInstanceId = databaseAttachment.itemInstanceId;
					attachment.itemDataId = databaseAttachment.itemDataId;
					attachment.quantity = databaseAttachment.quantity;
					attachment.itemDataJson = std::move(databaseAttachment.itemDataJson);
					attachment.currencyId = databaseAttachment.currencyId;
					attachment.currencyAmount = databaseAttachment.currencyAmount;
					attachment.state = databaseAttachment.state;
					mail.attachments.push_back(std::move(attachment));
				}

				if (usedPrimary)
				{
					Log(Foundation::ELogLevel::Warn,
						"GetMailDetail replica unavailable; primary fallback succeeded. userId={} mailId={}",
						userId,
						mailId);
				}
			}
		}

		const FGetMailDetailRpc::FResponseArguments responseArguments(result, mail);
		if (!IsResponseWithinPayloadLimit<FGetMailDetailRpc>(responseArguments))
		{
			result = ECacheQueryResult::DatabaseError;
			mail = {};
			error = "GetMailDetail response exceeds the RPC payload limit.";
		}
		reply.Send(result, std::move(mail));

		if (result != ECacheQueryResult::Success && result != ECacheQueryResult::NotFound)
		{
			Log(Foundation::ELogLevel::Warn,
				"GetMailDetail failed. userId={} mailId={} result={} error={}",
				userId,
				mailId,
				static_cast<std::uint8_t>(result),
				error);
		}
	}

	void FPlayerCacheContent::HandleGetCurrency(
		const RpcLib::Dispatch::FRpcCallContext& context,
		RpcLib::Dispatch::TRpcReply<Cache::Protocol::FGetCurrencyRpc>& reply,
		const std::uint64_t userId,
		const std::uint16_t currencyId)
	{
		using Cache::Protocol::ECacheQueryResult;
		Cache::Protocol::FCurrencyBalance balance;
		ECacheQueryResult result = ECacheQueryResult::Success;
		std::string error;
		if (!IsAuthorizedDataQueryCaller(context) || userId == 0 || currencyId == 0 || context.routingKey != userId)
		{
			result = ECacheQueryResult::InvalidArgument;
		}
		else
		{
			bool loadedFromDatabase = false;
			Cache::Protocol::ECacheUserLoadResult loadResult = Cache::Protocol::ECacheUserLoadResult::Success;
			Domain::FCacheUser* user = GetOrLoadUser(userId, loadedFromDatabase, loadResult, error);
			if (user == nullptr)
			{
				result = ToQueryResult(loadResult);
			}
			else if (const auto it = user->GetCurrencies().find(currencyId); it == user->GetCurrencies().end())
			{
				result = ECacheQueryResult::NotFound;
			}
			else
			{
				balance.currencyId = it->second.currencyId;
				balance.amount = it->second.amount;
				balance.version = it->second.version;
			}
		}

		reply.Send(result, balance);
	}

	void FPlayerCacheContent::HandleGetInventoryItem(
		const RpcLib::Dispatch::FRpcCallContext& context,
		RpcLib::Dispatch::TRpcReply<Cache::Protocol::FGetInventoryItemRpc>& reply,
		const std::uint64_t userId,
		const std::uint64_t itemInstanceId)
	{
		using Cache::Protocol::ECacheQueryResult;
		Cache::Protocol::FInventoryItemSnapshot item;
		ECacheQueryResult result = ECacheQueryResult::Success;
		std::string error;
		if (!IsAuthorizedDataQueryCaller(context) || userId == 0 || itemInstanceId == 0 || context.routingKey != userId)
		{
			result = ECacheQueryResult::InvalidArgument;
		}
		else
		{
			bool loadedFromDatabase = false;
			Cache::Protocol::ECacheUserLoadResult loadResult = Cache::Protocol::ECacheUserLoadResult::Success;
			Domain::FCacheUser* user = GetOrLoadUser(userId, loadedFromDatabase, loadResult, error);
			if (user == nullptr)
			{
				result = ToQueryResult(loadResult);
			}
			else if (const auto it = user->GetInventoryItems().find(itemInstanceId); it == user->GetInventoryItems().end())
			{
				result = ECacheQueryResult::NotFound;
			}
			else if (!ToRpcInventoryItem(it->second, item))
			{
				result = ECacheQueryResult::DatabaseError;
				error = "cached inventory item JSON is invalid.";
			}
		}

		reply.Send(result, item);
		if (result == ECacheQueryResult::DatabaseError)
		{
			Log(Foundation::ELogLevel::Error,
				"GetInventoryItem failed. userId={} itemInstanceId={} error={}",
				userId,
				itemInstanceId,
				error);
		}
	}

	void FPlayerCacheContent::HandleCreditCurrency(
		const RpcLib::Dispatch::FRpcCallContext& context,
		RpcLib::Dispatch::TRpcReply<Cache::Protocol::FCreditCurrencyRpc>& reply,
		const std::uint64_t userId,
		const std::uint16_t currencyId,
		const std::uint64_t amount)
	{
		using Cache::Protocol::ECacheCommandResult;
		Cache::Protocol::FCurrencyBalance balance;
		ECacheCommandResult result = ECacheCommandResult::Success;
		std::string error;
		const GameData::Currency::SCurrencyData* currencyData = m_currencyDataTable->Find(currencyId);
		if (!IsAuthorizedDataCommandCaller(context) || userId == 0 || currencyId == 0 || amount == 0 || context.routingKey != userId)
		{
			result = ECacheCommandResult::InvalidArgument;
		}
		else if (currencyData == nullptr)
		{
			result = ECacheCommandResult::InvalidArgument;
			error = "CreditCurrency received an unknown currency ID.";
		}
		else if (!m_databaseConfig.enabled)
		{
			result = ECacheCommandResult::DatabaseError;
			error = "GameDB is disabled.";
		}
		else if (m_faultInjectionCreditBeforeDatabaseTransaction)
		{
			result = ECacheCommandResult::DatabaseError;
			error = "fault injection requested before the GameDB currency-credit transaction.";
			Log(Foundation::ELogLevel::Error,
				"Cache RPC fault injected. operation=CreditCurrency "
				"stage=BeforeGameDB.Transaction action=Fail faultInjected=true rpcRequestId={} userId={} currencyId={} amount={}",
				context.requestId,
				userId,
				currencyId,
				amount);
		}
		else
		{
			if (m_faultInjectionCreditBeforeDatabaseDelayMilliseconds != 0)
			{
				Log(Foundation::ELogLevel::Error,
					"Cache RPC fault injected. operation=CreditCurrency "
					"stage=BeforeGameDB.Transaction action=Delay faultInjected=true rpcRequestId={} userId={} currencyId={} "
					"amount={} delayMs={}",
					context.requestId,
					userId,
					currencyId,
					amount,
					m_faultInjectionCreditBeforeDatabaseDelayMilliseconds);
				std::this_thread::sleep_for(std::chrono::milliseconds(m_faultInjectionCreditBeforeDatabaseDelayMilliseconds));
			}

			auto& databaseContext = Database::FContentThreadDbContext::Get(m_databaseConfig);
			Connector::MySql::FMySqlConnection* connection = databaseContext.GetGamePrimary(error);
			if (connection == nullptr)
			{
				result = ECacheCommandResult::DatabaseError;
			}
			else
			{
				Connector::MySql::FMySqlTransaction transaction(*connection);
				Database::SPlayerCurrencyRow currency;
				if (!transaction.Begin(error))
				{
					result = ECacheCommandResult::DatabaseError;
				}
				else if (!Database::FPlayerCacheRepository(*connection)
							 .CreditCurrency(userId, currencyId, amount, currencyData->maxAmount, currency, error))
				{
					transaction.Rollback();
					result = ToCommandResult(error);
				}
				else if (!transaction.Commit(error))
				{
					if (Domain::FCacheUser* user = FindUser(userId); user != nullptr)
					{
						user->InvalidatePlayerData(std::chrono::steady_clock::now());
					}
					result = ECacheCommandResult::OutcomeUnknown;
				}
				else
				{
					balance.currencyId = currency.currencyId;
					balance.amount = currency.amount;
					balance.version = currency.version;
					if (Domain::FCacheUser* user = FindUser(userId); user != nullptr && user->IsDataValid())
					{
						try
						{
							user->UpsertCurrency(Domain::SCachedCurrency{currency.currencyId, currency.amount, currency.version},
								std::chrono::steady_clock::now());
						}
						catch (...)
						{
							user->InvalidatePlayerData(std::chrono::steady_clock::now());
						}
					}
					if (m_faultInjectionCreditAfterCommitDelayMilliseconds != 0)
					{
						Log(Foundation::ELogLevel::Error,
							"Cache RPC fault injected. operation=CreditCurrency "
							"stage=AfterGameDB.Commit.BeforeRpcResponse action=Delay faultInjected=true rpcRequestId={} userId={} "
							"currencyId={} amount={} delayMs={}",
							context.requestId,
							userId,
							currencyId,
							amount,
							m_faultInjectionCreditAfterCommitDelayMilliseconds);
						std::this_thread::sleep_for(std::chrono::milliseconds(m_faultInjectionCreditAfterCommitDelayMilliseconds));
					}
					if (m_faultInjectionCreditAfterCommitDisconnect)
					{
						m_disconnectBeforeResponseRequestId = context.requestId;
					}
				}
			}
		}

		reply.Send(result, balance);
		Log(result == ECacheCommandResult::Success ? Foundation::ELogLevel::Info : Foundation::ELogLevel::Warn,
			"Cache mutation completed. operation=CreditCurrency rpcRequestId={} userId={} currencyId={} amount={} result={} balance={} "
			"version={} error={}",
			context.requestId,
			userId,
			currencyId,
			amount,
			static_cast<std::uint8_t>(result),
			balance.amount,
			balance.version,
			error);
	}

	void FPlayerCacheContent::HandleGrantInventoryItem(
		const RpcLib::Dispatch::FRpcCallContext& context,
		RpcLib::Dispatch::TRpcReply<Cache::Protocol::FGrantInventoryItemRpc>& reply,
		const std::uint64_t userId,
		const std::uint32_t itemDataId,
		const std::uint32_t quantity,
		const std::uint32_t maxStack,
		const std::uint32_t str,
		const std::uint32_t dex,
		const std::uint32_t intelligence,
		const std::uint32_t luk,
		const bool tradable)
	{
		using Cache::Protocol::ECacheCommandResult;
		Cache::Protocol::FInventoryItemSnapshot item;
		ECacheCommandResult result = ECacheCommandResult::Success;
		std::string error;
		const GameData::Item::SItemTemplate* itemTemplate = m_itemDataTable->Find(itemDataId);
		if (!IsAuthorizedDataCommandCaller(context) || userId == 0 || itemDataId == 0 || quantity == 0 || maxStack == 0 ||
			quantity > maxStack || context.routingKey != userId)
		{
			result = ECacheCommandResult::InvalidArgument;
		}
		else if (itemTemplate == nullptr)
		{
			result = ECacheCommandResult::InvalidArgument;
			error = "GrantInventoryItem received an unknown item data ID.";
		}
		else if (maxStack != itemTemplate->maxStack || tradable != itemTemplate->tradable)
		{
			result = ECacheCommandResult::InvalidArgument;
			error = "GrantInventoryItem received item policy values that do not match GameData.";
		}
		else if (!m_databaseConfig.enabled)
		{
			result = ECacheCommandResult::DatabaseError;
			error = "GameDB is disabled.";
		}
		else
		{
			auto& databaseContext = Database::FContentThreadDbContext::Get(m_databaseConfig);
			Connector::MySql::FMySqlConnection* connection = databaseContext.GetGamePrimary(error);
			if (connection == nullptr)
			{
				result = ECacheCommandResult::DatabaseError;
			}
			else
			{
				Connector::MySql::FMySqlTransaction transaction(*connection);
				Database::SInventoryItemMutationRow databaseItem;
				if (!transaction.Begin(error))
				{
					result = ECacheCommandResult::DatabaseError;
				}
				else if (!Database::FPlayerCacheRepository(*connection)
							 .CreateInventoryItem(userId,
								 itemDataId,
								 quantity,
								 maxStack,
								 m_inventoryPolicyTable->Get().maxInventorySlots,
								 str,
								 dex,
								 intelligence,
								 luk,
								 tradable,
								 databaseItem,
								 error))
				{
					transaction.Rollback();
					result = ToCommandResult(error);
				}
				else if (!transaction.Commit(error))
				{
					if (Domain::FCacheUser* user = FindUser(userId); user != nullptr)
					{
						user->InvalidatePlayerData(std::chrono::steady_clock::now());
					}
					result = ECacheCommandResult::OutcomeUnknown;
				}
				else
				{
					item = ToRpcInventoryItem(databaseItem);
					if (Domain::FCacheUser* user = FindUser(userId); user != nullptr && user->IsDataValid())
					{
						try
						{
							user->UpsertInventoryItem(ToCachedInventoryItem(databaseItem), std::chrono::steady_clock::now());
						}
						catch (...)
						{
							user->InvalidatePlayerData(std::chrono::steady_clock::now());
						}
					}
				}
			}
		}

		reply.Send(result, item);
		Log(result == ECacheCommandResult::Success ? Foundation::ELogLevel::Info : Foundation::ELogLevel::Warn,
			"Cache mutation completed. operation=GrantInventoryItem userId={} itemDataId={} itemInstanceId={} result={} error={}",
			userId,
			itemDataId,
			item.itemInstanceId,
			static_cast<std::uint8_t>(result),
			error);
	}

	void FPlayerCacheContent::HandleClaimMailAttachment(
		const RpcLib::Dispatch::FRpcCallContext& context,
		RpcLib::Dispatch::TRpcReply<Cache::Protocol::FClaimMailAttachmentRpc>& reply,
		const std::uint64_t userId,
		const std::uint64_t mailId,
		const std::uint64_t attachmentId)
	{
		using Cache::Protocol::ECacheCommandResult;
		Cache::Protocol::FMailClaimResult claim;
		ECacheCommandResult result = ECacheCommandResult::Success;
		std::string error;
		if (!IsAuthorizedDataCommandCaller(context) || userId == 0 || mailId == 0 || attachmentId == 0 || context.routingKey != userId)
		{
			result = ECacheCommandResult::InvalidArgument;
		}
		else if (!m_databaseConfig.enabled)
		{
			result = ECacheCommandResult::DatabaseError;
			error = "GameDB is disabled.";
		}
		else
		{
			auto& databaseContext = Database::FContentThreadDbContext::Get(m_databaseConfig);
			Connector::MySql::FMySqlConnection* connection = databaseContext.GetGamePrimary(error);
			if (connection == nullptr)
			{
				result = ECacheCommandResult::DatabaseError;
			}
			else
			{
				Connector::MySql::FMySqlTransaction transaction(*connection);
				Database::FPlayerCacheRepository repository(*connection);
				Database::SMailClaimMutationResult databaseClaim;
				if (!transaction.Begin(error))
				{
					result = ECacheCommandResult::DatabaseError;
				}
				else
				{
					Database::SMailDetail mail;
					bool mailFound = false;
					bool itemTradable = false;
					std::uint64_t maxCurrencyAmount = 0;
					if (!repository.GetMailDetail(userId, mailId, mail, mailFound, error))
					{
						transaction.Rollback();
						result = ECacheCommandResult::DatabaseError;
					}
					else
					{
						const auto attachmentIt = std::ranges::find_if(mail.attachments,
							[attachmentId](const Database::SMailAttachment& attachment)
							{
								return attachment.attachmentId == attachmentId;
							});
						if (!mailFound || attachmentIt == mail.attachments.end())
						{
							transaction.Rollback();
							result = ECacheCommandResult::NotFound;
							error = "mail attachment was not found during policy validation.";
						}
						else if (attachmentIt->attachmentType == 1)
						{
							const GameData::Item::SItemTemplate* itemTemplate = m_itemDataTable->Find(attachmentIt->itemDataId);
							if (itemTemplate == nullptr)
							{
								transaction.Rollback();
								result = ECacheCommandResult::InvalidArgument;
								error = "mail attachment references an unknown item data ID.";
							}
							else
							{
								itemTradable = itemTemplate->tradable;
							}
						}
						else if (attachmentIt->attachmentType == 2)
						{
							const GameData::Currency::SCurrencyData* currencyData = m_currencyDataTable->Find(attachmentIt->currencyId);
							if (currencyData == nullptr)
							{
								transaction.Rollback();
								result = ECacheCommandResult::InvalidArgument;
								error = "mail attachment references an unknown currency ID.";
							}
							else
							{
								maxCurrencyAmount = currencyData->maxAmount;
							}
						}

						if (result == ECacheCommandResult::Success && !repository.ClaimMailAttachment(userId,
																		  mailId,
																		  attachmentId,
																		  m_inventoryPolicyTable->Get().maxInventorySlots,
																		  itemTradable,
																		  maxCurrencyAmount,
																		  databaseClaim,
																		  error))
						{
							transaction.Rollback();
							result = ToCommandResult(error);
						}
						else if (result == ECacheCommandResult::Success && !transaction.Commit(error))
						{
							if (Domain::FCacheUser* user = FindUser(userId); user != nullptr)
							{
								user->InvalidatePlayerData(std::chrono::steady_clock::now());
							}
							result = ECacheCommandResult::OutcomeUnknown;
						}
						else if (result == ECacheCommandResult::Success)
						{
							claim.attachmentType = databaseClaim.attachmentType;
							claim.itemInstanceId = databaseClaim.itemInstanceId;
							claim.itemDataId = databaseClaim.itemDataId;
							claim.quantity = databaseClaim.quantity;
							claim.itemDataJson = databaseClaim.itemDataJson;
							claim.currencyId = databaseClaim.currencyId;
							claim.currencyAmount = databaseClaim.currencyAmount;
							claim.currencyBalance = databaseClaim.currencyBalance;
							claim.currencyVersion = databaseClaim.currencyVersion;
							claim.mailState = databaseClaim.mailState;

							if (Domain::FCacheUser* user = FindUser(userId); user != nullptr && user->IsDataValid())
							{
								try
								{
									if (databaseClaim.attachmentType == 1)
									{
										Database::SInventoryItemMutationRow item;
										item.itemInstanceId = databaseClaim.itemInstanceId;
										item.itemDataId = databaseClaim.itemDataId;
										item.quantity = databaseClaim.quantity;
										item.itemDataJson = databaseClaim.itemDataJson;
										item.tradable = itemTradable;
										item.version = 1;
										user->UpsertInventoryItem(ToCachedInventoryItem(item), std::chrono::steady_clock::now());
									}
									else if (databaseClaim.attachmentType == 2)
									{
										user->UpsertCurrency(
											Domain::SCachedCurrency{
												databaseClaim.currencyId, databaseClaim.currencyBalance, databaseClaim.currencyVersion},
											std::chrono::steady_clock::now());
									}
								}
								catch (...)
								{
									user->InvalidatePlayerData(std::chrono::steady_clock::now());
								}
							}
						}
					}
				}
			}
		}

		reply.Send(result, claim);
		Log(result == ECacheCommandResult::Success ? Foundation::ELogLevel::Info : Foundation::ELogLevel::Warn,
			"Cache mutation completed. operation=ClaimMailAttachment userId={} mailId={} attachmentId={} result={} error={}",
			userId,
			mailId,
			attachmentId,
			static_cast<std::uint8_t>(result),
			error);
	}

	void FPlayerCacheContent::HandleConsumeInventoryItemForListing(
		const RpcLib::Dispatch::FRpcCallContext& context,
		RpcLib::Dispatch::TRpcReply<Cache::Protocol::FConsumeInventoryItemForListingRpc>& reply,
		const std::uint64_t userId,
		const std::uint64_t itemInstanceId,
		const std::uint64_t expectedVersion)
	{
		using Cache::Protocol::ECacheCommandResult;
		Cache::Protocol::FInventoryItemSnapshot item;
		ECacheCommandResult result = ECacheCommandResult::Success;
		std::string error;
		Domain::FCacheUser* user = nullptr;
		if (!IsAuthorizedAuctionCaller(context) || userId == 0 || itemInstanceId == 0 || expectedVersion == 0 ||
			context.routingKey != userId)
		{
			result = ECacheCommandResult::InvalidArgument;
		}
		else
		{
			bool loadedFromDatabase = false;
			Cache::Protocol::ECacheUserLoadResult loadResult = Cache::Protocol::ECacheUserLoadResult::Success;
			user = GetOrLoadUser(userId, loadedFromDatabase, loadResult, error);
			if (user == nullptr)
			{
				result = ECacheCommandResult::DatabaseError;
			}
			else if (const auto it = user->GetInventoryItems().find(itemInstanceId); it == user->GetInventoryItems().end())
			{
				result = ECacheCommandResult::NotFound;
			}
			else if (it->second.version != expectedVersion)
			{
				result = ECacheCommandResult::ItemVersionMismatch;
			}
			else if (it->second.equipped)
			{
				result = ECacheCommandResult::ItemEquipped;
			}
			else if (!it->second.tradable)
			{
				result = ECacheCommandResult::InvalidArgument;
				error = "inventory item is not tradable.";
			}
		}

		if (result == ECacheCommandResult::Success)
		{
			auto& databaseContext = Database::FContentThreadDbContext::Get(m_databaseConfig);
			Connector::MySql::FMySqlConnection* connection = databaseContext.GetGamePrimary(error);
			if (connection == nullptr)
			{
				result = ECacheCommandResult::DatabaseError;
			}
			else
			{
				Connector::MySql::FMySqlTransaction transaction(*connection);
				Database::SInventoryItemMutationRow databaseItem;
				if (!transaction.Begin(error))
				{
					result = ECacheCommandResult::DatabaseError;
				}
				else if (!Database::FPlayerCacheRepository(*connection)
							 .RemoveInventoryItem(userId, itemInstanceId, expectedVersion, databaseItem, error))
				{
					transaction.Rollback();
					result = ToCommandResult(error);
				}
				else if (!transaction.Commit(error))
				{
					user->InvalidatePlayerData(std::chrono::steady_clock::now());
					result = ECacheCommandResult::OutcomeUnknown;
				}
				else
				{
					item = ToRpcInventoryItem(databaseItem);
					user->RemoveInventoryItem(itemInstanceId, std::chrono::steady_clock::now());
				}
			}
		}

		reply.Send(result, item);
		Log(result == ECacheCommandResult::Success ? Foundation::ELogLevel::Info : Foundation::ELogLevel::Warn,
			"Cache mutation completed. operation=ConsumeInventoryItemForListing userId={} itemInstanceId={} expectedVersion={} result={} "
			"error={}",
			userId,
			itemInstanceId,
			expectedVersion,
			static_cast<std::uint8_t>(result),
			error);
	}

	void FPlayerCacheContent::HandleDebitCurrency(
		const RpcLib::Dispatch::FRpcCallContext& context,
		RpcLib::Dispatch::TRpcReply<Cache::Protocol::FDebitCurrencyRpc>& reply,
		const std::uint64_t userId,
		const std::uint16_t currencyId,
		const std::uint64_t amount)
	{
		using Cache::Protocol::ECacheCommandResult;
		Cache::Protocol::FCurrencyBalance balance;
		ECacheCommandResult result = ECacheCommandResult::Success;
		std::string error;
		Domain::FCacheUser* user = nullptr;
		if (!IsAuthorizedDataCommandCaller(context) || userId == 0 || currencyId == 0 || amount == 0 || context.routingKey != userId)
		{
			result = ECacheCommandResult::InvalidArgument;
		}
		else
		{
			bool loadedFromDatabase = false;
			Cache::Protocol::ECacheUserLoadResult loadResult = Cache::Protocol::ECacheUserLoadResult::Success;
			user = GetOrLoadUser(userId, loadedFromDatabase, loadResult, error);
			if (user == nullptr)
			{
				result = ECacheCommandResult::DatabaseError;
			}
		}

		if (result == ECacheCommandResult::Success)
		{
			auto& databaseContext = Database::FContentThreadDbContext::Get(m_databaseConfig);
			Connector::MySql::FMySqlConnection* connection = databaseContext.GetGamePrimary(error);
			if (connection == nullptr)
			{
				result = ECacheCommandResult::DatabaseError;
			}
			else
			{
				Connector::MySql::FMySqlTransaction transaction(*connection);
				Database::SPlayerCurrencyRow currency;
				if (!transaction.Begin(error))
				{
					result = ECacheCommandResult::DatabaseError;
				}
				else if (!Database::FPlayerCacheRepository(*connection).DebitCurrency(userId, currencyId, amount, currency, error))
				{
					transaction.Rollback();
					result = ToCommandResult(error);
				}
				else if (!transaction.Commit(error))
				{
					user->InvalidatePlayerData(std::chrono::steady_clock::now());
					result = ECacheCommandResult::OutcomeUnknown;
				}
				else
				{
					balance.currencyId = currency.currencyId;
					balance.amount = currency.amount;
					balance.version = currency.version;
					try
					{
						user->UpsertCurrency(Domain::SCachedCurrency{currency.currencyId, currency.amount, currency.version},
							std::chrono::steady_clock::now());
					}
					catch (...)
					{
						user->InvalidatePlayerData(std::chrono::steady_clock::now());
					}
				}
			}
		}

		reply.Send(result, balance);
		Log(result == ECacheCommandResult::Success ? Foundation::ELogLevel::Info : Foundation::ELogLevel::Warn,
			"Cache mutation completed. operation=DebitCurrency rpcRequestId={} userId={} currencyId={} amount={} result={} balance={} "
			"version={} error={}",
			context.requestId,
			userId,
			currencyId,
			amount,
			static_cast<std::uint8_t>(result),
			balance.amount,
			balance.version,
			error);
	}

	void FPlayerCacheContent::HandleSettleBuyout(
		const RpcLib::Dispatch::FRpcCallContext& context,
		RpcLib::Dispatch::TRpcReply<Cache::Protocol::FSettleBuyoutRpc>& reply,
		const std::uint64_t buyerUserId,
		const std::uint64_t sellerUserId,
		const std::uint16_t currencyId,
		const std::uint64_t additionalDebit,
		const std::uint64_t buyoutPrice,
		const std::uint64_t itemInstanceId,
		const std::uint32_t itemDataId,
		const std::uint32_t quantity,
		const std::string& itemDataJson)
	{
		using Cache::Protocol::ECacheCommandResult;
		Cache::Protocol::FBuyoutSettlementResult settlement;
		ECacheCommandResult result = ECacheCommandResult::Success;
		std::string error;
		Domain::FCacheUser* buyer = nullptr;
		const GameData::MailTemplate::SMailTemplateData* purchaseMailTemplate =
			m_mailTemplateTable->FindByPurpose(GameData::Common::EMailTemplatePurpose::AuctionPurchase);
		const GameData::MailTemplate::SMailTemplateData* saleProceedsMailTemplate =
			m_mailTemplateTable->FindByPurpose(GameData::Common::EMailTemplatePurpose::AuctionSaleProceeds);
		const std::uint32_t mailExpirationSeconds = m_mailPolicyTable->Get().expirationSeconds;
		if (!IsAuthorizedAuctionCaller(context) || buyerUserId == 0 || sellerUserId == 0 || buyerUserId == sellerUserId ||
			currencyId == 0 || buyoutPrice == 0 || additionalDebit > buyoutPrice || itemInstanceId == 0 || itemDataId == 0 ||
			quantity == 0 || itemDataJson.empty() || context.routingKey != buyerUserId)
		{
			result = ECacheCommandResult::InvalidArgument;
		}
		else if (purchaseMailTemplate == nullptr || saleProceedsMailTemplate == nullptr || mailExpirationSeconds == 0)
		{
			result = ECacheCommandResult::DatabaseError;
			error = "SettleBuyout could not resolve required mail GameData.";
		}
		else
		{
			bool loadedFromDatabase = false;
			Cache::Protocol::ECacheUserLoadResult loadResult = Cache::Protocol::ECacheUserLoadResult::Success;
			buyer = GetOrLoadUser(buyerUserId, loadedFromDatabase, loadResult, error);
			if (buyer == nullptr)
			{
				result = ECacheCommandResult::DatabaseError;
			}
			else if (const auto it = buyer->GetCurrencies().find(currencyId); it != buyer->GetCurrencies().end())
			{
				settlement.currencyBalance.currencyId = it->second.currencyId;
				settlement.currencyBalance.amount = it->second.amount;
				settlement.currencyBalance.version = it->second.version;
			}
		}

		if (result == ECacheCommandResult::Success)
		{
			auto& databaseContext = Database::FContentThreadDbContext::Get(m_databaseConfig);
			Connector::MySql::FMySqlConnection* connection = databaseContext.GetGamePrimary(error);
			if (connection == nullptr)
			{
				result = ECacheCommandResult::DatabaseError;
			}
			else
			{
				Connector::MySql::FMySqlTransaction transaction(*connection);
				Database::SPlayerCurrencyRow currency;
				if (!transaction.Begin(error))
				{
					result = ECacheCommandResult::DatabaseError;
				}
				else if (additionalDebit > 0 && !Database::FPlayerCacheRepository(*connection)
													.DebitCurrency(buyerUserId, currencyId, additionalDebit, currency, error))
				{
					transaction.Rollback();
					result = ToCommandResult(error);
				}
				else
				{
					Database::FPlayerCacheRepository repository(*connection);
					if (!repository.CreateItemMail(buyerUserId,
							itemInstanceId,
							itemDataId,
							quantity,
							itemDataJson,
							purchaseMailTemplate->mailType,
							purchaseMailTemplate->subject,
							purchaseMailTemplate->body,
							mailExpirationSeconds,
							settlement.itemMailId,
							error) ||
						!repository.CreateCurrencyMail(sellerUserId,
							currencyId,
							buyoutPrice,
							saleProceedsMailTemplate->mailType,
							saleProceedsMailTemplate->subject,
							saleProceedsMailTemplate->body,
							mailExpirationSeconds,
							settlement.sellerMailId,
							error))
					{
						transaction.Rollback();
						result = ToCommandResult(error);
					}
					else if (!transaction.Commit(error))
					{
						buyer->InvalidatePlayerData(std::chrono::steady_clock::now());
						result = ECacheCommandResult::OutcomeUnknown;
					}
					else if (additionalDebit > 0)
					{
						settlement.currencyBalance.currencyId = currency.currencyId;
						settlement.currencyBalance.amount = currency.amount;
						settlement.currencyBalance.version = currency.version;
						try
						{
							buyer->UpsertCurrency(Domain::SCachedCurrency{currency.currencyId, currency.amount, currency.version},
								std::chrono::steady_clock::now());
						}
						catch (...)
						{
							buyer->InvalidatePlayerData(std::chrono::steady_clock::now());
						}
					}
				}
			}
		}

		reply.Send(result, settlement);
		Log(result == ECacheCommandResult::Success ? Foundation::ELogLevel::Info : Foundation::ELogLevel::Warn,
			"Cache mutation completed. operation=SettleBuyout rpcRequestId={} buyerUserId={} sellerUserId={} additionalDebit={} "
			"buyoutPrice={} result={} "
			"itemMailId={} sellerMailId={} error={}",
			context.requestId,
			buyerUserId,
			sellerUserId,
			additionalDebit,
			buyoutPrice,
			static_cast<std::uint8_t>(result),
			settlement.itemMailId,
			settlement.sellerMailId,
			error);
	}

	void FPlayerCacheContent::HandleCreateListingReturnMail(
		const RpcLib::Dispatch::FRpcCallContext& context,
		RpcLib::Dispatch::TRpcReply<Cache::Protocol::FCreateListingReturnMailRpc>& reply,
		const std::uint64_t sellerUserId,
		const std::uint64_t itemInstanceId,
		const std::uint32_t itemDataId,
		const std::uint32_t quantity,
		const std::string& itemDataJson)
	{
		using Cache::Protocol::ECacheCommandResult;
		std::uint64_t mailId = 0;
		ECacheCommandResult result = ECacheCommandResult::Success;
		std::string error;
		const GameData::MailTemplate::SMailTemplateData* cancellationMailTemplate =
			m_mailTemplateTable->FindByPurpose(GameData::Common::EMailTemplatePurpose::AuctionCancellationReturn);
		const std::uint32_t mailExpirationSeconds = m_mailPolicyTable->Get().expirationSeconds;
		if (!IsAuthorizedAuctionCaller(context) || sellerUserId == 0 || itemInstanceId == 0 || itemDataId == 0 || quantity == 0 ||
			itemDataJson.empty() || context.routingKey != sellerUserId)
		{
			result = ECacheCommandResult::InvalidArgument;
		}
		else if (cancellationMailTemplate == nullptr || mailExpirationSeconds == 0)
		{
			result = ECacheCommandResult::DatabaseError;
			error = "CreateListingReturnMail could not resolve required mail GameData.";
		}
		else
		{
			auto& databaseContext = Database::FContentThreadDbContext::Get(m_databaseConfig);
			Connector::MySql::FMySqlConnection* connection = databaseContext.GetGamePrimary(error);
			if (connection == nullptr)
			{
				result = ECacheCommandResult::DatabaseError;
			}
			else
			{
				Connector::MySql::FMySqlTransaction transaction(*connection);
				if (!transaction.Begin(error))
				{
					result = ECacheCommandResult::DatabaseError;
				}
				else if (!Database::FPlayerCacheRepository(*connection)
							 .CreateItemReturnMail(sellerUserId,
								 itemInstanceId,
								 itemDataId,
								 quantity,
								 itemDataJson,
								 cancellationMailTemplate->mailType,
								 cancellationMailTemplate->subject,
								 cancellationMailTemplate->body,
								 mailExpirationSeconds,
								 mailId,
								 error))
				{
					transaction.Rollback();
					result = ToCommandResult(error);
				}
				else if (!transaction.Commit(error))
				{
					result = ECacheCommandResult::OutcomeUnknown;
				}
			}
		}

		reply.Send(result, mailId);
		Log(result == ECacheCommandResult::Success ? Foundation::ELogLevel::Info : Foundation::ELogLevel::Warn,
			"Cache mutation completed. operation=CreateListingReturnMail rpcRequestId={} sellerUserId={} itemInstanceId={} result={} "
			"mailId={} "
			"error={}",
			context.requestId,
			sellerUserId,
			itemInstanceId,
			static_cast<std::uint8_t>(result),
			mailId,
			error);
	}

	void FPlayerCacheContent::HandleSettleExpiration(
		const RpcLib::Dispatch::FRpcCallContext& context,
		RpcLib::Dispatch::TRpcReply<Cache::Protocol::FSettleExpirationRpc>& reply,
		const std::uint64_t primaryUserId,
		const std::uint64_t sellerUserId,
		const std::uint64_t winnerUserId,
		const std::uint16_t currencyId,
		const std::uint64_t finalPrice,
		const std::uint64_t itemInstanceId,
		const std::uint32_t itemDataId,
		const std::uint32_t quantity,
		const std::string& itemDataJson)
	{
		using Cache::Protocol::ECacheCommandResult;
		Cache::Protocol::FExpirationSettlementResult settlement;
		ECacheCommandResult result = ECacheCommandResult::Success;
		std::string error;
		const std::uint64_t expectedPrimaryUserId = winnerUserId == 0 ? sellerUserId : winnerUserId;
		const GameData::MailTemplate::SMailTemplateData* purchaseMailTemplate =
			m_mailTemplateTable->FindByPurpose(GameData::Common::EMailTemplatePurpose::AuctionPurchase);
		const GameData::MailTemplate::SMailTemplateData* saleProceedsMailTemplate =
			m_mailTemplateTable->FindByPurpose(GameData::Common::EMailTemplatePurpose::AuctionSaleProceeds);
		const GameData::MailTemplate::SMailTemplateData* expirationReturnMailTemplate =
			m_mailTemplateTable->FindByPurpose(GameData::Common::EMailTemplatePurpose::AuctionExpirationReturn);
		const std::uint32_t mailExpirationSeconds = m_mailPolicyTable->Get().expirationSeconds;
		if (!IsAuthorizedAuctionCaller(context) || primaryUserId == 0 || sellerUserId == 0 || primaryUserId != expectedPrimaryUserId ||
			context.routingKey != primaryUserId || itemInstanceId == 0 || itemDataId == 0 || quantity == 0 || itemDataJson.empty() ||
			(winnerUserId == 0 && finalPrice != 0) ||
			(winnerUserId != 0 && (winnerUserId == sellerUserId || currencyId == 0 || finalPrice == 0)))
		{
			result = ECacheCommandResult::InvalidArgument;
		}
		else if (mailExpirationSeconds == 0 || (winnerUserId == 0 && expirationReturnMailTemplate == nullptr) ||
				 (winnerUserId != 0 && (purchaseMailTemplate == nullptr || saleProceedsMailTemplate == nullptr)))
		{
			result = ECacheCommandResult::DatabaseError;
			error = "SettleExpiration could not resolve required mail GameData.";
		}
		else
		{
			auto& databaseContext = Database::FContentThreadDbContext::Get(m_databaseConfig);
			Connector::MySql::FMySqlConnection* connection = databaseContext.GetGamePrimary(error);
			if (connection == nullptr)
			{
				result = ECacheCommandResult::DatabaseError;
			}
			else
			{
				Connector::MySql::FMySqlTransaction transaction(*connection);
				if (!transaction.Begin(error))
				{
					result = ECacheCommandResult::DatabaseError;
				}
				else
				{
					Database::FPlayerCacheRepository repository(*connection);
					const bool settled = winnerUserId == 0 ? repository.CreateExpiredItemReturnMail(sellerUserId,
																 itemInstanceId,
																 itemDataId,
																 quantity,
																 itemDataJson,
																 expirationReturnMailTemplate->mailType,
																 expirationReturnMailTemplate->subject,
																 expirationReturnMailTemplate->body,
																 mailExpirationSeconds,
																 settlement.itemMailId,
																 error)
														   : repository.CreateItemMail(winnerUserId,
																 itemInstanceId,
																 itemDataId,
																 quantity,
																 itemDataJson,
																 purchaseMailTemplate->mailType,
																 purchaseMailTemplate->subject,
																 purchaseMailTemplate->body,
																 mailExpirationSeconds,
																 settlement.itemMailId,
																 error) &&
																 repository.CreateCurrencyMail(sellerUserId,
																	 currencyId,
																	 finalPrice,
																	 saleProceedsMailTemplate->mailType,
																	 saleProceedsMailTemplate->subject,
																	 saleProceedsMailTemplate->body,
																	 mailExpirationSeconds,
																	 settlement.sellerMailId,
																	 error);
					if (!settled)
					{
						transaction.Rollback();
						result = ToCommandResult(error);
					}
					else if (!transaction.Commit(error))
					{
						result = ECacheCommandResult::OutcomeUnknown;
					}
				}
			}
		}

		reply.Send(result, settlement);
		Log(result == ECacheCommandResult::Success ? Foundation::ELogLevel::Info : Foundation::ELogLevel::Warn,
			"Cache mutation completed. operation=SettleExpiration rpcRequestId={} sellerUserId={} winnerUserId={} finalPrice={} result={} "
			"itemMailId={} "
			"sellerMailId={} error={}",
			context.requestId,
			sellerUserId,
			winnerUserId,
			finalPrice,
			static_cast<std::uint8_t>(result),
			settlement.itemMailId,
			settlement.sellerMailId,
			error);
	}

	void FPlayerCacheContent::HandleGetPlayerWorldSnapshot(
		const RpcLib::Dispatch::FRpcCallContext& context,
		RpcLib::Dispatch::TRpcReply<Cache::Protocol::FGetPlayerWorldSnapshotRpc>& reply,
		const std::uint64_t userId,
		const std::uint64_t gameClientSessionId,
		const std::uint64_t ownerGeneration)
	{
		using Cache::Protocol::EPlayerProgressResult;
		EPlayerProgressResult result = EPlayerProgressResult::Success;
		Cache::Protocol::FPlayerWorldSnapshot snapshot;
		std::string error;
		Domain::FCacheUser* user = nullptr;
		if (userId == 0 || gameClientSessionId == 0 || ownerGeneration == 0 || context.routingKey != userId)
		{
			result = EPlayerProgressResult::InvalidArgument;
			error = "GetPlayerWorldSnapshot received an invalid argument.";
		}
		else
		{
			user = GetCurrentGameOwnerUser(context, userId, gameClientSessionId, ownerGeneration, result, error);
			if (user != nullptr)
			{
				if (!ToRpcPlayerWorldSnapshot(*user, *m_itemDataTable, snapshot, error))
				{
					user->InvalidatePlayerData(std::chrono::steady_clock::now());
					result = EPlayerProgressResult::CharacterDataError;
				}
			}
		}

		reply.Send(result, snapshot);
		Log(result == EPlayerProgressResult::Success ? Foundation::ELogLevel::Info : Foundation::ELogLevel::Warn,
			"Player world snapshot completed. userId={} gameClientSessionId={} ownerGeneration={} result={} statRevision={} error={}",
			userId,
			gameClientSessionId,
			ownerGeneration,
			static_cast<std::uint8_t>(result),
			snapshot.statRevision,
			error);
	}

	void FPlayerCacheContent::HandleAllocatePlayerStat(
		const RpcLib::Dispatch::FRpcCallContext& context,
		RpcLib::Dispatch::TRpcReply<Cache::Protocol::FAllocatePlayerStatRpc>& reply,
		const std::uint64_t userId,
		const std::uint64_t gameClientSessionId,
		const std::uint64_t ownerGeneration,
		const std::uint64_t expectedStatVersion,
		const std::uint32_t addStr,
		const std::uint32_t addDex,
		const std::uint32_t addInt,
		const std::uint32_t addLuk)
	{
		using Cache::Protocol::EPlayerProgressResult;
		EPlayerProgressResult result = EPlayerProgressResult::Success;
		Cache::Protocol::FPlayerWorldSnapshot snapshot;
		std::string error;
		Domain::FCacheUser* user = nullptr;
		const std::uint64_t requiredPoints = static_cast<std::uint64_t>(addStr) + addDex + addInt + addLuk;
		if (userId == 0 || gameClientSessionId == 0 || ownerGeneration == 0 || expectedStatVersion == 0 || requiredPoints == 0 ||
			requiredPoints > std::numeric_limits<std::uint32_t>::max() || context.routingKey != userId)
		{
			result = EPlayerProgressResult::InvalidArgument;
			error = "AllocatePlayerStat received an invalid argument.";
		}
		else if ((user = GetCurrentGameOwnerUser(context, userId, gameClientSessionId, ownerGeneration, result, error)) == nullptr)
		{
		}
		else if (user->GetProgress().statVersion != expectedStatVersion)
		{
			result = EPlayerProgressResult::ConcurrentModification;
			error = "expected stat version is stale.";
		}
		else if (requiredPoints > user->GetProgress().unspentStatPoints)
		{
			result = EPlayerProgressResult::InsufficientStatPoints;
			error = "unspent stat points are insufficient.";
		}
		else
		{
			const Domain::SPrimaryStats& primary = user->GetProgress().persistentPrimary;
			if (primary.str > std::numeric_limits<std::uint32_t>::max() - addStr ||
				primary.dex > std::numeric_limits<std::uint32_t>::max() - addDex ||
				primary.intelligence > std::numeric_limits<std::uint32_t>::max() - addInt ||
				primary.luk > std::numeric_limits<std::uint32_t>::max() - addLuk)
			{
				result = EPlayerProgressResult::InvalidArgument;
				error = "allocated primary stat would overflow.";
			}
			else if (!m_databaseConfig.enabled)
			{
				result = EPlayerProgressResult::DatabaseError;
				error = "GameDB is disabled.";
			}
			else
			{
				auto& databaseContext = Database::FContentThreadDbContext::Get(m_databaseConfig);
				Connector::MySql::FMySqlConnection* connection = databaseContext.GetGamePrimary(error);
				if (connection == nullptr)
				{
					result = EPlayerProgressResult::DatabaseError;
				}
				else
				{
					Connector::MySql::FMySqlTransaction transaction(*connection);
					Database::SPlayerCharacterRow updatedCharacter;
					if (!transaction.Begin(error))
					{
						result = EPlayerProgressResult::DatabaseError;
					}
					else if (!Database::FPlayerCacheRepository(*connection)
								 .AllocatePlayerStats(userId, expectedStatVersion, addStr, addDex, addInt, addLuk, updatedCharacter, error))
					{
						transaction.Rollback();
						result = error.find("PLAYER_STAT_ALLOCATION_CONFLICT") != std::string::npos
									 ? EPlayerProgressResult::ConcurrentModification
									 : EPlayerProgressResult::DatabaseError;
					}
					else if (!transaction.Commit(error))
					{
						user->InvalidatePlayerData(std::chrono::steady_clock::now());
						result = EPlayerProgressResult::OutcomeUnknown;
					}
					else
					{
						user->UpdateProgress(ToDomainProgress(updatedCharacter), std::chrono::steady_clock::now());
						if (!RefreshStateRevision(*user, error) || !ToRpcPlayerWorldSnapshot(*user, *m_itemDataTable, snapshot, error))
						{
							user->InvalidatePlayerData(std::chrono::steady_clock::now());
							result = EPlayerProgressResult::CharacterDataError;
						}
					}
				}
			}
		}

		reply.Send(result, snapshot);
		Log(result == EPlayerProgressResult::Success ? Foundation::ELogLevel::Info : Foundation::ELogLevel::Warn,
			"Player stat allocation completed. userId={} expectedStatVersion={} add={}/{}/{}/{} result={} newStatVersion={} error={}",
			userId,
			expectedStatVersion,
			addStr,
			addDex,
			addInt,
			addLuk,
			static_cast<std::uint8_t>(result),
			snapshot.progress.statVersion,
			error);
	}

	void FPlayerCacheContent::HandleGrantPlayerExperience(
		const RpcLib::Dispatch::FRpcCallContext& context,
		RpcLib::Dispatch::TRpcReply<Cache::Protocol::FGrantPlayerExperienceRpc>& reply,
		const std::uint64_t userId,
		const std::uint64_t gameClientSessionId,
		const std::uint64_t ownerGeneration,
		const std::uint64_t expectedProgressVersion,
		const std::uint64_t amount)
	{
		using Cache::Protocol::EPlayerProgressResult;
		EPlayerProgressResult result = EPlayerProgressResult::Success;
		Cache::Protocol::FPlayerWorldSnapshot snapshot;
		std::string error;
		Domain::FCacheUser* user = nullptr;
		if (userId == 0 || gameClientSessionId == 0 || ownerGeneration == 0 || expectedProgressVersion == 0 || amount == 0 ||
			context.routingKey != userId)
		{
			result = EPlayerProgressResult::InvalidArgument;
			error = "GrantPlayerExperience received an invalid argument.";
		}
		else if ((user = GetCurrentGameOwnerUser(context, userId, gameClientSessionId, ownerGeneration, result, error)) == nullptr)
		{
		}
		else if (user->GetProgress().progressVersion != expectedProgressVersion)
		{
			result = EPlayerProgressResult::ConcurrentModification;
			error = "expected progress version is stale.";
		}
		else if (user->GetProgress().exp > std::numeric_limits<std::uint64_t>::max() - amount)
		{
			result = EPlayerProgressResult::InvalidArgument;
			error = "experience amount would overflow.";
		}
		else
		{
			const Domain::SPlayerProgress& progress = user->GetProgress();
			std::uint32_t newLevel = progress.level;
			std::uint64_t newExp = progress.exp + amount;
			std::uint64_t reward = 0;
			const auto* currentLevelData = m_characterLevelDataTable->Find(progress.characterDataId, newLevel);
			if (currentLevelData == nullptr)
			{
				result = EPlayerProgressResult::CharacterDataError;
				error = "current CharacterLevel GameData is missing.";
			}
			else if (currentLevelData->requiredExpToNextLevel == 0)
			{
				result = EPlayerProgressResult::LevelCap;
				error = "character already reached the maximum configured level.";
			}
			else
			{
				while (currentLevelData->requiredExpToNextLevel != 0 && newExp >= currentLevelData->requiredExpToNextLevel)
				{
					newExp -= currentLevelData->requiredExpToNextLevel;
					if (newLevel == std::numeric_limits<std::uint32_t>::max())
					{
						result = EPlayerProgressResult::CharacterDataError;
						error = "character level overflow.";
						break;
					}
					++newLevel;
					currentLevelData = m_characterLevelDataTable->Find(progress.characterDataId, newLevel);
					if (currentLevelData == nullptr)
					{
						result = EPlayerProgressResult::CharacterDataError;
						error = "next CharacterLevel GameData is missing.";
						break;
					}
					reward += currentLevelData->statPointReward;
					if (reward > std::numeric_limits<std::uint32_t>::max() ||
						progress.unspentStatPoints > std::numeric_limits<std::uint32_t>::max() - reward)
					{
						result = EPlayerProgressResult::CharacterDataError;
						error = "level-up stat point reward would overflow.";
						break;
					}
					if (currentLevelData->requiredExpToNextLevel == 0)
					{
						newExp = 0;
						break;
					}
				}

				if (result == EPlayerProgressResult::Success && !m_databaseConfig.enabled)
				{
					result = EPlayerProgressResult::DatabaseError;
					error = "GameDB is disabled.";
				}
				else if (result == EPlayerProgressResult::Success)
				{
					auto& databaseContext = Database::FContentThreadDbContext::Get(m_databaseConfig);
					Connector::MySql::FMySqlConnection* connection = databaseContext.GetGamePrimary(error);
					if (connection == nullptr)
					{
						result = EPlayerProgressResult::DatabaseError;
					}
					else
					{
						Connector::MySql::FMySqlTransaction transaction(*connection);
						Database::SPlayerCharacterRow updatedCharacter;
						if (!transaction.Begin(error))
						{
							result = EPlayerProgressResult::DatabaseError;
						}
						else if (!Database::FPlayerCacheRepository(*connection)
									 .UpdatePlayerExperience(userId,
										 expectedProgressVersion,
										 newLevel,
										 newExp,
										 static_cast<std::uint32_t>(reward),
										 updatedCharacter,
										 error))
						{
							transaction.Rollback();
							result = error.find("PLAYER_EXPERIENCE_CONFLICT") != std::string::npos
										 ? EPlayerProgressResult::ConcurrentModification
										 : EPlayerProgressResult::DatabaseError;
						}
						else if (!transaction.Commit(error))
						{
							user->InvalidatePlayerData(std::chrono::steady_clock::now());
							result = EPlayerProgressResult::OutcomeUnknown;
						}
						else
						{
							user->UpdateProgress(ToDomainProgress(updatedCharacter), std::chrono::steady_clock::now());
							if (!RefreshStateRevision(*user, error) || !ToRpcPlayerWorldSnapshot(*user, *m_itemDataTable, snapshot, error))
							{
								user->InvalidatePlayerData(std::chrono::steady_clock::now());
								result = EPlayerProgressResult::CharacterDataError;
							}
						}
					}
				}
			}
		}

		reply.Send(result, snapshot);
		Log(result == EPlayerProgressResult::Success ? Foundation::ELogLevel::Info : Foundation::ELogLevel::Warn,
			"Player experience grant completed. userId={} amount={} expectedProgressVersion={} result={} level={} exp={} "
			"progressVersion={} error={}",
			userId,
			amount,
			expectedProgressVersion,
			static_cast<std::uint8_t>(result),
			snapshot.progress.level,
			snapshot.progress.exp,
			snapshot.progress.progressVersion,
			error);
	}

	void FPlayerCacheContent::HandleEquipPlayerItem(
		const RpcLib::Dispatch::FRpcCallContext& context,
		RpcLib::Dispatch::TRpcReply<Cache::Protocol::FEquipPlayerItemRpc>& reply,
		const std::uint64_t userId,
		const std::uint64_t gameClientSessionId,
		const std::uint64_t ownerGeneration,
		const std::uint64_t itemInstanceId,
		const std::uint64_t expectedItemVersion,
		const std::uint64_t expectedStatRevision,
		const std::uint64_t expectedEquipmentVersion)
	{
		Cache::Protocol::EPlayerEquipmentResult result = Cache::Protocol::EPlayerEquipmentResult::Success;
		Cache::Protocol::FPlayerWorldSnapshot snapshot;
		std::uint64_t itemVersion = 0;
		bool equipped = true;
		bool stateInvalidated = false;
		std::string error;
		SetPlayerEquipment(context,
			userId,
			gameClientSessionId,
			ownerGeneration,
			itemInstanceId,
			expectedItemVersion,
			expectedStatRevision,
			expectedEquipmentVersion,
			true,
			result,
			snapshot,
			itemVersion,
			equipped,
			stateInvalidated,
			error);
		reply.Send(result, snapshot, itemVersion, equipped);
		Log(result == Cache::Protocol::EPlayerEquipmentResult::Success ? Foundation::ELogLevel::Info : Foundation::ELogLevel::Warn,
			"Player equipment mutation completed. operation=Equip userId={} itemInstanceId={} expectedItemVersion={} "
			"expectedStatRevision={} expectedEquipmentVersion={} result={} stateInvalidated={} newStatRevision={} "
			"newEquipmentVersion={} error={}",
			userId,
			itemInstanceId,
			expectedItemVersion,
			expectedStatRevision,
			expectedEquipmentVersion,
			static_cast<std::uint8_t>(result),
			stateInvalidated,
			snapshot.statRevision,
			snapshot.equipmentVersion,
			error);
	}

	void FPlayerCacheContent::HandleUnequipPlayerItem(
		const RpcLib::Dispatch::FRpcCallContext& context,
		RpcLib::Dispatch::TRpcReply<Cache::Protocol::FUnequipPlayerItemRpc>& reply,
		const std::uint64_t userId,
		const std::uint64_t gameClientSessionId,
		const std::uint64_t ownerGeneration,
		const std::uint64_t itemInstanceId,
		const std::uint64_t expectedItemVersion,
		const std::uint64_t expectedStatRevision,
		const std::uint64_t expectedEquipmentVersion)
	{
		Cache::Protocol::EPlayerEquipmentResult result = Cache::Protocol::EPlayerEquipmentResult::Success;
		Cache::Protocol::FPlayerWorldSnapshot snapshot;
		std::uint64_t itemVersion = 0;
		bool equipped = false;
		bool stateInvalidated = false;
		std::string error;
		SetPlayerEquipment(context,
			userId,
			gameClientSessionId,
			ownerGeneration,
			itemInstanceId,
			expectedItemVersion,
			expectedStatRevision,
			expectedEquipmentVersion,
			false,
			result,
			snapshot,
			itemVersion,
			equipped,
			stateInvalidated,
			error);
		reply.Send(result, snapshot, itemVersion, equipped);
		Log(result == Cache::Protocol::EPlayerEquipmentResult::Success ? Foundation::ELogLevel::Info : Foundation::ELogLevel::Warn,
			"Player equipment mutation completed. operation=Unequip userId={} itemInstanceId={} expectedItemVersion={} "
			"expectedStatRevision={} expectedEquipmentVersion={} result={} stateInvalidated={} newStatRevision={} "
			"newEquipmentVersion={} error={}",
			userId,
			itemInstanceId,
			expectedItemVersion,
			expectedStatRevision,
			expectedEquipmentVersion,
			static_cast<std::uint8_t>(result),
			stateInvalidated,
			snapshot.statRevision,
			snapshot.equipmentVersion,
			error);
	}

	void FPlayerCacheContent::HandleEquipPlayerItemV2(
		const RpcLib::Dispatch::FRpcCallContext& context,
		RpcLib::Dispatch::TRpcReply<Cache::Protocol::FEquipPlayerItemV2Rpc>& reply,
		const std::uint64_t userId,
		const std::uint64_t gameClientSessionId,
		const std::uint64_t ownerGeneration,
		const std::uint64_t itemInstanceId,
		const std::uint64_t expectedItemVersion,
		const std::uint64_t expectedStatRevision,
		const std::uint64_t expectedEquipmentVersion)
	{
		Cache::Protocol::EPlayerEquipmentResult result = Cache::Protocol::EPlayerEquipmentResult::Success;
		Cache::Protocol::FPlayerWorldSnapshot snapshot;
		std::uint64_t itemVersion = 0;
		bool equipped = false;
		bool stateInvalidated = false;
		std::string error;
		SetPlayerEquipment(context,
			userId,
			gameClientSessionId,
			ownerGeneration,
			itemInstanceId,
			expectedItemVersion,
			expectedStatRevision,
			expectedEquipmentVersion,
			true,
			result,
			snapshot,
			itemVersion,
			equipped,
			stateInvalidated,
			error);
		reply.Send(result, snapshot, itemVersion, equipped, stateInvalidated);
		Log(result == Cache::Protocol::EPlayerEquipmentResult::Success ? Foundation::ELogLevel::Info : Foundation::ELogLevel::Warn,
			"Player equipment mutation completed. rpc=V2 operation=Equip userId={} itemInstanceId={} expectedItemVersion={} "
			"expectedStatRevision={} expectedEquipmentVersion={} result={} stateInvalidated={} newStatRevision={} "
			"newEquipmentVersion={} error={}",
			userId,
			itemInstanceId,
			expectedItemVersion,
			expectedStatRevision,
			expectedEquipmentVersion,
			static_cast<std::uint8_t>(result),
			stateInvalidated,
			snapshot.statRevision,
			snapshot.equipmentVersion,
			error);
	}

	void FPlayerCacheContent::HandleUnequipPlayerItemV2(
		const RpcLib::Dispatch::FRpcCallContext& context,
		RpcLib::Dispatch::TRpcReply<Cache::Protocol::FUnequipPlayerItemV2Rpc>& reply,
		const std::uint64_t userId,
		const std::uint64_t gameClientSessionId,
		const std::uint64_t ownerGeneration,
		const std::uint64_t itemInstanceId,
		const std::uint64_t expectedItemVersion,
		const std::uint64_t expectedStatRevision,
		const std::uint64_t expectedEquipmentVersion)
	{
		Cache::Protocol::EPlayerEquipmentResult result = Cache::Protocol::EPlayerEquipmentResult::Success;
		Cache::Protocol::FPlayerWorldSnapshot snapshot;
		std::uint64_t itemVersion = 0;
		bool equipped = false;
		bool stateInvalidated = false;
		std::string error;
		SetPlayerEquipment(context,
			userId,
			gameClientSessionId,
			ownerGeneration,
			itemInstanceId,
			expectedItemVersion,
			expectedStatRevision,
			expectedEquipmentVersion,
			false,
			result,
			snapshot,
			itemVersion,
			equipped,
			stateInvalidated,
			error);
		reply.Send(result, snapshot, itemVersion, equipped, stateInvalidated);
		Log(result == Cache::Protocol::EPlayerEquipmentResult::Success ? Foundation::ELogLevel::Info : Foundation::ELogLevel::Warn,
			"Player equipment mutation completed. rpc=V2 operation=Unequip userId={} itemInstanceId={} expectedItemVersion={} "
			"expectedStatRevision={} expectedEquipmentVersion={} result={} stateInvalidated={} newStatRevision={} "
			"newEquipmentVersion={} error={}",
			userId,
			itemInstanceId,
			expectedItemVersion,
			expectedStatRevision,
			expectedEquipmentVersion,
			static_cast<std::uint8_t>(result),
			stateInvalidated,
			snapshot.statRevision,
			snapshot.equipmentVersion,
			error);
	}

	void FPlayerCacheContent::SetPlayerEquipment(
		const RpcLib::Dispatch::FRpcCallContext& context,
		const std::uint64_t userId,
		const std::uint64_t gameClientSessionId,
		const std::uint64_t ownerGeneration,
		const std::uint64_t itemInstanceId,
		const std::uint64_t expectedItemVersion,
		const std::uint64_t expectedStatRevision,
		const std::uint64_t expectedEquipmentVersion,
		const bool equipped,
		Cache::Protocol::EPlayerEquipmentResult& outResult,
		Cache::Protocol::FPlayerWorldSnapshot& outSnapshot,
		std::uint64_t& outItemVersion,
		bool& outEquipped,
		bool& outStateInvalidated,
		std::string& outError)
	{
		using EEquipmentResult = Cache::Protocol::EPlayerEquipmentResult;
		outResult = EEquipmentResult::Success;
		outSnapshot = {};
		outItemVersion = 0;
		outEquipped = false;
		outStateInvalidated = false;
		outError.clear();

		Domain::FCacheUser* user = nullptr;
		if (userId == 0 || gameClientSessionId == 0 || ownerGeneration == 0 || itemInstanceId == 0 || expectedItemVersion == 0 ||
			expectedStatRevision == 0 || context.routingKey != userId)
		{
			outResult = EEquipmentResult::InvalidArgument;
			outError = "player equipment request contains an invalid argument.";
			return;
		}
		Cache::Protocol::EPlayerProgressResult ownerResult = Cache::Protocol::EPlayerProgressResult::Success;
		user = GetCurrentGameOwnerUser(context, userId, gameClientSessionId, ownerGeneration, ownerResult, outError);
		if (user == nullptr)
		{
			if (ownerResult == Cache::Protocol::EPlayerProgressResult::UnauthorizedCaller)
			{
				outResult = EEquipmentResult::UnauthorizedCaller;
			}
			else if (ownerResult == Cache::Protocol::EPlayerProgressResult::CharacterDataError)
			{
				outResult = EEquipmentResult::CharacterDataError;
			}
			else
			{
				outResult = EEquipmentResult::DatabaseError;
			}
			return;
		}

		const Domain::SPlayerStateRevision& currentRevision = user->GetStateRevision();
		if (currentRevision.statRevision != expectedStatRevision || currentRevision.equipmentVersion != expectedEquipmentVersion)
		{
			outResult = EEquipmentResult::ConcurrentModification;
			outError = "player state or equipment revision is stale.";
			return;
		}
		if (currentRevision.statRevision == std::numeric_limits<std::uint64_t>::max())
		{
			outResult = EEquipmentResult::ConcurrentModification;
			outError = "player stat revision cannot advance.";
			return;
		}

		const auto targetIt = user->GetInventoryItems().find(itemInstanceId);
		if (targetIt == user->GetInventoryItems().end())
		{
			outResult = EEquipmentResult::ItemNotFound;
			outError = "inventory item was not found.";
			return;
		}
		const Domain::SCachedInventoryItem& targetItem = targetIt->second;
		if (targetItem.version != expectedItemVersion)
		{
			outResult = EEquipmentResult::ItemVersionMismatch;
			outError = "inventory item version is stale.";
			return;
		}
		if (targetItem.equipped == equipped)
		{
			outResult = EEquipmentResult::EquipmentStateConflict;
			outError = equipped ? "inventory item is already equipped." : "inventory item is already unequipped.";
			return;
		}
		if (targetItem.version == std::numeric_limits<std::uint64_t>::max())
		{
			outResult = EEquipmentResult::ConcurrentModification;
			outError = "inventory item version cannot advance.";
			return;
		}

		const GameData::Item::SItemTemplate* targetTemplate = m_itemDataTable->Find(targetItem.itemDataId);
		if (targetTemplate == nullptr)
		{
			outResult = EEquipmentResult::CharacterDataError;
			outError = "inventory item references missing Item GameData.";
			return;
		}
		if (targetTemplate->category != GameData::Common::EItemCategory::Equipment ||
			targetTemplate->equipmentSlot == GameData::Common::EEquipmentSlot::None)
		{
			outResult = EEquipmentResult::NotEquipment;
			outError = "inventory item is not equipment.";
			return;
		}

		const Domain::SCachedInventoryItem* previousItem = nullptr;
		if (equipped)
		{
			for (const auto& [candidateItemInstanceId, candidate] : user->GetInventoryItems())
			{
				if (!candidate.equipped || candidateItemInstanceId == itemInstanceId)
				{
					continue;
				}
				const GameData::Item::SItemTemplate* candidateTemplate = m_itemDataTable->Find(candidate.itemDataId);
				if (candidateTemplate == nullptr)
				{
					outResult = EEquipmentResult::CharacterDataError;
					outError = "equipped inventory item references missing Item GameData.";
					return;
				}
				if (candidateTemplate->equipmentSlot != targetTemplate->equipmentSlot)
				{
					continue;
				}
				if (previousItem != nullptr)
				{
					outResult = EEquipmentResult::CharacterDataError;
					outError = "multiple equipped items already occupy the target EquipmentSlot.";
					return;
				}
				previousItem = &candidate;
			}
		}
		if (previousItem != nullptr && previousItem->version == std::numeric_limits<std::uint64_t>::max())
		{
			outResult = EEquipmentResult::ConcurrentModification;
			outError = "previous equipment item version cannot advance.";
			return;
		}

		Domain::FCachedInventoryItemMap prospectiveItems = user->GetInventoryItems();
		Domain::SCachedInventoryItem& prospectiveTarget = prospectiveItems.at(itemInstanceId);
		prospectiveTarget.equipped = equipped;
		++prospectiveTarget.version;
		if (previousItem != nullptr)
		{
			Domain::SCachedInventoryItem& prospectivePrevious = prospectiveItems.at(previousItem->itemInstanceId);
			prospectivePrevious.equipped = false;
			++prospectivePrevious.version;
		}

		SBuiltEquipmentState prospectiveEquipmentState;
		if (!TryBuildEquipmentState(prospectiveItems, *m_itemDataTable, prospectiveEquipmentState, outError))
		{
			outResult = EEquipmentResult::CharacterDataError;
			return;
		}
		if (!m_databaseConfig.enabled)
		{
			outResult = EEquipmentResult::DatabaseError;
			outError = "GameDB is disabled.";
			return;
		}

		auto& databaseContext = Database::FContentThreadDbContext::Get(m_databaseConfig);
		Connector::MySql::FMySqlConnection* connection = databaseContext.GetGamePrimary(outError);
		if (connection == nullptr)
		{
			outResult = EEquipmentResult::DatabaseError;
			return;
		}

		Connector::MySql::FMySqlTransaction transaction(*connection);
		Database::SPlayerEquipmentMutationResult equipResult;
		Database::SPlayerInventoryItemRow unequippedItem;
		if (!transaction.Begin(outError))
		{
			outResult = EEquipmentResult::DatabaseError;
			return;
		}

		const bool databaseMutationSucceeded =
			equipped ? Database::FPlayerCacheRepository(*connection)
						   .EquipPlayerItem(userId,
							   itemInstanceId,
							   expectedItemVersion,
							   previousItem == nullptr ? 0 : previousItem->itemInstanceId,
							   previousItem == nullptr ? 0 : previousItem->version,
							   equipResult,
							   outError)
					 : Database::FPlayerCacheRepository(*connection)
						   .UnequipPlayerItem(userId, itemInstanceId, expectedItemVersion, unequippedItem, outError);
		if (!databaseMutationSucceeded)
		{
			transaction.Rollback();
			bool databaseStateDiverged = false;
			if (outError.find("INVENTORY_ITEM_NOT_FOUND") != std::string::npos)
			{
				outResult = EEquipmentResult::ItemNotFound;
				databaseStateDiverged = true;
			}
			else if (outError.find("ITEM_VERSION_MISMATCH") != std::string::npos)
			{
				outResult = EEquipmentResult::ItemVersionMismatch;
				databaseStateDiverged = true;
			}
			else if (outError.find("EQUIPMENT_STATE_CONFLICT") != std::string::npos)
			{
				outResult = EEquipmentResult::EquipmentStateConflict;
				databaseStateDiverged = true;
			}
			else if (outError.find("PREVIOUS_EQUIPMENT_CONFLICT") != std::string::npos ||
					 outError.find("ITEM_VERSION_OVERFLOW") != std::string::npos)
			{
				outResult = EEquipmentResult::ConcurrentModification;
				databaseStateDiverged = true;
			}
			else
			{
				outResult = EEquipmentResult::DatabaseError;
			}
			if (databaseStateDiverged)
			{
				user->InvalidatePlayerData(std::chrono::steady_clock::now());
				outStateInvalidated = true;
			}
			return;
		}

		const Database::SPlayerInventoryItemRow& changedTarget = equipped ? equipResult.targetItem : unequippedItem;
		const bool targetMatchesCache = changedTarget.itemDataId == targetItem.itemDataId &&
										changedTarget.quantity == targetItem.quantity &&
										changedTarget.itemDataJson == targetItem.itemDataJson && changedTarget.equipped == equipped &&
										changedTarget.tradable == targetItem.tradable && changedTarget.version == targetItem.version + 1;
		const bool previousMatchesCache =
			previousItem == nullptr ||
			(equipResult.previousItem.has_value() && equipResult.previousItem->itemDataId == previousItem->itemDataId &&
				equipResult.previousItem->quantity == previousItem->quantity &&
				equipResult.previousItem->itemDataJson == previousItem->itemDataJson && !equipResult.previousItem->equipped &&
				equipResult.previousItem->tradable == previousItem->tradable &&
				equipResult.previousItem->version == previousItem->version + 1);
		if (!targetMatchesCache || !previousMatchesCache)
		{
			transaction.Rollback();
			user->InvalidatePlayerData(std::chrono::steady_clock::now());
			outStateInvalidated = true;
			outResult = EEquipmentResult::ConcurrentModification;
			outError = "GameDB equipment rows no longer match the cached inventory snapshot.";
			return;
		}
		if (!transaction.Commit(outError))
		{
			user->InvalidatePlayerData(std::chrono::steady_clock::now());
			outStateInvalidated = true;
			outResult = EEquipmentResult::OutcomeUnknown;
			return;
		}

		if (equipResult.previousItem.has_value())
		{
			user->UpsertInventoryItem(ToCachedInventoryItem(*equipResult.previousItem), std::chrono::steady_clock::now());
		}
		user->UpsertInventoryItem(ToCachedInventoryItem(changedTarget), std::chrono::steady_clock::now());
		user->AdvanceStateRevision(prospectiveEquipmentState.equipmentVersion, std::chrono::steady_clock::now());
		if (!ToRpcPlayerWorldSnapshot(*user, *m_itemDataTable, outSnapshot, outError))
		{
			user->InvalidatePlayerData(std::chrono::steady_clock::now());
			outStateInvalidated = true;
			outResult = EEquipmentResult::CharacterDataError;
			return;
		}

		outItemVersion = changedTarget.version;
		outEquipped = changedTarget.equipped;
	}

	void FPlayerCacheContent::HandleEnterUser(
		const RpcLib::Dispatch::FRpcCallContext& context,
		RpcLib::Dispatch::TRpcReply<ServerProtocol::UserPresence::FEnterUserRpc>& reply,
		const std::uint64_t userId,
		const std::uint64_t localClientSessionId)
	{
		using namespace ServerProtocol::UserPresence;

		const auto leaseMilliseconds = static_cast<FLeaseDurationMilliseconds>(m_cachePolicy.gameOwnerLeaseDuration.count());
		if (!IsAuthorizedGameCaller(context))
		{
			reply.Send(userId, EEnterUserResult::UnauthorizedCaller, FOwnerGeneration{0}, leaseMilliseconds);
			return;
		}
		if (userId == 0 || localClientSessionId == 0 || context.routingKey != userId)
		{
			reply.Send(userId, EEnterUserResult::InvalidRequest, FOwnerGeneration{0}, leaseMilliseconds);
			return;
		}

		bool loadedFromDatabase = false;
		Cache::Protocol::ECacheUserLoadResult loadResult = Cache::Protocol::ECacheUserLoadResult::Success;
		std::string error;
		Domain::FCacheUser* user = GetOrLoadUser(userId, loadedFromDatabase, loadResult, error);
		if (user == nullptr)
		{
			reply.Send(userId, EEnterUserResult::UserLoadFailed, FOwnerGeneration{0}, leaseMilliseconds);
			Log(Foundation::ELogLevel::Error,
				"EnterUser load failed. userId={} gameServerInstanceId={} result={} error={}",
				userId,
				context.peerServerInstanceId,
				static_cast<std::uint8_t>(loadResult),
				error);
			return;
		}

		const auto now = std::chrono::steady_clock::now();
		if (const Domain::SGameUserOwner* owner = user->GetGameOwner(); owner != nullptr && owner->leaseExpiresAt <= now)
		{
			user->ClearGameOwner(now);
		}
		if (user->IsSameGameOwner(context.rpcSessionId, context.peerServerInstanceId, localClientSessionId))
		{
			const Domain::SGameUserOwner* owner = user->GetGameOwner();
			if (owner == nullptr)
			{
				reply.Send(userId, EEnterUserResult::ServerBusy, FOwnerGeneration{0}, leaseMilliseconds);
				return;
			}

			user->RenewGameOwner(context.rpcSessionId,
				context.peerServerInstanceId,
				localClientSessionId,
				owner->ownerGeneration,
				now,
				now + m_cachePolicy.gameOwnerLeaseDuration);
			reply.Send(userId, EEnterUserResult::AlreadyEntered, owner->ownerGeneration, leaseMilliseconds);
			return;
		}

		std::optional<Domain::SGameUserOwner> previousOwner;
		if (const Domain::SGameUserOwner* owner = user->GetGameOwner(); owner != nullptr)
		{
			previousOwner = *owner;
		}

		Domain::SGameUserOwner newOwner;
		newOwner.rpcSessionId = context.rpcSessionId;
		newOwner.gameServerInstanceId = context.peerServerInstanceId;
		newOwner.gameClientSessionId = localClientSessionId;
		newOwner.ownerGeneration = NextOwnerGeneration();
		newOwner.leaseExpiresAt = now + m_cachePolicy.gameOwnerLeaseDuration;
		user->SetGameOwner(newOwner, now);

		const EEnterUserResult result =
			previousOwner.has_value() ? EEnterUserResult::ReplacedPreviousGameServer : EEnterUserResult::Entered;
		if (previousOwner.has_value())
		{
			SendRevokeUser(*previousOwner, userId, ERevokeUserReason::ReplacedByNewLogin);
		}

		reply.Send(userId, result, newOwner.ownerGeneration, leaseMilliseconds);
		Log(Foundation::ELogLevel::Info,
			"EnterUser completed. userId={} result={} gameServerInstanceId={} localClientSessionId={} ownerGeneration={} source={}",
			userId,
			static_cast<std::uint8_t>(result),
			context.peerServerInstanceId,
			localClientSessionId,
			newOwner.ownerGeneration,
			loadedFromDatabase ? "database" : "cache");
	}

	void FPlayerCacheContent::HandleLeaveUser(
		const RpcLib::Dispatch::FRpcCallContext& context,
		RpcLib::Dispatch::TRpcReply<ServerProtocol::UserPresence::FLeaveUserRpc>& reply,
		const std::uint64_t userId,
		const std::uint64_t localClientSessionId,
		const std::uint64_t ownerGeneration)
	{
		using namespace ServerProtocol::UserPresence;

		if (!IsAuthorizedGameCaller(context))
		{
			reply.Send(ELeaveUserResult::UnauthorizedCaller);
			return;
		}
		if (userId == 0 || localClientSessionId == 0 || ownerGeneration == 0 || context.routingKey != userId)
		{
			reply.Send(ELeaveUserResult::InvalidRequest);
			return;
		}

		Domain::FCacheUser* user = FindUser(userId);
		if (user == nullptr || !user->HasGameOwner())
		{
			reply.Send(ELeaveUserResult::AlreadyLeft);
			return;
		}
		if (!user->MatchesGameOwner(context.rpcSessionId, context.peerServerInstanceId, localClientSessionId, ownerGeneration))
		{
			reply.Send(ELeaveUserResult::StaleOwner);
			return;
		}

		user->ClearGameOwner(std::chrono::steady_clock::now());
		reply.Send(ELeaveUserResult::Left);
		Log(Foundation::ELogLevel::Info,
			"LeaveUser completed. userId={} gameServerInstanceId={} localClientSessionId={} ownerGeneration={}",
			userId,
			context.peerServerInstanceId,
			localClientSessionId,
			ownerGeneration);
	}

	void FPlayerCacheContent::HandleRenewUser(
		const RpcLib::Dispatch::FRpcCallContext& context,
		RpcLib::Dispatch::TRpcReply<ServerProtocol::UserPresence::FRenewUserRpc>& reply,
		const std::uint64_t userId,
		const std::uint64_t localClientSessionId,
		const std::uint64_t ownerGeneration)
	{
		using namespace ServerProtocol::UserPresence;

		if (!IsAuthorizedGameCaller(context))
		{
			reply.Send(ERenewUserResult::UnauthorizedCaller);
			return;
		}
		if (userId == 0 || localClientSessionId == 0 || ownerGeneration == 0 || context.routingKey != userId)
		{
			reply.Send(ERenewUserResult::InvalidRequest);
			return;
		}

		Domain::FCacheUser* user = FindUser(userId);
		if (user == nullptr || !user->HasGameOwner())
		{
			reply.Send(ERenewUserResult::OwnerNotFound);
			return;
		}
		const auto now = std::chrono::steady_clock::now();
		if (!user->RenewGameOwner(context.rpcSessionId,
				context.peerServerInstanceId,
				localClientSessionId,
				ownerGeneration,
				now,
				now + m_cachePolicy.gameOwnerLeaseDuration))
		{
			reply.Send(ERenewUserResult::StaleOwner);
			return;
		}

		reply.Send(ERenewUserResult::Renewed);
	}

	void FPlayerCacheContent::SendRevokeUser(
		const Domain::SGameUserOwner& previousOwner,
		const std::uint64_t userId,
		const ServerProtocol::UserPresence::ERevokeUserReason reason)
	{
		RpcLib::Protocol::FRpcTarget target;
		target.serverType = RpcLib::Protocol::ERpcServerType::Game;
		target.serverInstanceId = previousOwner.gameServerInstanceId;
		target.routingKey = userId;
		target.rpcSessionId = previousOwner.rpcSessionId;

		const auto callResult = m_rpcCommon.Call<ServerProtocol::UserPresence::FRevokeUserRpc>(
			target,
			m_cachePolicy.revokeTimeout,
			[this, userId, ownerGeneration = previousOwner.ownerGeneration](const ServerProtocol::UserPresence::ERevokeUserResult result)
			{
				Log(Foundation::ELogLevel::Info,
					"RevokeUser response. userId={} ownerGeneration={} result={}",
					userId,
					ownerGeneration,
					static_cast<std::uint8_t>(result));
			},
			[this, userId, ownerGeneration = previousOwner.ownerGeneration](const RpcLib::Protocol::FRpcCallFailure& failure)
			{
				Log(Foundation::ELogLevel::Warn,
					"RevokeUser failed. userId={} ownerGeneration={} error={} remote={}",
					userId,
					ownerGeneration,
					static_cast<std::uint8_t>(failure.error),
					static_cast<std::uint16_t>(failure.remoteResponseCode));
			},
			userId,
			previousOwner.gameClientSessionId,
			previousOwner.ownerGeneration,
			reason);

		if (!callResult.accepted)
		{
			Log(Foundation::ELogLevel::Warn,
				"RevokeUser start failed. userId={} gameServerInstanceId={} ownerGeneration={} error={}",
				userId,
				previousOwner.gameServerInstanceId,
				previousOwner.ownerGeneration,
				static_cast<std::uint8_t>(callResult.error));
		}
	}

	std::uint64_t FPlayerCacheContent::NextOwnerGeneration() noexcept
	{
		std::uint64_t generation = m_ownerGenerationSequence.fetch_add(1, std::memory_order_relaxed);
		if (generation == 0)
		{
			generation = m_ownerGenerationSequence.fetch_add(1, std::memory_order_relaxed);
		}
		return generation;
	}

	bool FPlayerCacheContent::IsAuthorizedGameCaller(
		const RpcLib::Dispatch::FRpcCallContext& context) const noexcept
	{
		return context.rpcSessionId != 0 && context.peerServerType == RpcLib::Protocol::ERpcServerType::Game &&
			   context.peerServerInstanceId != 0;
	}

	bool FPlayerCacheContent::IsAuthorizedDataQueryCaller(
		const RpcLib::Dispatch::FRpcCallContext& context) const noexcept
	{
		const bool supportedServerType = context.peerServerType == RpcLib::Protocol::ERpcServerType::Auction ||
										 context.peerServerType == RpcLib::Protocol::ERpcServerType::Game;
		return context.rpcSessionId != 0 && context.peerServerInstanceId != 0 && supportedServerType;
	}

	bool FPlayerCacheContent::IsAuthorizedDataCommandCaller(
		const RpcLib::Dispatch::FRpcCallContext& context) const noexcept
	{
		return IsAuthorizedDataQueryCaller(context);
	}

	bool FPlayerCacheContent::IsAuthorizedAuctionCaller(
		const RpcLib::Dispatch::FRpcCallContext& context) const noexcept
	{
		return context.rpcSessionId != 0 && context.peerServerType == RpcLib::Protocol::ERpcServerType::Auction &&
			   context.peerServerInstanceId != 0;
	}

	bool FPlayerCacheContent::IsCurrentGameOwner(
		const RpcLib::Dispatch::FRpcCallContext& context,
		const Domain::FCacheUser& user,
		const std::uint64_t gameClientSessionId,
		const std::uint64_t ownerGeneration) const noexcept
	{
		const Domain::SGameUserOwner* owner = user.GetGameOwner();
		return IsAuthorizedGameCaller(context) && gameClientSessionId != 0 && ownerGeneration != 0 && owner != nullptr &&
			   owner->leaseExpiresAt > std::chrono::steady_clock::now() &&
			   user.MatchesGameOwner(context.rpcSessionId, context.peerServerInstanceId, gameClientSessionId, ownerGeneration);
	}

	Domain::FCacheUser* FPlayerCacheContent::GetCurrentGameOwnerUser(
		const RpcLib::Dispatch::FRpcCallContext& context,
		const std::uint64_t userId,
		const std::uint64_t gameClientSessionId,
		const std::uint64_t ownerGeneration,
		Cache::Protocol::EPlayerProgressResult& outResult,
		std::string& outError)
	{
		using Cache::Protocol::EPlayerProgressResult;
		Domain::FCacheUser* user = FindUser(userId);
		if (user == nullptr || !IsCurrentGameOwner(context, *user, gameClientSessionId, ownerGeneration))
		{
			outResult = EPlayerProgressResult::UnauthorizedCaller;
			outError = "request requires the current Game owner.";
			return nullptr;
		}
		if (user->IsDataValid())
		{
			return user;
		}

		bool loadedFromDatabase = false;
		Cache::Protocol::ECacheUserLoadResult loadResult = Cache::Protocol::ECacheUserLoadResult::Success;
		user = GetOrLoadUser(userId, loadedFromDatabase, loadResult, outError);
		if (user == nullptr)
		{
			outResult = loadResult == Cache::Protocol::ECacheUserLoadResult::InvalidSnapshot ? EPlayerProgressResult::CharacterDataError
																							 : EPlayerProgressResult::DatabaseError;
			return nullptr;
		}
		if (!IsCurrentGameOwner(context, *user, gameClientSessionId, ownerGeneration))
		{
			outResult = EPlayerProgressResult::UnauthorizedCaller;
			outError = "Game owner changed while reloading player data.";
			return nullptr;
		}
		return user;
	}

	bool FPlayerCacheContent::RefreshStateRevision(
		Domain::FCacheUser& user,
		std::string& outError) const
	{
		SBuiltEquipmentState equipmentState;
		if (!TryBuildEquipmentState(user.GetInventoryItems(), *m_itemDataTable, equipmentState, outError))
		{
			return false;
		}

		user.AdvanceStateRevision(equipmentState.equipmentVersion, std::chrono::steady_clock::now());
		return true;
	}

	void FPlayerCacheContent::RunMaintenance(
		const std::chrono::steady_clock::time_point now)
	{
		for (auto it = m_users.begin(); it != m_users.end();)
		{
			Domain::FCacheUser& user = *it->second;
			if (const Domain::SGameUserOwner* owner = user.GetGameOwner(); owner != nullptr)
			{
				const std::shared_ptr<RpcLib::Session::FRpcSession> rpcSession = m_sessionRegistry.Find(owner->rpcSessionId);
				const bool disconnected = rpcSession == nullptr || !rpcSession->IsReady();
				const bool leaseExpired = owner->leaseExpiresAt <= now;
				if (disconnected || leaseExpired)
				{
					const std::optional<Domain::SGameUserOwner> previousOwner = user.ClearGameOwner(now);
					if (leaseExpired && !disconnected && previousOwner.has_value())
					{
						SendRevokeUser(*previousOwner, user.GetUserId(), ServerProtocol::UserPresence::ERevokeUserReason::LeaseExpired);
					}

					Log(Foundation::ELogLevel::Info,
						"Game owner released by maintenance. userId={} reason={} ownerGeneration={}",
						user.GetUserId(),
						disconnected ? "disconnected" : "lease_expired",
						previousOwner.has_value() ? previousOwner->ownerGeneration : 0);
				}
			}

			if (!user.HasGameOwner() && now - user.GetLastAccessedAt() >= m_cachePolicy.idleEvictionDuration)
			{
				Log(Foundation::ELogLevel::Info, "Player cache evicted. userId={} reason=idle", user.GetUserId());
				it = m_users.erase(it);
				continue;
			}

			++it;
		}
	}

	Domain::FCacheUser* FPlayerCacheContent::FindUser(
		const std::uint64_t userId) noexcept
	{
		const auto it = m_users.find(userId);
		return it == m_users.end() ? nullptr : it->second.get();
	}

	Domain::FCacheUser* FPlayerCacheContent::GetOrLoadUser(
		const std::uint64_t userId,
		bool& outLoadedFromDatabase,
		Cache::Protocol::ECacheUserLoadResult& outResult,
		std::string& outError)
	{
		outLoadedFromDatabase = false;
		outResult = Cache::Protocol::ECacheUserLoadResult::Success;
		outError.clear();

		if (userId == 0)
		{
			outResult = Cache::Protocol::ECacheUserLoadResult::InvalidUserId;
			outError = "userId must not be zero.";
			return nullptr;
		}
		if (GetPlayerCacheShardIndex(userId, m_shardCount) != m_shardIndex)
		{
			outResult = Cache::Protocol::ECacheUserLoadResult::InvalidUserId;
			outError = "userId does not belong to this shard.";
			return nullptr;
		}

		Domain::FCacheUser* cachedUser = FindUser(userId);
		if (cachedUser != nullptr && cachedUser->IsDataValid())
		{
			cachedUser->Touch(std::chrono::steady_clock::now());
			return cachedUser;
		}

		if (!m_databaseConfig.enabled)
		{
			outResult = Cache::Protocol::ECacheUserLoadResult::DatabaseError;
			outError = "GameDB is disabled.";
			return nullptr;
		}

		auto& databaseContext = Database::FContentThreadDbContext::Get(m_databaseConfig);
		Connector::MySql::FMySqlConnection* connection = databaseContext.GetGamePrimary(outError);
		if (connection == nullptr)
		{
			outResult = Cache::Protocol::ECacheUserLoadResult::DatabaseError;
			return nullptr;
		}

		Database::SPlayerCacheSnapshot databaseSnapshot;
		Database::FPlayerCacheRepository repository(*connection);
		if (!repository.LoadPlayerSnapshot(userId, databaseSnapshot, outError))
		{
			outResult = Cache::Protocol::ECacheUserLoadResult::DatabaseError;
			return nullptr;
		}
		if (!databaseSnapshot.character.has_value())
		{
			const std::vector<const GameData::Character::SCharacterData*> characters = m_characterDataTable->GetAll();
			if (characters.empty())
			{
				outResult = Cache::Protocol::ECacheUserLoadResult::InvalidSnapshot;
				outError = "Character GameData is empty.";
				return nullptr;
			}

			const GameData::Character::SCharacterData& initialCharacter = *characters.front();
			Database::SPlayerCharacterRow createdCharacter;
			Connector::MySql::FMySqlTransaction transaction(*connection);
			if (!transaction.Begin(outError))
			{
				outResult = Cache::Protocol::ECacheUserLoadResult::DatabaseError;
				return nullptr;
			}
			if (!repository.CreatePlayerCharacter(userId,
					initialCharacter.characterDataId,
					initialCharacter.initialLevel,
					0,
					initialCharacter.initialStr,
					initialCharacter.initialDex,
					initialCharacter.initialInt,
					initialCharacter.initialLuk,
					initialCharacter.initialUnspentStatPoints,
					createdCharacter,
					outError))
			{
				transaction.Rollback();
				if (outError.find("PLAYER_CHARACTER_ALREADY_EXISTS") == std::string::npos)
				{
					outResult = Cache::Protocol::ECacheUserLoadResult::DatabaseError;
					return nullptr;
				}

				bool found = false;
				if (!repository.LoadPlayerCharacter(userId, createdCharacter, found, outError) || !found)
				{
					outResult = Cache::Protocol::ECacheUserLoadResult::DatabaseError;
					return nullptr;
				}
			}
			else if (!transaction.Commit(outError))
			{
				outResult = Cache::Protocol::ECacheUserLoadResult::DatabaseError;
				return nullptr;
			}

			databaseSnapshot.character = createdCharacter;
			Log(Foundation::ELogLevel::Info,
				"Player character created. userId={} characterId={} characterDataId={} initialLevel={}",
				userId,
				createdCharacter.characterId,
				createdCharacter.characterDataId,
				createdCharacter.level);
		}

		Domain::SPlayerCacheSnapshot domainSnapshot;
		domainSnapshot.userId = databaseSnapshot.userId;
		domainSnapshot.progress = ToDomainProgress(*databaseSnapshot.character);
		domainSnapshot.currencies.reserve(databaseSnapshot.currencies.size());
		for (auto& [currencyId, row] : databaseSnapshot.currencies)
		{
			Domain::SCachedCurrency currency;
			currency.currencyId = row.currencyId;
			currency.amount = row.amount;
			currency.version = row.version;
			domainSnapshot.currencies.emplace(currencyId, std::move(currency));
		}
		domainSnapshot.inventoryItems.reserve(databaseSnapshot.inventoryItems.size());
		for (auto& [itemInstanceId, row] : databaseSnapshot.inventoryItems)
		{
			Domain::SCachedInventoryItem item;
			item.itemInstanceId = row.itemInstanceId;
			item.itemDataId = row.itemDataId;
			item.quantity = row.quantity;
			item.itemDataJson = std::move(row.itemDataJson);
			item.equipped = row.equipped;
			item.tradable = row.tradable;
			item.version = row.version;
			domainSnapshot.inventoryItems.emplace(itemInstanceId, std::move(item));
		}

		const auto loadedAt = std::chrono::system_clock::now();
		const auto lastAccessedAt = std::chrono::steady_clock::now();
		if (cachedUser != nullptr)
		{
			if (!cachedUser->ReplacePlayerData(std::move(domainSnapshot), loadedAt, lastAccessedAt))
			{
				outResult = Cache::Protocol::ECacheUserLoadResult::InvalidSnapshot;
				outError = "GameDB returned a cache snapshot that could not replace the invalidated entry.";
				return nullptr;
			}
			if (!RefreshStateRevision(*cachedUser, outError))
			{
				cachedUser->InvalidatePlayerData(lastAccessedAt);
				outResult = Cache::Protocol::ECacheUserLoadResult::InvalidSnapshot;
				return nullptr;
			}

			outLoadedFromDatabase = true;
			Log(Foundation::ELogLevel::Info,
				"Player cache reloaded. userId={} shardIndex={} source=primary currencyCount={} inventoryItemCount={} ownerPreserved={}",
				userId,
				m_shardIndex,
				cachedUser->GetCurrencyCount(),
				cachedUser->GetInventoryItemCount(),
				cachedUser->HasGameOwner());
			return cachedUser;
		}

		std::unique_ptr<Domain::FCacheUser> loadedUser = Domain::FCacheUser::Create(std::move(domainSnapshot), loadedAt, lastAccessedAt);
		if (loadedUser == nullptr)
		{
			outResult = Cache::Protocol::ECacheUserLoadResult::InvalidSnapshot;
			outError = "GameDB returned an invalid player cache snapshot.";
			return nullptr;
		}
		if (!RefreshStateRevision(*loadedUser, outError))
		{
			outResult = Cache::Protocol::ECacheUserLoadResult::InvalidSnapshot;
			return nullptr;
		}

		Domain::FCacheUser* loadedUserPointer = loadedUser.get();
		const auto [it, inserted] = m_users.emplace(userId, std::move(loadedUser));
		if (!inserted)
		{
			it->second->Touch(lastAccessedAt);
			return it->second.get();
		}

		outLoadedFromDatabase = true;
		Log(Foundation::ELogLevel::Info,
			"Player cache loaded. userId={} shardIndex={} source=primary currencyCount={} inventoryItemCount={}",
			userId,
			m_shardIndex,
			loadedUserPointer->GetCurrencyCount(),
			loadedUserPointer->GetInventoryItemCount());
		return loadedUserPointer;
	}

	void FPlayerCacheContent::Log(
		const Foundation::ELogLevel level,
		const std::string& message) const
	{
		if (m_logger != nullptr)
		{
			m_logger->Log(level, "CacheServer", message);
		}
	}
}
