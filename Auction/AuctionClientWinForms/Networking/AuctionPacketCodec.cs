using System.Buffers.Binary;
using AuctionClientWinForms.Models;

namespace AuctionClientWinForms.Networking;

internal sealed record DecodedPacket(ushort Opcode, byte[] Body);

internal static class AuctionPacketCodec
{
    public const ushort AuctionAuthRqOpcode = 3998;
    public const ushort AuctionAuthRpOpcode = 3999;
    public const ushort MyBidListRqOpcode = 4010;
    public const ushort MyBidListRpOpcode = 4011;
    public const ushort BidRefundRqOpcode = 4012;
    public const ushort BidRefundRpOpcode = 4013;
    public const ushort InventoryListRqOpcode = 4014;
    public const ushort InventoryListRpOpcode = 4015;
    public const ushort ListingRegisterRqOpcode = 4016;
    public const ushort ListingRegisterRpOpcode = 4017;
    public const ushort ListingSearchRqOpcode = 4018;
    public const ushort ListingSearchRpOpcode = 4019;
    public const ushort ListingDetailRqOpcode = 4020;
    public const ushort ListingDetailRpOpcode = 4021;
    public const ushort BidRqOpcode = 4022;
    public const ushort BidRpOpcode = 4023;
    public const ushort AuctionOutbidNotiOpcode = 4024;
    public const ushort BuyoutRqOpcode = 4025;
    public const ushort BuyoutRpOpcode = 4026;
    public const ushort MailListRqOpcode = 4027;
    public const ushort MailListRpOpcode = 4028;
    public const ushort MailDetailRqOpcode = 4029;
    public const ushort MailDetailRpOpcode = 4030;
    public const ushort MailClaimRqOpcode = 4031;
    public const ushort MailClaimRpOpcode = 4032;
    public const ushort ListingCancelRqOpcode = 4033;
    public const ushort ListingCancelRpOpcode = 4034;
    public const ushort AuctionWonNotiOpcode = 4035;
    public const ushort DebugCheatRqOpcode = 4036;
    public const ushort DebugCheatRpOpcode = 4037;
    public const ushort SaleHistorySearchRqOpcode = 4038;
    public const ushort SaleHistorySearchRpOpcode = 4039;
    public const ushort SaleHistoryDetailRqOpcode = 4040;
    public const ushort SaleHistoryDetailRpOpcode = 4041;

    private const int TransportHeaderSize = 4;
    private const int ContentHeaderSize = 2;

    public static byte[] CreateAuthRequest(ulong requestId, string ticket, byte packetKey) =>
        BuildPacket(AuctionAuthRqOpcode, writer =>
        {
            writer.WriteUInt64(requestId);
            writer.WriteString(ticket);
        }, packetKey);

    public static byte[] CreateListingSearchRequest(
        ulong requestId,
        byte itemCategory,
        IReadOnlyList<uint> itemDataIds,
        uint minStr,
        uint minDex,
        uint minInt,
        uint minLuk,
        bool sellerOnly,
		byte sortType,
		ulong cursorSortValue,
        ulong cursorListingId,
        uint limit,
        byte packetKey) => BuildPacket(ListingSearchRqOpcode, writer =>
    {
        writer.WriteUInt64(requestId);
        writer.WriteByte(itemCategory);
        writer.WriteUInt32List(itemDataIds);
        writer.WriteUInt32(minStr);
        writer.WriteUInt32(minDex);
        writer.WriteUInt32(minInt);
        writer.WriteUInt32(minLuk);
        writer.WriteByte(sellerOnly ? (byte)1 : (byte)0);
		writer.WriteByte(sortType);
		writer.WriteUInt64(cursorSortValue);
        writer.WriteUInt64(cursorListingId);
        writer.WriteUInt32(limit);
    }, packetKey);

    public static byte[] CreateSaleHistorySearchRequest(
        ulong requestId,
        byte itemCategory,
        IReadOnlyList<uint> itemDataIds,
        uint minStr,
        uint minDex,
        uint minInt,
        uint minLuk,
		byte sortType,
		ulong cursorSortValue,
        ulong cursorListingId,
        uint limit,
        byte packetKey) => BuildPacket(SaleHistorySearchRqOpcode, writer =>
    {
        writer.WriteUInt64(requestId);
        writer.WriteByte(itemCategory);
        writer.WriteUInt32List(itemDataIds);
        writer.WriteUInt32(minStr);
        writer.WriteUInt32(minDex);
        writer.WriteUInt32(minInt);
        writer.WriteUInt32(minLuk);
		writer.WriteByte(sortType);
		writer.WriteUInt64(cursorSortValue);
        writer.WriteUInt64(cursorListingId);
        writer.WriteUInt32(limit);
    }, packetKey);

    public static byte[] CreateSaleHistoryDetailRequest(ulong requestId, ulong listingId, byte packetKey) =>
        BuildPacket(SaleHistoryDetailRqOpcode, writer =>
        {
            writer.WriteUInt64(requestId);
            writer.WriteUInt64(listingId);
        }, packetKey);

    public static byte[] CreateListingDetailRequest(ulong requestId, ulong listingId, byte packetKey) =>
        BuildPacket(ListingDetailRqOpcode, writer =>
        {
            writer.WriteUInt64(requestId);
            writer.WriteUInt64(listingId);
        }, packetKey);

    public static byte[] CreateBidRequest(ulong requestId, ulong listingId, ulong bidAmount, ulong version, byte packetKey) =>
        BuildPacket(BidRqOpcode, writer =>
        {
            writer.WriteUInt64(requestId);
            writer.WriteUInt64(listingId);
            writer.WriteUInt64(bidAmount);
            writer.WriteUInt64(version);
        }, packetKey);

    public static byte[] CreateBuyoutRequest(ulong requestId, ulong listingId, ulong version, byte packetKey) =>
        BuildPacket(BuyoutRqOpcode, writer =>
        {
            writer.WriteUInt64(requestId);
            writer.WriteUInt64(listingId);
            writer.WriteUInt64(version);
        }, packetKey);

    public static byte[] CreateMyBidListRequest(ulong requestId, byte packetKey) =>
        BuildPacket(MyBidListRqOpcode, writer =>
        {
            writer.WriteUInt64(requestId);
            writer.WriteUInt64(0);
            writer.WriteUInt32(100);
        }, packetKey);

    public static byte[] CreateBidRefundRequest(
        ulong requestId,
        ulong listingId,
        ulong bidId,
        ulong bidVersion,
        byte packetKey) => BuildPacket(BidRefundRqOpcode, writer =>
    {
        writer.WriteUInt64(requestId);
        writer.WriteUInt64(listingId);
        writer.WriteUInt64(bidId);
        writer.WriteUInt64(bidVersion);
    }, packetKey);

    public static byte[] CreateInventoryListRequest(ulong requestId, byte packetKey) =>
        BuildPacket(InventoryListRqOpcode, writer =>
        {
            writer.WriteUInt64(requestId);
            writer.WriteUInt64(0);
            writer.WriteUInt32(100);
        }, packetKey);

    public static byte[] CreateListingRegisterRequest(
        ulong requestId,
        InventoryItem item,
        ushort currencyId,
        ulong startPrice,
        ulong buyoutPrice,
        uint durationSeconds,
        byte packetKey) => BuildPacket(ListingRegisterRqOpcode, writer =>
    {
        writer.WriteUInt64(requestId);
        writer.WriteUInt64(item.ItemInstanceId);
        writer.WriteUInt64(item.Version);
        writer.WriteUInt16(currencyId);
        writer.WriteUInt64(startPrice);
        writer.WriteUInt64(buyoutPrice);
        writer.WriteUInt32(durationSeconds);
    }, packetKey);

    public static byte[] CreateMailListRequest(ulong requestId, byte packetKey) =>
        BuildPacket(MailListRqOpcode, writer =>
        {
            writer.WriteUInt64(requestId);
            writer.WriteUInt64(0);
            writer.WriteUInt32(100);
        }, packetKey);

    public static byte[] CreateMailDetailRequest(ulong requestId, ulong mailId, byte packetKey) =>
        BuildPacket(MailDetailRqOpcode, writer =>
        {
            writer.WriteUInt64(requestId);
            writer.WriteUInt64(mailId);
        }, packetKey);

    public static byte[] CreateMailClaimRequest(ulong requestId, ulong mailId, ulong attachmentId, byte packetKey) =>
        BuildPacket(MailClaimRqOpcode, writer =>
        {
            writer.WriteUInt64(requestId);
            writer.WriteUInt64(mailId);
            writer.WriteUInt64(attachmentId);
        }, packetKey);

    public static byte[] CreateListingCancelRequest(
        ulong requestId,
        ulong listingId,
        ulong expectedListingVersion,
        byte packetKey) => BuildPacket(ListingCancelRqOpcode, writer =>
    {
        writer.WriteUInt64(requestId);
        writer.WriteUInt64(listingId);
        writer.WriteUInt64(expectedListingVersion);
    }, packetKey);

    public static byte[] CreateDebugCheatRequest(
        ulong requestId,
        byte cheatType,
        ulong amount,
        uint itemDataId,
        uint strength,
        uint dexterity,
        uint intelligence,
        uint luck,
        byte packetKey) => BuildPacket(DebugCheatRqOpcode, writer =>
    {
        writer.WriteUInt64(requestId);
        writer.WriteByte(cheatType);
        writer.WriteUInt64(amount);
        writer.WriteUInt32(itemDataId);
        writer.WriteUInt32(strength);
        writer.WriteUInt32(dexterity);
        writer.WriteUInt32(intelligence);
        writer.WriteUInt32(luck);
    }, packetKey);

    public static bool TryExtractPacket(
        List<byte> receiveBuffer,
        byte packetKey,
        out DecodedPacket? packet,
        out string? error)
    {
        packet = null;
        error = null;
        if (receiveBuffer.Count < TransportHeaderSize)
        {
            return false;
        }

        ushort payloadLength = (ushort)(receiveBuffer[0] | (receiveBuffer[1] << 8));
        int packetLength = TransportHeaderSize + payloadLength;
        if (receiveBuffer.Count < packetLength)
        {
            return false;
        }

        byte randomKey = receiveBuffer[2];
        byte checksum = receiveBuffer[3];
        byte[] payload = receiveBuffer.GetRange(TransportHeaderSize, payloadLength).ToArray();
        receiveBuffer.RemoveRange(0, packetLength);
        if (CalculateChecksum(payload) != checksum)
        {
            error = "packet checksum mismatch.";
            return false;
        }

        DecodeInPlace(payload, randomKey, packetKey);
        if (payload.Length < ContentHeaderSize)
        {
            error = "content payload too short.";
            return false;
        }

        packet = new DecodedPacket(
            BinaryPrimitives.ReadUInt16LittleEndian(payload.AsSpan(0, ContentHeaderSize)),
            payload.AsSpan(ContentHeaderSize).ToArray());
        return true;
    }

    public static bool TryGetResponseRequestId(DecodedPacket packet, out ulong requestId)
    {
        requestId = 0;
        var reader = new PacketBinaryReader(packet.Body);
        return reader.TryReadUInt16(out _) && reader.TryReadUInt64(out requestId);
    }

    public static bool TryReadAuth(DecodedPacket packet, out AuctionAuthResult result)
    {
        result = new(ushort.MaxValue, 0, 0, 0, 0, 0, 0, 0);
        var reader = new PacketBinaryReader(packet.Body);
        if (packet.Opcode != AuctionAuthRpOpcode || !reader.TryReadUInt16(out ushort code) ||
			!reader.TryReadUInt64(out ulong requestId) || !reader.TryReadUInt64(out ulong userId) ||
			!reader.TryReadUInt32(out uint maxActiveListings) ||
			!reader.TryReadUInt32(out uint searchPageSize) ||
			!reader.TryReadUInt32(out uint minimumListingDurationSeconds) ||
			!reader.TryReadUInt32(out uint maximumListingDurationSeconds) ||
			!reader.TryReadUInt32(out uint defaultListingDurationSeconds) || !reader.IsAtEnd)
        {
            return false;
        }
		result = new(code,
			requestId,
			userId,
			maxActiveListings,
			searchPageSize,
			minimumListingDurationSeconds,
			maximumListingDurationSeconds,
			defaultListingDurationSeconds);
        return true;
    }

    public static bool TryReadListingSearch(DecodedPacket packet, out ushort resultCode, out IReadOnlyList<ListingSummary> listings)
    {
        resultCode = ushort.MaxValue;
        listings = [];
        var reader = new PacketBinaryReader(packet.Body);
        if (packet.Opcode != ListingSearchRpOpcode || !reader.TryReadUInt16(out resultCode) || !reader.TryReadUInt64(out _) ||
            !reader.TryReadUInt64List(out List<ulong> listingIds) || !reader.TryReadUInt64List(out List<ulong> sellerIds) ||
            !reader.TryReadStringList(out List<string> sellerLoginIds) ||
            !reader.TryReadUInt32List(out List<uint> itemDataIds) || !reader.TryReadByteList(out List<byte> categories) ||
            !reader.TryReadUInt32List(out List<uint> quantities) || !reader.TryReadStringList(out List<string> names) ||
            !reader.TryReadUInt32List(out List<uint> strengths) || !reader.TryReadUInt32List(out List<uint> dexterities) ||
            !reader.TryReadUInt32List(out List<uint> intelligences) || !reader.TryReadUInt32List(out List<uint> lucks) ||
            !reader.TryReadUInt16List(out List<ushort> currencyIds) || !reader.TryReadUInt64List(out List<ulong> startPrices) ||
            !reader.TryReadUInt64List(out List<ulong> bidPrices) || !reader.TryReadUInt64List(out List<ulong> buyoutPrices) ||
            !reader.TryReadUInt64List(out List<ulong> expires) || !reader.TryReadUInt64List(out List<ulong> versions) || !reader.IsAtEnd)
        {
            return false;
        }

        int count = listingIds.Count;
        if (!AllCountsEqual(count, sellerIds.Count, sellerLoginIds.Count, itemDataIds.Count, categories.Count, quantities.Count, names.Count,
                strengths.Count, dexterities.Count, intelligences.Count, lucks.Count, currencyIds.Count, startPrices.Count,
                bidPrices.Count, buyoutPrices.Count, expires.Count, versions.Count))
        {
            return false;
        }

        List<ListingSummary> parsed = new(count);
        for (int i = 0; i < count; ++i)
        {
            parsed.Add(new(listingIds[i], sellerIds[i], sellerLoginIds[i], itemDataIds[i], categories[i], quantities[i], names[i],
                strengths[i], dexterities[i], intelligences[i], lucks[i], currencyIds[i], startPrices[i], bidPrices[i],
                buyoutPrices[i], expires[i], versions[i]));
        }
        listings = parsed;
        return true;
    }

    public static bool TryReadSaleHistorySearch(
        DecodedPacket packet,
        out ushort resultCode,
        out IReadOnlyList<SaleHistorySummary> history)
    {
        resultCode = ushort.MaxValue;
        history = [];
        var reader = new PacketBinaryReader(packet.Body);
        if (packet.Opcode != SaleHistorySearchRpOpcode || !reader.TryReadUInt16(out resultCode) || !reader.TryReadUInt64(out _) ||
            !reader.TryReadUInt64List(out List<ulong> listingIds) || !reader.TryReadUInt32List(out List<uint> itemDataIds) ||
            !reader.TryReadByteList(out List<byte> categories) || !reader.TryReadUInt32List(out List<uint> quantities) ||
            !reader.TryReadStringList(out List<string> names) || !reader.TryReadUInt32List(out List<uint> strengths) ||
            !reader.TryReadUInt32List(out List<uint> dexterities) || !reader.TryReadUInt32List(out List<uint> intelligences) ||
            !reader.TryReadUInt32List(out List<uint> lucks) || !reader.TryReadUInt16List(out List<ushort> currencyIds) ||
            !reader.TryReadUInt64List(out List<ulong> finalPrices) || !reader.TryReadByteList(out List<byte> saleTypes) ||
            !reader.TryReadUInt64List(out List<ulong> soldAt) || !reader.IsAtEnd)
        {
            return false;
        }

        int count = listingIds.Count;
        if (!AllCountsEqual(count, itemDataIds.Count, categories.Count, quantities.Count, names.Count, strengths.Count,
                dexterities.Count, intelligences.Count, lucks.Count, currencyIds.Count, finalPrices.Count, saleTypes.Count, soldAt.Count))
        {
            return false;
        }

        List<SaleHistorySummary> parsed = new(count);
        for (int i = 0; i < count; ++i)
        {
            parsed.Add(new(listingIds[i], itemDataIds[i], categories[i], quantities[i], names[i], strengths[i],
                dexterities[i], intelligences[i], lucks[i], currencyIds[i], finalPrices[i], saleTypes[i], soldAt[i]));
        }
        history = parsed;
        return true;
    }

    public static bool TryReadSaleHistoryDetail(DecodedPacket packet, out ushort resultCode, out SaleHistoryDetail? detail)
    {
        resultCode = ushort.MaxValue;
        detail = null;
        var reader = new PacketBinaryReader(packet.Body);
        if (packet.Opcode != SaleHistoryDetailRpOpcode || !reader.TryReadUInt16(out resultCode) || !reader.TryReadUInt64(out _) ||
            !reader.TryReadUInt64(out ulong listingId) || !reader.TryReadString(out string sellerLoginId) ||
            !reader.TryReadUInt32(out uint itemDataId) || !reader.TryReadByte(out byte category) ||
            !reader.TryReadUInt32(out uint quantity) || !reader.TryReadString(out string itemData) ||
            !reader.TryReadString(out string name) || !reader.TryReadUInt32(out uint strength) ||
            !reader.TryReadUInt32(out uint dexterity) || !reader.TryReadUInt32(out uint intelligence) ||
            !reader.TryReadUInt32(out uint luck) || !reader.TryReadUInt16(out ushort currencyId) ||
            !reader.TryReadUInt64(out ulong startPrice) || !reader.TryReadUInt64(out ulong finalPrice) ||
            !reader.TryReadByte(out byte saleType) || !reader.TryReadUInt64(out ulong soldAt) || !reader.IsAtEnd)
        {
            return false;
        }
        detail = new(listingId, sellerLoginId, itemDataId, category, quantity, itemData, name, strength, dexterity,
            intelligence, luck, currencyId, startPrice, finalPrice, saleType, soldAt);
        return true;
    }

    public static bool TryReadListingDetail(DecodedPacket packet, out ushort resultCode, out ListingDetail? detail)
    {
        resultCode = ushort.MaxValue;
        detail = null;
        var reader = new PacketBinaryReader(packet.Body);
        if (packet.Opcode != ListingDetailRpOpcode || !reader.TryReadUInt16(out resultCode) || !reader.TryReadUInt64(out _) ||
            !reader.TryReadUInt64(out ulong listingId) || !reader.TryReadUInt64(out ulong sellerId) ||
            !reader.TryReadString(out string sellerLoginId) ||
            !reader.TryReadUInt64(out ulong itemInstanceId) || !reader.TryReadUInt32(out uint itemDataId) ||
            !reader.TryReadByte(out byte category) || !reader.TryReadUInt32(out uint quantity) ||
            !reader.TryReadString(out string itemData) || !reader.TryReadString(out string name) ||
            !reader.TryReadUInt32(out uint strength) || !reader.TryReadUInt32(out uint dexterity) ||
            !reader.TryReadUInt32(out uint intelligence) || !reader.TryReadUInt32(out uint luck) ||
            !reader.TryReadUInt16(out ushort currencyId) || !reader.TryReadUInt64(out ulong startPrice) ||
            !reader.TryReadUInt64(out ulong bidPrice) || !reader.TryReadUInt64(out ulong buyoutPrice) ||
            !reader.TryReadUInt64(out ulong highestBidderId) || !reader.TryReadUInt64(out ulong expires) ||
            !reader.TryReadUInt64(out ulong version) || !reader.IsAtEnd)
        {
            return false;
        }
        detail = new(listingId, sellerId, sellerLoginId, itemInstanceId, itemDataId, category, quantity, itemData, name, strength,
            dexterity, intelligence, luck, currencyId, startPrice, bidPrice, buyoutPrice, highestBidderId, expires, version);
        return true;
    }

    public static bool TryReadMyBidList(DecodedPacket packet, out ushort resultCode, out IReadOnlyList<BidSummary> bids)
    {
        resultCode = ushort.MaxValue;
        bids = [];
        var reader = new PacketBinaryReader(packet.Body);
        if (packet.Opcode != MyBidListRpOpcode || !reader.TryReadUInt16(out resultCode) || !reader.TryReadUInt64(out _) ||
            !reader.TryReadUInt64List(out List<ulong> bidIds) || !reader.TryReadUInt64List(out List<ulong> listingIds) ||
            !reader.TryReadUInt16List(out List<ushort> currencyIds) || !reader.TryReadUInt64List(out List<ulong> bidAmounts) ||
            !reader.TryReadByteList(out List<byte> bidStates) || !reader.TryReadUInt64List(out List<ulong> bidVersions) ||
            !reader.TryReadUInt64List(out List<ulong> currentPrices) || !reader.TryReadByteList(out List<byte> listingStates) ||
            !reader.IsAtEnd)
        {
            return false;
        }
        int count = bidIds.Count;
        if (!AllCountsEqual(count, listingIds.Count, currencyIds.Count, bidAmounts.Count, bidStates.Count, bidVersions.Count,
                currentPrices.Count, listingStates.Count))
        {
            return false;
        }
        List<BidSummary> parsed = new(count);
        for (int i = 0; i < count; ++i)
        {
            parsed.Add(new(bidIds[i], listingIds[i], currencyIds[i], bidAmounts[i], bidStates[i], bidVersions[i],
                currentPrices[i], listingStates[i]));
        }
        bids = parsed;
        return true;
    }

    public static bool TryReadInventory(DecodedPacket packet, out ushort resultCode, out IReadOnlyList<InventoryItem> items)
    {
        resultCode = ushort.MaxValue;
        items = [];
        var reader = new PacketBinaryReader(packet.Body);
        if (packet.Opcode != InventoryListRpOpcode || !reader.TryReadUInt16(out resultCode) || !reader.TryReadUInt64(out _) ||
            !reader.TryReadUInt64List(out List<ulong> instanceIds) || !reader.TryReadUInt32List(out List<uint> dataIds) ||
            !reader.TryReadUInt32List(out List<uint> quantities) || !reader.TryReadByteList(out List<byte> equipped) ||
            !reader.TryReadByteList(out List<byte> tradable) || !reader.TryReadStringList(out List<string> itemData) ||
            !reader.TryReadUInt64List(out List<ulong> versions) || !reader.IsAtEnd)
        {
            return false;
        }
        int count = instanceIds.Count;
        if (!AllCountsEqual(count, dataIds.Count, quantities.Count, equipped.Count, tradable.Count, itemData.Count, versions.Count))
        {
            return false;
        }
        List<InventoryItem> parsed = new(count);
        for (int i = 0; i < count; ++i)
        {
            parsed.Add(new(instanceIds[i], dataIds[i], quantities[i], equipped[i] != 0, tradable[i] != 0, itemData[i], versions[i]));
        }
        items = parsed;
        return true;
    }

    public static bool TryReadMailList(DecodedPacket packet, out ushort resultCode, out IReadOnlyList<MailSummary> mails)
    {
        resultCode = ushort.MaxValue;
        mails = [];
        var reader = new PacketBinaryReader(packet.Body);
        if (packet.Opcode != MailListRpOpcode || !reader.TryReadUInt16(out resultCode) || !reader.TryReadUInt64(out _) ||
            !reader.TryReadUInt64List(out List<ulong> ids) || !reader.TryReadByteList(out List<byte> types) ||
            !reader.TryReadStringList(out List<string> subjects) || !reader.TryReadByteList(out List<byte> states) ||
            !reader.TryReadUInt64List(out List<ulong> expires) || !reader.TryReadUInt64List(out List<ulong> created) || !reader.IsAtEnd)
        {
            return false;
        }
        int count = ids.Count;
        if (!AllCountsEqual(count, types.Count, subjects.Count, states.Count, expires.Count, created.Count))
        {
            return false;
        }
        List<MailSummary> parsed = new(count);
        for (int i = 0; i < count; ++i)
        {
            parsed.Add(new(ids[i], types[i], subjects[i], states[i], expires[i], created[i]));
        }
        mails = parsed;
        return true;
    }

    public static bool TryReadMailDetail(DecodedPacket packet, out ushort resultCode, out MailDetail? detail)
    {
        resultCode = ushort.MaxValue;
        detail = null;
        var reader = new PacketBinaryReader(packet.Body);
        if (packet.Opcode != MailDetailRpOpcode || !reader.TryReadUInt16(out resultCode) || !reader.TryReadUInt64(out _) ||
            !reader.TryReadUInt64(out ulong mailId) || !reader.TryReadByte(out byte mailType) ||
            !reader.TryReadString(out string subject) || !reader.TryReadString(out string body) ||
            !reader.TryReadByte(out byte state) || !reader.TryReadUInt64(out ulong expires) ||
            !reader.TryReadUInt64List(out List<ulong> attachmentIds) || !reader.TryReadByteList(out List<byte> attachmentTypes) ||
            !reader.TryReadUInt64List(out List<ulong> itemInstanceIds) || !reader.TryReadUInt32List(out List<uint> itemDataIds) ||
            !reader.TryReadUInt32List(out List<uint> quantities) || !reader.TryReadStringList(out List<string> itemData) ||
            !reader.TryReadUInt16List(out List<ushort> currencyIds) || !reader.TryReadUInt64List(out List<ulong> currencyAmounts) ||
            !reader.TryReadByteList(out List<byte> attachmentStates) || !reader.IsAtEnd)
        {
            return false;
        }
        int count = attachmentIds.Count;
        if (!AllCountsEqual(count, attachmentTypes.Count, itemInstanceIds.Count, itemDataIds.Count, quantities.Count,
                itemData.Count, currencyIds.Count, currencyAmounts.Count, attachmentStates.Count))
        {
            return false;
        }
        List<MailAttachment> attachments = new(count);
        for (int i = 0; i < count; ++i)
        {
            attachments.Add(new(attachmentIds[i], attachmentTypes[i], itemInstanceIds[i], itemDataIds[i], quantities[i],
                itemData[i], currencyIds[i], currencyAmounts[i], attachmentStates[i]));
        }
        detail = new(mailId, mailType, subject, body, state, expires, attachments);
        return true;
    }

    public static bool TryReadBidResult(DecodedPacket packet, out BidResult result)
    {
        result = new(ushort.MaxValue, 0, 0, 0, 0, 0);
        var reader = new PacketBinaryReader(packet.Body);
        if (packet.Opcode != BidRpOpcode || !reader.TryReadUInt16(out ushort code) || !reader.TryReadUInt64(out _) ||
            !reader.TryReadUInt64(out ulong listingId) || !reader.TryReadUInt64(out ulong bidId) ||
            !reader.TryReadUInt64(out ulong amount) || !reader.TryReadUInt64(out _) ||
            !reader.TryReadUInt64(out ulong balance) || !reader.TryReadUInt64(out ulong version) || !reader.IsAtEnd)
        {
            return false;
        }
        result = new(code, listingId, bidId, amount, balance, version);
        return true;
    }

    public static bool TryReadBuyoutResult(DecodedPacket packet, out BuyoutResult result)
    {
        result = new(ushort.MaxValue, 0, 0, 0, 0, 0);
        var reader = new PacketBinaryReader(packet.Body);
        if (packet.Opcode != BuyoutRpOpcode || !reader.TryReadUInt16(out ushort code) || !reader.TryReadUInt64(out _) ||
            !reader.TryReadUInt64(out ulong listingId) || !reader.TryReadUInt64(out ulong price) ||
            !reader.TryReadUInt64(out _) || !reader.TryReadUInt64(out ulong balance) ||
            !reader.TryReadUInt64(out ulong itemMailId) || !reader.TryReadUInt64(out _) ||
            !reader.TryReadUInt64(out ulong version) || !reader.IsAtEnd)
        {
            return false;
        }
        result = new(code, listingId, price, balance, itemMailId, version);
        return true;
    }

    public static bool TryReadMailClaimResult(DecodedPacket packet, out MailClaimResult result)
    {
        result = new(ushort.MaxValue, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
        var reader = new PacketBinaryReader(packet.Body);
        if (packet.Opcode != MailClaimRpOpcode || !reader.TryReadUInt16(out ushort code) || !reader.TryReadUInt64(out _) ||
            !reader.TryReadUInt64(out ulong mailId) || !reader.TryReadUInt64(out ulong attachmentId) ||
            !reader.TryReadByte(out byte attachmentType) || !reader.TryReadUInt64(out ulong itemInstanceId) ||
            !reader.TryReadUInt32(out uint itemDataId) || !reader.TryReadUInt32(out uint quantity) ||
            !reader.TryReadUInt16(out ushort currencyId) || !reader.TryReadUInt64(out ulong currencyAmount) ||
            !reader.TryReadUInt64(out ulong balance) || !reader.TryReadByte(out byte mailState) || !reader.IsAtEnd)
        {
            return false;
        }
        result = new(code, mailId, attachmentId, attachmentType, itemInstanceId, itemDataId, quantity, currencyId,
            currencyAmount, balance, mailState);
        return true;
    }

    public static bool TryReadListingRegisterResult(DecodedPacket packet, out ListingRegisterResult result)
    {
        result = new(ushort.MaxValue, 0);
        var reader = new PacketBinaryReader(packet.Body);
        if (packet.Opcode != ListingRegisterRpOpcode || !reader.TryReadUInt16(out ushort code) ||
            !reader.TryReadUInt64(out _) || !reader.TryReadUInt64(out ulong listingId) || !reader.IsAtEnd)
        {
            return false;
        }
        result = new(code, listingId);
        return true;
    }

    public static bool TryReadListingCancelResult(DecodedPacket packet, out ListingCancelResult result)
    {
        result = new(ushort.MaxValue, 0, 0, 0);
        var reader = new PacketBinaryReader(packet.Body);
        if (packet.Opcode != ListingCancelRpOpcode || !reader.TryReadUInt16(out ushort code) ||
            !reader.TryReadUInt64(out _) || !reader.TryReadUInt64(out ulong listingId) ||
            !reader.TryReadUInt64(out ulong returnMailId) || !reader.TryReadUInt64(out ulong listingVersion) ||
            !reader.IsAtEnd)
        {
            return false;
        }
        result = new(code, listingId, returnMailId, listingVersion);
        return true;
    }

    public static bool TryReadBidRefundResult(DecodedPacket packet, out BidRefundResult result)
    {
        result = new(ushort.MaxValue, 0, 0, 0, 0, 0);
        var reader = new PacketBinaryReader(packet.Body);
        if (packet.Opcode != BidRefundRpOpcode || !reader.TryReadUInt16(out ushort code) || !reader.TryReadUInt64(out _) ||
            !reader.TryReadUInt64(out ulong bidId) || !reader.TryReadUInt64(out ulong refundedAmount) ||
            !reader.TryReadUInt64(out ulong balance) || !reader.TryReadByte(out byte bidState) ||
            !reader.TryReadUInt64(out ulong bidVersion) || !reader.IsAtEnd)
        {
            return false;
        }
        result = new(code, bidId, refundedAmount, balance, bidState, bidVersion);
        return true;
    }

    public static bool TryReadDebugCheatResult(DecodedPacket packet, out DebugCheatResult result)
    {
        result = new(ushort.MaxValue, 0, 0, 0, string.Empty);
        var reader = new PacketBinaryReader(packet.Body);
        if (packet.Opcode != DebugCheatRpOpcode || !reader.TryReadUInt16(out ushort code) || !reader.TryReadUInt64(out _) ||
            !reader.TryReadByte(out byte cheatType) || !reader.TryReadUInt64(out ulong balance) ||
            !reader.TryReadUInt64(out ulong itemInstanceId) || !reader.TryReadString(out string message) || !reader.IsAtEnd)
        {
            return false;
        }
        result = new(code, cheatType, balance, itemInstanceId, message);
        return true;
    }

    public static bool TryReadOutbidNotification(DecodedPacket packet, out AuctionOutbidNotification notification)
    {
        notification = new(0, 0, 0, 0);
        var reader = new PacketBinaryReader(packet.Body);
        if (packet.Opcode != AuctionOutbidNotiOpcode || !reader.TryReadUInt64(out ulong listingId) ||
            !reader.TryReadUInt64(out ulong bidId) || !reader.TryReadUInt64(out ulong heldAmount) ||
            !reader.TryReadUInt64(out ulong newAmount) || !reader.IsAtEnd)
        {
            return false;
        }
        notification = new(listingId, bidId, heldAmount, newAmount);
        return true;
    }

    public static bool TryReadWonNotification(DecodedPacket packet, out AuctionWonNotification notification)
    {
        notification = new(0, 0, 0, 0);
        var reader = new PacketBinaryReader(packet.Body);
        if (packet.Opcode != AuctionWonNotiOpcode || !reader.TryReadUInt64(out ulong listingId) ||
            !reader.TryReadUInt64(out ulong bidId) || !reader.TryReadUInt64(out ulong price) ||
            !reader.TryReadUInt64(out ulong mailId) || !reader.IsAtEnd)
        {
            return false;
        }
        notification = new(listingId, bidId, price, mailId);
        return true;
    }

    private static byte[] BuildPacket(ushort opcode, Action<PacketBinaryWriter> writeBody, byte packetKey)
    {
        PacketBinaryWriter writer = new();
        writer.WriteUInt16(opcode);
        writeBody(writer);
        byte[] payload = writer.ToArray();
        byte randomKey = (byte)Random.Shared.Next(0, 256);
        EncodeInPlace(payload, randomKey, packetKey);
        byte[] packet = new byte[TransportHeaderSize + payload.Length];
        BinaryPrimitives.WriteUInt16LittleEndian(packet.AsSpan(0, sizeof(ushort)), (ushort)payload.Length);
        packet[2] = randomKey;
        packet[3] = CalculateChecksum(payload);
        payload.CopyTo(packet, TransportHeaderSize);
        return packet;
    }

    private static void EncodeInPlace(byte[] buffer, byte randomKey, byte packetKey)
    {
        byte plainState = 0;
        byte encodedState = 0;
        for (int i = 0; i < buffer.Length; ++i)
        {
            byte plain = buffer[i];
            plainState = (byte)(plain ^ (byte)(plainState + (byte)(randomKey + 1) + i));
            encodedState = (byte)(plainState ^ (byte)(encodedState + (byte)(packetKey + 1) + i));
            buffer[i] = encodedState;
        }
    }

    private static void DecodeInPlace(byte[] buffer, byte randomKey, byte packetKey)
    {
        byte previousPlainState = 0;
        byte previousEncodedState = 0;
        for (int i = 0; i < buffer.Length; ++i)
        {
            byte encoded = buffer[i];
            byte plainState = (byte)(encoded ^ (byte)(previousEncodedState + (byte)(packetKey + 1) + i));
            buffer[i] = (byte)(plainState ^ (byte)(previousPlainState + (byte)(randomKey + 1) + i));
            previousEncodedState = encoded;
            previousPlainState = plainState;
        }
    }

    private static byte CalculateChecksum(byte[] buffer)
    {
        uint sum = 0;
        foreach (byte value in buffer)
        {
            sum += value;
        }
        return (byte)sum;
    }

    private static bool AllCountsEqual(int expected, params int[] counts) => counts.All(count => count == expected);
}
