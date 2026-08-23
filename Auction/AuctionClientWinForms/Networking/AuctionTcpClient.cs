using System.Collections.Concurrent;
using System.Net.Sockets;
using AuctionClientWinForms.Models;

namespace AuctionClientWinForms.Networking;

internal sealed class AuctionTcpClient : IAsyncDisposable
{
    private readonly SemaphoreSlim m_lifecycleLock = new(1, 1);
    private readonly SemaphoreSlim m_sendLock = new(1, 1);
    private readonly List<byte> m_receiveBuffer = [];
    private readonly ConcurrentDictionary<ulong, TaskCompletionSource<DecodedPacket>> m_pendingRequests = new();

    private TcpClient? m_tcpClient;
    private NetworkStream? m_stream;
    private CancellationTokenSource? m_receiveCancellation;
    private Task? m_receiveTask;
    private byte m_packetKey;
    private long m_nextRequestId;
    private bool m_disposed;

    public event Action<string>? SystemMessageReceived;
    public event Action<bool>? ConnectionStateChanged;
    public event Action<AuctionOutbidNotification>? OutbidReceived;
    public event Action<AuctionWonNotification>? AuctionWonReceived;

    public bool IsConnected => m_stream is not null && m_tcpClient is not null;

    public async Task ConnectAsync(AuctionConnectionSettings settings, CancellationToken cancellationToken = default)
    {
        ThrowIfDisposed();
        await m_lifecycleLock.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            DisconnectCore("Reconnecting.", notify: false);
            TcpClient tcpClient = new() { NoDelay = true };
            try
            {
                await tcpClient.ConnectAsync(settings.Host, settings.Port, cancellationToken).ConfigureAwait(false);
            }
            catch
            {
                tcpClient.Dispose();
                throw;
            }

            m_packetKey = settings.PacketKey;
            m_tcpClient = tcpClient;
            m_stream = tcpClient.GetStream();
            m_receiveBuffer.Clear();
            m_receiveCancellation = new CancellationTokenSource();
            m_receiveTask = Task.Run(() => ReceiveLoopAsync(m_stream, m_receiveCancellation.Token), CancellationToken.None);
        }
        finally
        {
            m_lifecycleLock.Release();
        }

        SystemMessageReceived?.Invoke($"Connected to {settings.Host}:{settings.Port}.");
        ConnectionStateChanged?.Invoke(true);
    }

    public async Task DisconnectAsync(string reason = "Disconnected.")
    {
        await m_lifecycleLock.WaitAsync().ConfigureAwait(false);
        try
        {
            DisconnectCore(reason, notify: true);
        }
        finally
        {
            m_lifecycleLock.Release();
        }
    }

    public async Task<AuctionAuthResult> AuthenticateAsync(string ticket, CancellationToken cancellationToken = default)
    {
        DecodedPacket packet = await RequestAsync(
            requestId => AuctionPacketCodec.CreateAuthRequest(requestId, ticket, m_packetKey),
            AuctionPacketCodec.AuctionAuthRpOpcode,
            cancellationToken).ConfigureAwait(false);
        if (!AuctionPacketCodec.TryReadAuth(packet, out AuctionAuthResult result))
        {
            throw new InvalidDataException("AuctionAuthRp deserialize failed.");
        }
        return result;
    }

    public async Task<(ushort ResultCode, IReadOnlyList<ListingSummary> Listings)> SearchListingsAsync(
        byte category,
        IReadOnlyList<uint> itemDataIds,
        uint minStr,
        uint minDex,
        uint minInt,
        uint minLuk,
        bool sellerOnly = false,
		byte sortType = 1,
		ulong cursorSortValue = 0,
        ulong cursorListingId = 0,
        uint limit = 100,
        CancellationToken cancellationToken = default)
    {
        DecodedPacket packet = await RequestAsync(
            requestId => AuctionPacketCodec.CreateListingSearchRequest(
				requestId, category, itemDataIds, minStr, minDex, minInt, minLuk, sellerOnly,
				sortType, cursorSortValue, cursorListingId, limit, m_packetKey),
            AuctionPacketCodec.ListingSearchRpOpcode,
            cancellationToken).ConfigureAwait(false);
        if (!AuctionPacketCodec.TryReadListingSearch(packet, out ushort code, out IReadOnlyList<ListingSummary> listings))
        {
            throw new InvalidDataException("ListingSearchRp deserialize failed.");
        }
        return (code, listings);
    }

    public async Task<(ushort ResultCode, IReadOnlyList<SaleHistorySummary> History)> SearchSaleHistoryAsync(
        byte category,
        IReadOnlyList<uint> itemDataIds,
        uint minStr,
        uint minDex,
        uint minInt,
        uint minLuk,
		byte sortType = 1,
		ulong cursorSortValue = 0,
        ulong cursorListingId = 0,
        uint limit = 100,
        CancellationToken cancellationToken = default)
    {
        DecodedPacket packet = await RequestAsync(
            requestId => AuctionPacketCodec.CreateSaleHistorySearchRequest(
                requestId, category, itemDataIds, minStr, minDex, minInt, minLuk,
				sortType, cursorSortValue, cursorListingId, limit, m_packetKey),
            AuctionPacketCodec.SaleHistorySearchRpOpcode,
            cancellationToken).ConfigureAwait(false);
        if (!AuctionPacketCodec.TryReadSaleHistorySearch(packet, out ushort code, out IReadOnlyList<SaleHistorySummary> history))
        {
            throw new InvalidDataException("SaleHistorySearchRp deserialize failed.");
        }
        return (code, history);
    }

    public async Task<(ushort ResultCode, SaleHistoryDetail? Detail)> GetSaleHistoryDetailAsync(
        ulong listingId,
        CancellationToken cancellationToken = default)
    {
        DecodedPacket packet = await RequestAsync(
            requestId => AuctionPacketCodec.CreateSaleHistoryDetailRequest(requestId, listingId, m_packetKey),
            AuctionPacketCodec.SaleHistoryDetailRpOpcode,
            cancellationToken).ConfigureAwait(false);
        if (!AuctionPacketCodec.TryReadSaleHistoryDetail(packet, out ushort code, out SaleHistoryDetail? detail))
        {
            throw new InvalidDataException("SaleHistoryDetailRp deserialize failed.");
        }
        return (code, detail);
    }

    public async Task<DebugCheatResult> ExecuteDebugCheatAsync(
        byte cheatType,
        ulong amount,
        uint itemDataId,
        uint strength,
        uint dexterity,
        uint intelligence,
        uint luck,
        CancellationToken cancellationToken = default)
    {
        DecodedPacket packet = await RequestAsync(
            requestId => AuctionPacketCodec.CreateDebugCheatRequest(
                requestId, cheatType, amount, itemDataId, strength, dexterity, intelligence, luck, m_packetKey),
            AuctionPacketCodec.DebugCheatRpOpcode,
            cancellationToken).ConfigureAwait(false);
        if (!AuctionPacketCodec.TryReadDebugCheatResult(packet, out DebugCheatResult result))
        {
            throw new InvalidDataException("DebugCheatRp deserialize failed.");
        }
        return result;
    }

    public async Task<(ushort ResultCode, ListingDetail? Detail)> GetListingDetailAsync(
        ulong listingId,
        CancellationToken cancellationToken = default)
    {
        DecodedPacket packet = await RequestAsync(
            requestId => AuctionPacketCodec.CreateListingDetailRequest(requestId, listingId, m_packetKey),
            AuctionPacketCodec.ListingDetailRpOpcode,
            cancellationToken).ConfigureAwait(false);
        if (!AuctionPacketCodec.TryReadListingDetail(packet, out ushort code, out ListingDetail? detail))
        {
            throw new InvalidDataException("ListingDetailRp deserialize failed.");
        }
        return (code, detail);
    }

    public async Task<BidResult> BidAsync(ulong listingId, ulong amount, ulong version, CancellationToken cancellationToken = default)
    {
        DecodedPacket packet = await RequestAsync(
            requestId => AuctionPacketCodec.CreateBidRequest(requestId, listingId, amount, version, m_packetKey),
            AuctionPacketCodec.BidRpOpcode,
            cancellationToken).ConfigureAwait(false);
        if (!AuctionPacketCodec.TryReadBidResult(packet, out BidResult result))
        {
            throw new InvalidDataException("BidRp deserialize failed.");
        }
        return result;
    }

    public async Task<BuyoutResult> BuyoutAsync(ulong listingId, ulong version, CancellationToken cancellationToken = default)
    {
        DecodedPacket packet = await RequestAsync(
            requestId => AuctionPacketCodec.CreateBuyoutRequest(requestId, listingId, version, m_packetKey),
            AuctionPacketCodec.BuyoutRpOpcode,
            cancellationToken).ConfigureAwait(false);
        if (!AuctionPacketCodec.TryReadBuyoutResult(packet, out BuyoutResult result))
        {
            throw new InvalidDataException("BuyoutRp deserialize failed.");
        }
        return result;
    }

    public async Task<(ushort ResultCode, IReadOnlyList<BidSummary> Bids)> GetMyBidsAsync(CancellationToken cancellationToken = default)
    {
        DecodedPacket packet = await RequestAsync(
            requestId => AuctionPacketCodec.CreateMyBidListRequest(requestId, m_packetKey),
            AuctionPacketCodec.MyBidListRpOpcode,
            cancellationToken).ConfigureAwait(false);
        if (!AuctionPacketCodec.TryReadMyBidList(packet, out ushort code, out IReadOnlyList<BidSummary> bids))
        {
            throw new InvalidDataException("MyBidListRp deserialize failed.");
        }
        return (code, bids);
    }

    public async Task<(ushort ResultCode, IReadOnlyList<InventoryItem> Items)> GetInventoryAsync(CancellationToken cancellationToken = default)
    {
        DecodedPacket packet = await RequestAsync(
            requestId => AuctionPacketCodec.CreateInventoryListRequest(requestId, m_packetKey),
            AuctionPacketCodec.InventoryListRpOpcode,
            cancellationToken).ConfigureAwait(false);
        if (!AuctionPacketCodec.TryReadInventory(packet, out ushort code, out IReadOnlyList<InventoryItem> items))
        {
            throw new InvalidDataException("InventoryListRp deserialize failed.");
        }
        return (code, items);
    }

    public async Task<BidRefundResult> RefundBidAsync(BidSummary bid, CancellationToken cancellationToken = default)
    {
        DecodedPacket packet = await RequestAsync(
            requestId => AuctionPacketCodec.CreateBidRefundRequest(
                requestId, bid.ListingId, bid.BidId, bid.BidVersion, m_packetKey),
            AuctionPacketCodec.BidRefundRpOpcode,
            cancellationToken).ConfigureAwait(false);
        if (!AuctionPacketCodec.TryReadBidRefundResult(packet, out BidRefundResult result))
        {
            throw new InvalidDataException("BidRefundRp deserialize failed.");
        }
        return result;
    }

    public async Task<ListingRegisterResult> RegisterListingAsync(
        InventoryItem item,
        ushort currencyId,
        ulong startPrice,
        ulong buyoutPrice,
        uint durationSeconds,
        CancellationToken cancellationToken = default)
    {
        DecodedPacket packet = await RequestAsync(
            requestId => AuctionPacketCodec.CreateListingRegisterRequest(
                requestId, item, currencyId, startPrice, buyoutPrice, durationSeconds, m_packetKey),
            AuctionPacketCodec.ListingRegisterRpOpcode,
            cancellationToken).ConfigureAwait(false);
        if (!AuctionPacketCodec.TryReadListingRegisterResult(packet, out ListingRegisterResult result))
        {
            throw new InvalidDataException("ListingRegisterRp deserialize failed.");
        }
        return result;
    }

    public async Task<ListingCancelResult> CancelListingAsync(
        ulong listingId,
        ulong expectedListingVersion,
        CancellationToken cancellationToken = default)
    {
        DecodedPacket packet = await RequestAsync(
            requestId => AuctionPacketCodec.CreateListingCancelRequest(
                requestId, listingId, expectedListingVersion, m_packetKey),
            AuctionPacketCodec.ListingCancelRpOpcode,
            cancellationToken).ConfigureAwait(false);
        if (!AuctionPacketCodec.TryReadListingCancelResult(packet, out ListingCancelResult result))
        {
            throw new InvalidDataException("ListingCancelRp deserialize failed.");
        }
        return result;
    }

    public async Task<(ushort ResultCode, IReadOnlyList<MailSummary> Mails)> GetMailsAsync(CancellationToken cancellationToken = default)
    {
        DecodedPacket packet = await RequestAsync(
            requestId => AuctionPacketCodec.CreateMailListRequest(requestId, m_packetKey),
            AuctionPacketCodec.MailListRpOpcode,
            cancellationToken).ConfigureAwait(false);
        if (!AuctionPacketCodec.TryReadMailList(packet, out ushort code, out IReadOnlyList<MailSummary> mails))
        {
            throw new InvalidDataException("MailListRp deserialize failed.");
        }
        return (code, mails);
    }

    public async Task<(ushort ResultCode, MailDetail? Detail)> GetMailDetailAsync(
        ulong mailId,
        CancellationToken cancellationToken = default)
    {
        DecodedPacket packet = await RequestAsync(
            requestId => AuctionPacketCodec.CreateMailDetailRequest(requestId, mailId, m_packetKey),
            AuctionPacketCodec.MailDetailRpOpcode,
            cancellationToken).ConfigureAwait(false);
        if (!AuctionPacketCodec.TryReadMailDetail(packet, out ushort code, out MailDetail? detail))
        {
            throw new InvalidDataException("MailDetailRp deserialize failed.");
        }
        return (code, detail);
    }

    public async Task<MailClaimResult> ClaimMailAsync(
        ulong mailId,
        ulong attachmentId,
        CancellationToken cancellationToken = default)
    {
        DecodedPacket packet = await RequestAsync(
            requestId => AuctionPacketCodec.CreateMailClaimRequest(requestId, mailId, attachmentId, m_packetKey),
            AuctionPacketCodec.MailClaimRpOpcode,
            cancellationToken).ConfigureAwait(false);
        if (!AuctionPacketCodec.TryReadMailClaimResult(packet, out MailClaimResult result))
        {
            throw new InvalidDataException("MailClaimRp deserialize failed.");
        }
        return result;
    }

    public async ValueTask DisposeAsync()
    {
        if (m_disposed)
        {
            return;
        }
        await DisconnectAsync("Client closed.").ConfigureAwait(false);
        m_disposed = true;
        m_lifecycleLock.Dispose();
        m_sendLock.Dispose();
    }

    private async Task<DecodedPacket> RequestAsync(
        Func<ulong, byte[]> packetFactory,
        ushort responseOpcode,
        CancellationToken cancellationToken)
    {
        ulong requestId = unchecked((ulong)Interlocked.Increment(ref m_nextRequestId));
        TaskCompletionSource<DecodedPacket> completion = new(TaskCreationOptions.RunContinuationsAsynchronously);
        if (!m_pendingRequests.TryAdd(requestId, completion))
        {
            throw new InvalidOperationException("Duplicate request id.");
        }

        try
        {
            await SendPacketAsync(packetFactory(requestId), cancellationToken).ConfigureAwait(false);
            DecodedPacket response = await completion.Task.WaitAsync(TimeSpan.FromSeconds(10), cancellationToken).ConfigureAwait(false);
            if (response.Opcode != responseOpcode)
            {
                throw new InvalidDataException($"Unexpected response opcode. expected={responseOpcode}, actual={response.Opcode}");
            }
            return response;
        }
        finally
        {
            m_pendingRequests.TryRemove(requestId, out _);
        }
    }

    private async Task SendPacketAsync(byte[] packet, CancellationToken cancellationToken)
    {
        await m_sendLock.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            NetworkStream stream = m_stream ?? throw new InvalidOperationException("AuctionServer에 연결되지 않았습니다.");
            await stream.WriteAsync(packet, cancellationToken).ConfigureAwait(false);
        }
        finally
        {
            m_sendLock.Release();
        }
    }

    private async Task ReceiveLoopAsync(NetworkStream stream, CancellationToken cancellationToken)
    {
        string reason = "Connection closed by server.";
        try
        {
            byte[] buffer = new byte[8192];
            while (!cancellationToken.IsCancellationRequested)
            {
                int count = await stream.ReadAsync(buffer, cancellationToken).ConfigureAwait(false);
                if (count == 0)
                {
                    break;
                }
                for (int i = 0; i < count; ++i)
                {
                    m_receiveBuffer.Add(buffer[i]);
                }

                while (true)
                {
                    if (!AuctionPacketCodec.TryExtractPacket(m_receiveBuffer, m_packetKey, out DecodedPacket? packet, out string? error))
                    {
                        if (!string.IsNullOrEmpty(error))
                        {
                            throw new InvalidDataException(error);
                        }
                        break;
                    }
                    if (packet is not null)
                    {
                        DispatchPacket(packet);
                    }
                }
            }
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
            reason = "Disconnected.";
        }
        catch (Exception exception)
        {
            reason = $"Connection lost: {exception.Message}";
            SystemMessageReceived?.Invoke(reason);
        }
        finally
        {
            await m_lifecycleLock.WaitAsync().ConfigureAwait(false);
            try
            {
                if (ReferenceEquals(m_stream, stream))
                {
                    DisconnectCore(reason, notify: true);
                }
            }
            finally
            {
                m_lifecycleLock.Release();
            }
        }
    }

    private void DispatchPacket(DecodedPacket packet)
    {
        if (AuctionPacketCodec.TryReadOutbidNotification(packet, out AuctionOutbidNotification outbid))
        {
            OutbidReceived?.Invoke(outbid);
            return;
        }
        if (AuctionPacketCodec.TryReadWonNotification(packet, out AuctionWonNotification won))
        {
            AuctionWonReceived?.Invoke(won);
            return;
        }
        if (!AuctionPacketCodec.TryGetResponseRequestId(packet, out ulong requestId) ||
            !m_pendingRequests.TryGetValue(requestId, out TaskCompletionSource<DecodedPacket>? completion))
        {
            SystemMessageReceived?.Invoke($"Unhandled packet opcode={packet.Opcode}");
            return;
        }
        completion.TrySetResult(packet);
    }

    private void DisconnectCore(string reason, bool notify)
    {
        m_receiveCancellation?.Cancel();
        m_receiveCancellation?.Dispose();
        m_receiveCancellation = null;
        m_receiveTask = null;
        m_stream?.Dispose();
        m_stream = null;
        m_tcpClient?.Dispose();
        m_tcpClient = null;
        m_receiveBuffer.Clear();

        foreach (TaskCompletionSource<DecodedPacket> completion in m_pendingRequests.Values)
        {
            completion.TrySetException(new IOException(reason));
        }
        m_pendingRequests.Clear();
        if (notify)
        {
            ConnectionStateChanged?.Invoke(false);
        }
    }

    private void ThrowIfDisposed() => ObjectDisposedException.ThrowIf(m_disposed, this);
}
