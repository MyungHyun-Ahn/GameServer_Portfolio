using System.Collections.Concurrent;
using AuctionClientWinForms.Models;
using ClientNetwork.Packet;
using ClientNetwork.Threading;
using ClientNetwork.Transport;
using Generated.Packets;
using Generated.Packets.Auction;

namespace AuctionClientWinForms.Networking;

internal sealed class AuctionTcpClient : IAsyncDisposable
{
    private static readonly TimeSpan s_requestTimeout = TimeSpan.FromSeconds(10);

    private readonly SemaphoreSlim m_lifecycleLock = new(1, 1);
    private readonly object m_connectionSetupLock = new();
    private readonly object m_connectAttemptLock = new();
    private readonly object m_dispatchLock = new();
    private readonly ConcurrentDictionary<ulong, PendingRequest> m_pendingRequests = new();
    private readonly FSerializedCallbackQueue m_callbackQueue = new();
    private readonly FClientSession m_session = new();
    private readonly PacketRouter m_packetRouter = new();
    private CancellationTokenSource? m_dispatchStopSource;
    private Task? m_dispatchTask;
    private TaskCompletionSource? m_connectionSetupCompletion;
    private CancellationTokenSource? m_connectAttemptStopSource;
    private long m_activeGeneration;
    private long m_nextRequestId;
    private int m_disposed;
    private uint m_authenticatedSearchPageSize;
    private uint m_authenticatedInventoryListPageSize;
    private uint m_authenticatedMailListPageSize;

    public AuctionTcpClient()
    {
        m_packetRouter.SetAuctionHandler(new AuctionPacketHandler(this));
        m_session.Disconnected += OnSessionDisconnected;
    }

    public event Action<string>? SystemMessageReceived;
    public event Action<bool>? ConnectionStateChanged;
    public event Action<AuctionOutbidNotification>? OutbidReceived;
    public event Action<AuctionWonNotification>? AuctionWonReceived;

    public bool IsConnected => m_session.IsConnected;

    public async Task ConnectAsync(AuctionConnectionSettings settings, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(settings);
        ThrowIfDisposed();
        CancelConnectAttempt();
        await m_lifecycleLock.WaitAsync(cancellationToken).ConfigureAwait(false);
        TaskCompletionSource? connectionSetupCompletion = null;
        CancellationTokenSource? connectAttemptStopSource = null;
        try
        {
            ThrowIfDisposed();
            CancelConnectAttempt();
            bool wasConnected = Volatile.Read(ref m_activeGeneration) != 0;
            await DisconnectCoreAsync("Reconnecting.", notify: wasConnected).ConfigureAwait(false);
            connectionSetupCompletion = BeginConnectionSetup();
            connectAttemptStopSource = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
            RegisterConnectAttempt(connectAttemptStopSource);
        }
        finally
        {
            m_lifecycleLock.Release();
        }

        try
        {
            long generation = await m_session.ConnectAsync(new FClientConnectionOptions(settings.Host, settings.Port)
            {
                PacketKey = settings.PacketKey
            }, connectAttemptStopSource.Token).ConfigureAwait(false);

            await m_lifecycleLock.WaitAsync().ConfigureAwait(false);
            try
            {
                ThrowIfDisposed();
                bool isCurrentAttempt = IsCurrentConnectAttempt(connectAttemptStopSource);
                if (!isCurrentAttempt || connectAttemptStopSource.IsCancellationRequested)
                {
                    if (m_session.ConnectionGeneration == generation &&
                        m_session.State != EClientConnectionState.Disconnected)
                    {
                        await m_session.DisconnectAsync().ConfigureAwait(false);
                    }
                    throw new OperationCanceledException(connectAttemptStopSource.Token);
                }

                var dispatchStopSource = new CancellationTokenSource();
                Volatile.Write(ref m_activeGeneration, generation);
                StartDispatchLoop(generation, dispatchStopSource);

                EmitSystemMessage($"Connected to {settings.Host}:{settings.Port}.");
                EmitConnectionState(true);
            }
            finally
            {
                m_lifecycleLock.Release();
            }
        }
        finally
        {
            CompleteConnectionSetup(connectionSetupCompletion);
            CompleteConnectAttempt(connectAttemptStopSource);
            connectAttemptStopSource.Dispose();
        }
    }

    public async Task DisconnectAsync(string reason = "Disconnected.")
    {
        ThrowIfDisposed();
        CancelConnectAttempt();
        await m_lifecycleLock.WaitAsync().ConfigureAwait(false);
        try
        {
            ThrowIfDisposed();
            CancelConnectAttempt();
            await DisconnectCoreAsync(reason, notify: true).ConfigureAwait(false);
        }
        finally
        {
            m_lifecycleLock.Release();
        }
    }

    public async Task<AuctionAuthResult> AuthenticateAsync(
        string ticket,
        CancellationToken cancellationToken = default)
    {
        ResetAuthenticatedPolicies();
        AuctionAuthRp response = await RequestAsync<AuctionAuthRp>(requestId => new AuctionAuthRq
        {
            RequestId = requestId,
            Ticket = ticket
        }, cancellationToken).ConfigureAwait(false);

        var result = new AuctionAuthResult(
            response.ResultCode,
            response.RequestId,
            response.UserId,
            response.MaxActiveListings,
            response.SearchPageSize,
            response.InventoryListPageSize,
            response.MailListPageSize,
            response.MinimumListingDurationSeconds,
            response.MaximumListingDurationSeconds,
            response.DefaultListingDurationSeconds,
            response.DefaultCurrencyId,
            response.MinimumBidIncrement,
            response.MinimumListingPrice,
            response.MaximumListingPrice);

        if (response.ResultCode == 0)
        {
            m_authenticatedSearchPageSize = RequirePositivePageSize(response.SearchPageSize, nameof(response.SearchPageSize));
            m_authenticatedInventoryListPageSize =
                RequirePositivePageSize(response.InventoryListPageSize, nameof(response.InventoryListPageSize));
            m_authenticatedMailListPageSize = RequirePositivePageSize(response.MailListPageSize, nameof(response.MailListPageSize));
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
        ListingSearchRp response = await RequestAsync<ListingSearchRp>(requestId => new ListingSearchRq
        {
            RequestId = requestId,
            ItemCategory = category,
            ItemDataIds = [.. itemDataIds],
            MinStr = minStr,
            MinDex = minDex,
            MinInt = minInt,
            MinLuk = minLuk,
            SellerOnly = sellerOnly ? (byte)1 : (byte)0,
            SortType = sortType,
            CursorSortValue = cursorSortValue,
            CursorListingId = cursorListingId,
            Limit = limit
        }, cancellationToken).ConfigureAwait(false);
        return (response.ResultCode, ConvertListings(response));
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
        SaleHistorySearchRp response = await RequestAsync<SaleHistorySearchRp>(requestId => new SaleHistorySearchRq
        {
            RequestId = requestId,
            ItemCategory = category,
            ItemDataIds = [.. itemDataIds],
            MinStr = minStr,
            MinDex = minDex,
            MinInt = minInt,
            MinLuk = minLuk,
            SortType = sortType,
            CursorSortValue = cursorSortValue,
            CursorListingId = cursorListingId,
            Limit = limit
        }, cancellationToken).ConfigureAwait(false);
        return (response.ResultCode, ConvertSaleHistory(response));
    }

    public async Task<(ushort ResultCode, SaleHistoryDetail? Detail)> GetSaleHistoryDetailAsync(
        ulong listingId,
        CancellationToken cancellationToken = default)
    {
        SaleHistoryDetailRp response = await RequestAsync<SaleHistoryDetailRp>(requestId => new SaleHistoryDetailRq
        {
            RequestId = requestId,
            ListingId = listingId
        }, cancellationToken).ConfigureAwait(false);

        return (response.ResultCode, new SaleHistoryDetail(
            response.ListingId,
            response.SellerLoginId,
            response.ItemDataId,
            response.ItemCategory,
            response.Quantity,
            response.ItemData,
            response.Name,
            response.StrStat,
            response.DexStat,
            response.IntStat,
            response.LukStat,
            response.CurrencyId,
            response.StartPrice,
            response.FinalPrice,
            response.SaleType,
            response.SoldAtUnixMs));
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
        DebugCheatRp response = await RequestAsync<DebugCheatRp>(requestId => new DebugCheatRq
        {
            RequestId = requestId,
            CheatType = cheatType,
            Amount = amount,
            ItemDataId = itemDataId,
            StrStat = strength,
            DexStat = dexterity,
            IntStat = intelligence,
            LukStat = luck
        }, cancellationToken).ConfigureAwait(false);
        return new DebugCheatResult(
            response.ResultCode,
            response.CheatType,
            response.CurrencyBalance,
            response.ItemInstanceId,
            response.Message);
    }

    public async Task<(ushort ResultCode, ListingDetail? Detail)> GetListingDetailAsync(
        ulong listingId,
        CancellationToken cancellationToken = default)
    {
        ListingDetailRp response = await RequestAsync<ListingDetailRp>(requestId => new ListingDetailRq
        {
            RequestId = requestId,
            ListingId = listingId
        }, cancellationToken).ConfigureAwait(false);

        return (response.ResultCode, new ListingDetail(
            response.ListingId,
            response.SellerUserId,
            response.SellerLoginId,
            response.ItemInstanceId,
            response.ItemDataId,
            response.ItemCategory,
            response.Quantity,
            response.ItemData,
            response.Name,
            response.StrStat,
            response.DexStat,
            response.IntStat,
            response.LukStat,
            response.CurrencyId,
            response.StartPrice,
            response.CurrentBidPrice,
            response.BuyoutPrice,
            response.HighestBidderUserId,
            response.ExpiresAtUnixMs,
            response.Version));
    }

    public async Task<BidResult> BidAsync(
        ulong listingId,
        ulong amount,
        ulong version,
        CancellationToken cancellationToken = default)
    {
        BidRp response = await RequestAsync<BidRp>(requestId => new BidRq
        {
            RequestId = requestId,
            ListingId = listingId,
            BidAmount = amount,
            ExpectedListingVersion = version
        }, cancellationToken).ConfigureAwait(false);
        return new BidResult(
            response.ResultCode,
            response.ListingId,
            response.BidId,
            response.BidAmount,
            response.CurrencyBalance,
            response.ListingVersion);
    }

    public async Task<BuyoutResult> BuyoutAsync(
        ulong listingId,
        ulong version,
        CancellationToken cancellationToken = default)
    {
        BuyoutRp response = await RequestAsync<BuyoutRp>(requestId => new BuyoutRq
        {
            RequestId = requestId,
            ListingId = listingId,
            ExpectedListingVersion = version
        }, cancellationToken).ConfigureAwait(false);
        return new BuyoutResult(
            response.ResultCode,
            response.ListingId,
            response.BuyoutPrice,
            response.CurrencyBalance,
            response.ItemMailId,
            response.ListingVersion);
    }

    public async Task<(ushort ResultCode, IReadOnlyList<BidSummary> Bids)> GetMyBidsAsync(
        CancellationToken cancellationToken = default)
    {
        uint pageSize = RequirePositivePageSize(m_authenticatedSearchPageSize, "authenticated auction search page size");
        List<BidSummary> allBids = [];
        ulong cursorBidId = 0;
        while (true)
        {
            MyBidListRp response = await RequestAsync<MyBidListRp>(requestId => new MyBidListRq
            {
                RequestId = requestId,
                CursorBidId = cursorBidId,
                Limit = pageSize
            }, cancellationToken).ConfigureAwait(false);
            IReadOnlyList<BidSummary> bids = ConvertBids(response);
            if (response.ResultCode != 0)
            {
                return (response.ResultCode, allBids);
            }

            allBids.AddRange(bids);
            if (bids.Count < pageSize)
            {
                return (response.ResultCode, allBids);
            }

            ulong nextCursor = bids[^1].BidId;
            if (nextCursor == 0 || nextCursor == cursorBidId)
            {
                throw new InvalidDataException("MyBidListRp returned an invalid cursor sequence.");
            }
            cursorBidId = nextCursor;
        }
    }

    public async Task<(ushort ResultCode, IReadOnlyList<InventoryItem> Items)> GetInventoryAsync(
        CancellationToken cancellationToken = default)
    {
        uint pageSize = RequirePositivePageSize(m_authenticatedInventoryListPageSize, "authenticated inventory list page size");
        List<InventoryItem> allItems = [];
        ulong cursorItemInstanceId = 0;
        while (true)
        {
            InventoryListRp response = await RequestAsync<InventoryListRp>(requestId => new InventoryListRq
            {
                RequestId = requestId,
                CursorItemInstanceId = cursorItemInstanceId,
                Limit = pageSize
            }, cancellationToken).ConfigureAwait(false);
            IReadOnlyList<InventoryItem> items = ConvertInventory(response);
            if (response.ResultCode != 0)
            {
                return (response.ResultCode, allItems);
            }

            allItems.AddRange(items);
            if (items.Count < pageSize)
            {
                return (response.ResultCode, allItems);
            }

            ulong nextCursor = items[^1].ItemInstanceId;
            if (nextCursor == 0 || nextCursor == cursorItemInstanceId)
            {
                throw new InvalidDataException("InventoryListRp returned an invalid cursor sequence.");
            }
            cursorItemInstanceId = nextCursor;
        }
    }

    public async Task<BidRefundResult> RefundBidAsync(
        BidSummary bid,
        CancellationToken cancellationToken = default)
    {
        BidRefundRp response = await RequestAsync<BidRefundRp>(requestId => new BidRefundRq
        {
            RequestId = requestId,
            ListingId = bid.ListingId,
            BidId = bid.BidId,
            ExpectedBidVersion = bid.BidVersion
        }, cancellationToken).ConfigureAwait(false);
        return new BidRefundResult(
            response.ResultCode,
            response.BidId,
            response.RefundedAmount,
            response.CurrencyBalance,
            response.BidState,
            response.BidVersion);
    }

    public async Task<ListingRegisterResult> RegisterListingAsync(
        InventoryItem item,
        ushort currencyId,
        ulong startPrice,
        ulong buyoutPrice,
        uint durationSeconds,
        CancellationToken cancellationToken = default)
    {
        ListingRegisterRp response = await RequestAsync<ListingRegisterRp>(requestId => new ListingRegisterRq
        {
            RequestId = requestId,
            ItemInstanceId = item.ItemInstanceId,
            ExpectedItemVersion = item.Version,
            CurrencyId = currencyId,
            StartPrice = startPrice,
            BuyoutPrice = buyoutPrice,
            DurationSeconds = durationSeconds
        }, cancellationToken).ConfigureAwait(false);
        return new ListingRegisterResult(response.ResultCode, response.ListingId);
    }

    public async Task<ListingCancelResult> CancelListingAsync(
        ulong listingId,
        ulong expectedListingVersion,
        CancellationToken cancellationToken = default)
    {
        ListingCancelRp response = await RequestAsync<ListingCancelRp>(requestId => new ListingCancelRq
        {
            RequestId = requestId,
            ListingId = listingId,
            ExpectedListingVersion = expectedListingVersion
        }, cancellationToken).ConfigureAwait(false);
        return new ListingCancelResult(
            response.ResultCode,
            response.ListingId,
            response.ReturnMailId,
            response.ListingVersion);
    }

    public async Task<(ushort ResultCode, IReadOnlyList<MailSummary> Mails)> GetMailsAsync(
        CancellationToken cancellationToken = default)
    {
        uint pageSize = RequirePositivePageSize(m_authenticatedMailListPageSize, "authenticated mail list page size");
        List<MailSummary> allMails = [];
        ulong cursorMailId = 0;
        while (true)
        {
            MailListRp response = await RequestAsync<MailListRp>(requestId => new MailListRq
            {
                RequestId = requestId,
                CursorMailId = cursorMailId,
                Limit = pageSize
            }, cancellationToken).ConfigureAwait(false);
            IReadOnlyList<MailSummary> mails = ConvertMails(response);
            if (response.ResultCode != 0)
            {
                return (response.ResultCode, allMails);
            }

            allMails.AddRange(mails);
            if (mails.Count < pageSize)
            {
                return (response.ResultCode, allMails);
            }

            ulong nextCursor = mails[^1].MailId;
            if (nextCursor == 0 || nextCursor == cursorMailId)
            {
                throw new InvalidDataException("MailListRp returned an invalid cursor sequence.");
            }
            cursorMailId = nextCursor;
        }
    }

    public async Task<(ushort ResultCode, MailDetail? Detail)> GetMailDetailAsync(
        ulong mailId,
        CancellationToken cancellationToken = default)
    {
        MailDetailRp response = await RequestAsync<MailDetailRp>(requestId => new MailDetailRq
        {
            RequestId = requestId,
            MailId = mailId
        }, cancellationToken).ConfigureAwait(false);
        return (response.ResultCode, new MailDetail(
            response.MailId,
            response.MailType,
            response.Subject,
            response.Body,
            response.State,
            response.ExpiresAtUnixMs,
            ConvertMailAttachments(response)));
    }

    public async Task<MailClaimResult> ClaimMailAsync(
        ulong mailId,
        ulong attachmentId,
        CancellationToken cancellationToken = default)
    {
        MailClaimRp response = await RequestAsync<MailClaimRp>(requestId => new MailClaimRq
        {
            RequestId = requestId,
            MailId = mailId,
            AttachmentId = attachmentId
        }, cancellationToken).ConfigureAwait(false);
        return new MailClaimResult(
            response.ResultCode,
            response.MailId,
            response.AttachmentId,
            response.AttachmentType,
            response.ItemInstanceId,
            response.ItemDataId,
            response.Quantity,
            response.CurrencyId,
            response.CurrencyAmount,
            response.CurrencyBalance,
            response.MailState);
    }

    public async ValueTask DisposeAsync()
    {
        if (Interlocked.Exchange(ref m_disposed, 1) != 0)
        {
            return;
        }

        CancelConnectAttempt();
        try
        {
            await m_lifecycleLock.WaitAsync().ConfigureAwait(false);
            try
            {
                CancelConnectAttempt();
                await DisconnectCoreAsync("Client closed.", notify: false).ConfigureAwait(false);
                m_session.Disconnected -= OnSessionDisconnected;
                await m_session.DisposeAsync().ConfigureAwait(false);
            }
            finally
            {
                m_lifecycleLock.Release();
            }
        }
        finally
        {
            await m_callbackQueue.DisposeAsync().ConfigureAwait(false);
        }
    }

    private async Task<TResponse> RequestAsync<TResponse>(
        Func<ulong, IContentPacket> requestFactory,
        CancellationToken cancellationToken)
        where TResponse : class
    {
        ThrowIfDisposed();
        long generation = Volatile.Read(ref m_activeGeneration);
        if (generation == 0 || !m_session.IsConnected)
        {
            throw new IOException("AuctionServer에 연결되지 않았습니다.");
        }

        ulong requestId = unchecked((ulong)Interlocked.Increment(ref m_nextRequestId));
        var completion = new TaskCompletionSource<object>(TaskCreationOptions.RunContinuationsAsynchronously);
        var pendingRequest = new PendingRequest(typeof(TResponse), completion);
        if (!m_pendingRequests.TryAdd(requestId, pendingRequest))
        {
            throw new InvalidOperationException("Duplicate request id.");
        }

        try
        {
            if (!await m_session.SendAsync(requestFactory(requestId), cancellationToken).ConfigureAwait(false) ||
                Volatile.Read(ref m_activeGeneration) != generation)
            {
                throw new IOException("AuctionServer에 연결되지 않았습니다.");
            }

            object response = await completion.Task.WaitAsync(s_requestTimeout, cancellationToken).ConfigureAwait(false);
            if (response is not TResponse typedResponse)
            {
                throw new InvalidDataException(
                    $"Unexpected response type. expected={typeof(TResponse).Name}, actual={response.GetType().Name}");
            }
            return typedResponse;
        }
        finally
        {
            m_pendingRequests.TryRemove(requestId, out _);
        }
    }

    private async Task DispatchLoopAsync(long generation, CancellationToken cancellationToken)
    {
        try
        {
            while (await m_session.WaitToReadPacketAsync(cancellationToken).ConfigureAwait(false))
            {
                while (m_session.TryDequeuePacket(out FReceivedPacket packet))
                {
                    if (cancellationToken.IsCancellationRequested ||
                        Volatile.Read(ref m_activeGeneration) != generation)
                    {
                        return;
                    }

                    if (packet.ConnectionGeneration != generation)
                    {
                        continue;
                    }

                    if (!m_packetRouter.DispatchPacket(packet.Opcode, packet.Body.Span))
                    {
                        EmitSystemMessage($"Unhandled or invalid packet opcode={packet.Opcode}");
                    }
                }
            }
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
        }
        catch (Exception exception)
        {
            EmitSystemMessage($"Packet dispatch failed: {exception.Message}");
            FailPendingRequests(new InvalidDataException("Packet dispatch failed.", exception));
            if (Interlocked.CompareExchange(ref m_activeGeneration, 0, generation) == generation)
            {
                try
                {
                    if (m_session.State != EClientConnectionState.Disconnected)
                    {
                        await m_session.DisconnectAsync().ConfigureAwait(false);
                    }
                }
                catch (Exception disconnectException)
                {
                    EmitSystemMessage($"Disconnect after dispatch failure failed: {disconnectException.Message}");
                }

                EmitConnectionState(false);
            }
        }
    }

    private bool CompleteResponse<TResponse>(ulong requestId, TResponse response)
        where TResponse : class
    {
        if (!m_pendingRequests.TryGetValue(requestId, out PendingRequest? pendingRequest))
        {
            EmitSystemMessage(
                $"Response has no pending request. type={typeof(TResponse).Name}, requestId={requestId}");
            return true;
        }

        if (pendingRequest.ResponseType != typeof(TResponse))
        {
            pendingRequest.Completion.TrySetException(new InvalidDataException(
                $"Unexpected response type for request {requestId}. " +
                $"expected={pendingRequest.ResponseType.Name}, actual={typeof(TResponse).Name}"));
            return true;
        }

        pendingRequest.Completion.TrySetResult(response);
        return true;
    }

    private async Task DisconnectCoreAsync(string reason, bool notify)
    {
        long disconnectedGeneration = Interlocked.Exchange(ref m_activeGeneration, 0);
        ResetAuthenticatedPolicies();

        if (m_session.State != EClientConnectionState.Disconnected)
        {
            await m_session.DisconnectAsync().ConfigureAwait(false);
        }

        await StopDispatchLoopAsync().ConfigureAwait(false);

        FailPendingRequests(new IOException(reason));
        if (notify && disconnectedGeneration != 0)
        {
            EmitConnectionState(false);
        }
    }

    private void OnSessionDisconnected(FClientDisconnectInfo disconnectInfo)
    {
        Task connectionSetupTask = GetConnectionSetupTask();
        _ = HandleSessionDisconnectedAsync(disconnectInfo, connectionSetupTask);
    }

    private async Task HandleSessionDisconnectedAsync(
        FClientDisconnectInfo disconnectInfo,
        Task connectionSetupTask)
    {
        await connectionSetupTask.ConfigureAwait(false);
        try
        {
            await m_lifecycleLock.WaitAsync().ConfigureAwait(false);
        }
        catch (ObjectDisposedException)
        {
            return;
        }

        try
        {
            if (Interlocked.CompareExchange(
                    ref m_activeGeneration,
                    0,
                    disconnectInfo.ConnectionGeneration) != disconnectInfo.ConnectionGeneration)
            {
                return;
            }

            ResetAuthenticatedPolicies();
            await StopDispatchLoopAsync().ConfigureAwait(false);
            FailPendingRequests(new IOException(disconnectInfo.Message, disconnectInfo.Exception));
            EmitSystemMessage($"Connection lost: {disconnectInfo.Message}");
            EmitConnectionState(false);
        }
        finally
        {
            m_lifecycleLock.Release();
        }
    }

    private void StartDispatchLoop(long generation, CancellationTokenSource stopSource)
    {
        Task dispatchTask = Task.Run(
            () => DispatchLoopAsync(generation, stopSource.Token),
            CancellationToken.None);
        lock (m_dispatchLock)
        {
            m_dispatchStopSource = stopSource;
            m_dispatchTask = dispatchTask;
        }
    }

    private async Task StopDispatchLoopAsync()
    {
        CancellationTokenSource? stopSource;
        Task? dispatchTask;
        lock (m_dispatchLock)
        {
            stopSource = m_dispatchStopSource;
            dispatchTask = m_dispatchTask;
            m_dispatchStopSource = null;
            m_dispatchTask = null;
        }

        if (stopSource is null)
        {
            return;
        }

        try
        {
            stopSource.Cancel();
            if (dispatchTask is not null)
            {
                await dispatchTask.ConfigureAwait(false);
            }
        }
        catch (OperationCanceledException)
        {
        }
        finally
        {
            stopSource.Dispose();
        }
    }

    private TaskCompletionSource BeginConnectionSetup()
    {
        var completion = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        lock (m_connectionSetupLock)
        {
            m_connectionSetupCompletion = completion;
        }
        return completion;
    }

    private Task GetConnectionSetupTask()
    {
        lock (m_connectionSetupLock)
        {
            return m_connectionSetupCompletion?.Task ?? Task.CompletedTask;
        }
    }

    private void CompleteConnectionSetup(TaskCompletionSource? completion)
    {
        if (completion is null)
        {
            return;
        }

        lock (m_connectionSetupLock)
        {
            if (ReferenceEquals(m_connectionSetupCompletion, completion))
            {
                m_connectionSetupCompletion = null;
            }
        }
        completion.TrySetResult();
    }

    private void FailPendingRequests(Exception exception)
    {
        foreach ((ulong requestId, PendingRequest _) in m_pendingRequests)
        {
            if (m_pendingRequests.TryRemove(requestId, out PendingRequest? pendingRequest))
            {
                pendingRequest.Completion.TrySetException(exception);
            }
        }
    }

    private void ResetAuthenticatedPolicies()
    {
        m_authenticatedSearchPageSize = 0;
        m_authenticatedInventoryListPageSize = 0;
        m_authenticatedMailListPageSize = 0;
    }

    private static uint RequirePositivePageSize(uint pageSize, string policyName)
    {
        if (pageSize == 0)
        {
            throw new InvalidDataException($"{policyName} is not available. Authenticate before requesting paged data.");
        }

        return pageSize;
    }

    private void EmitSystemMessage(string message) => InvokeSafely(SystemMessageReceived, message);

    private void EmitConnectionState(bool connected) => InvokeSafely(ConnectionStateChanged, connected);

    private void EmitOutbid(AuctionOutbidNotification notification) => InvokeSafely(OutbidReceived, notification);

    private void EmitAuctionWon(AuctionWonNotification notification) => InvokeSafely(AuctionWonReceived, notification);

    private void InvokeSafely<T>(Action<T>? callbacks, T argument)
    {
        if (callbacks is null)
        {
            return;
        }

        Delegate[] invocationList = callbacks.GetInvocationList();
        m_callbackQueue.Enqueue(() =>
        {
            foreach (Delegate callback in invocationList)
            {
                if (Volatile.Read(ref m_disposed) != 0)
                {
                    return;
                }

                try
                {
                    ((Action<T>)callback)(argument);
                }
                catch
                {
                    // UI/application callbacks must not terminate packet dispatch.
                }
            }
        });
    }

    private void RegisterConnectAttempt(CancellationTokenSource stopSource)
    {
        lock (m_connectAttemptLock)
        {
            m_connectAttemptStopSource = stopSource;
        }
    }

    private bool IsCurrentConnectAttempt(CancellationTokenSource stopSource)
    {
        lock (m_connectAttemptLock)
        {
            return ReferenceEquals(m_connectAttemptStopSource, stopSource);
        }
    }

    private void CompleteConnectAttempt(CancellationTokenSource stopSource)
    {
        lock (m_connectAttemptLock)
        {
            if (ReferenceEquals(m_connectAttemptStopSource, stopSource))
            {
                m_connectAttemptStopSource = null;
            }
        }
    }

    private void CancelConnectAttempt()
    {
        lock (m_connectAttemptLock)
        {
            try
            {
                m_connectAttemptStopSource?.Cancel();
            }
            catch (ObjectDisposedException)
            {
            }
        }
    }

    private static IReadOnlyList<ListingSummary> ConvertListings(ListingSearchRp response)
    {
        int count = response.ListingIds.Count;
        ValidateParallelCount(nameof(ListingSearchRp), count,
            response.SellerUserIds.Count,
            response.SellerLoginIds.Count,
            response.ItemDataIds.Count,
            response.ItemCategories.Count,
            response.Quantities.Count,
            response.Names.Count,
            response.StrStats.Count,
            response.DexStats.Count,
            response.IntStats.Count,
            response.LukStats.Count,
            response.CurrencyIds.Count,
            response.StartPrices.Count,
            response.CurrentBidPrices.Count,
            response.BuyoutPrices.Count,
            response.ExpiresAtUnixMs.Count,
            response.Versions.Count);

        List<ListingSummary> listings = new(count);
        for (int index = 0; index < count; ++index)
        {
            listings.Add(new ListingSummary(
                response.ListingIds[index],
                response.SellerUserIds[index],
                response.SellerLoginIds[index],
                response.ItemDataIds[index],
                response.ItemCategories[index],
                response.Quantities[index],
                response.Names[index],
                response.StrStats[index],
                response.DexStats[index],
                response.IntStats[index],
                response.LukStats[index],
                response.CurrencyIds[index],
                response.StartPrices[index],
                response.CurrentBidPrices[index],
                response.BuyoutPrices[index],
                response.ExpiresAtUnixMs[index],
                response.Versions[index]));
        }
        return listings;
    }

    private static IReadOnlyList<SaleHistorySummary> ConvertSaleHistory(SaleHistorySearchRp response)
    {
        int count = response.ListingIds.Count;
        ValidateParallelCount(nameof(SaleHistorySearchRp), count,
            response.ItemDataIds.Count,
            response.ItemCategories.Count,
            response.Quantities.Count,
            response.Names.Count,
            response.StrStats.Count,
            response.DexStats.Count,
            response.IntStats.Count,
            response.LukStats.Count,
            response.CurrencyIds.Count,
            response.FinalPrices.Count,
            response.SaleTypes.Count,
            response.SoldAtUnixMs.Count);

        List<SaleHistorySummary> history = new(count);
        for (int index = 0; index < count; ++index)
        {
            history.Add(new SaleHistorySummary(
                response.ListingIds[index],
                response.ItemDataIds[index],
                response.ItemCategories[index],
                response.Quantities[index],
                response.Names[index],
                response.StrStats[index],
                response.DexStats[index],
                response.IntStats[index],
                response.LukStats[index],
                response.CurrencyIds[index],
                response.FinalPrices[index],
                response.SaleTypes[index],
                response.SoldAtUnixMs[index]));
        }
        return history;
    }

    private static IReadOnlyList<BidSummary> ConvertBids(MyBidListRp response)
    {
        int count = response.BidIds.Count;
        ValidateParallelCount(nameof(MyBidListRp), count,
            response.ListingIds.Count,
            response.CurrencyIds.Count,
            response.BidAmounts.Count,
            response.BidStates.Count,
            response.BidVersions.Count,
            response.CurrentBidPrices.Count,
            response.ListingStates.Count);

        List<BidSummary> bids = new(count);
        for (int index = 0; index < count; ++index)
        {
            bids.Add(new BidSummary(
                response.BidIds[index],
                response.ListingIds[index],
                response.CurrencyIds[index],
                response.BidAmounts[index],
                response.BidStates[index],
                response.BidVersions[index],
                response.CurrentBidPrices[index],
                response.ListingStates[index]));
        }
        return bids;
    }

    private static IReadOnlyList<InventoryItem> ConvertInventory(InventoryListRp response)
    {
        int count = response.ItemInstanceIds.Count;
        ValidateParallelCount(nameof(InventoryListRp), count,
            response.ItemDataIds.Count,
            response.Quantities.Count,
            response.EquippedStates.Count,
            response.TradableStates.Count,
            response.ItemData.Count,
            response.Versions.Count);

        List<InventoryItem> items = new(count);
        for (int index = 0; index < count; ++index)
        {
            items.Add(new InventoryItem(
                response.ItemInstanceIds[index],
                response.ItemDataIds[index],
                response.Quantities[index],
                response.EquippedStates[index] != 0,
                response.TradableStates[index] != 0,
                response.ItemData[index],
                response.Versions[index]));
        }
        return items;
    }

    private static IReadOnlyList<MailSummary> ConvertMails(MailListRp response)
    {
        int count = response.MailIds.Count;
        ValidateParallelCount(nameof(MailListRp), count,
            response.MailTypes.Count,
            response.Subjects.Count,
            response.States.Count,
            response.ExpiresAtUnixMs.Count,
            response.CreatedAtUnixMs.Count);

        List<MailSummary> mails = new(count);
        for (int index = 0; index < count; ++index)
        {
            mails.Add(new MailSummary(
                response.MailIds[index],
                response.MailTypes[index],
                response.Subjects[index],
                response.States[index],
                response.ExpiresAtUnixMs[index],
                response.CreatedAtUnixMs[index]));
        }
        return mails;
    }

    private static IReadOnlyList<MailAttachment> ConvertMailAttachments(MailDetailRp response)
    {
        int count = response.AttachmentIds.Count;
        ValidateParallelCount(nameof(MailDetailRp), count,
            response.AttachmentTypes.Count,
            response.ItemInstanceIds.Count,
            response.ItemDataIds.Count,
            response.Quantities.Count,
            response.ItemData.Count,
            response.CurrencyIds.Count,
            response.CurrencyAmounts.Count,
            response.AttachmentStates.Count);

        List<MailAttachment> attachments = new(count);
        for (int index = 0; index < count; ++index)
        {
            attachments.Add(new MailAttachment(
                response.AttachmentIds[index],
                response.AttachmentTypes[index],
                response.ItemInstanceIds[index],
                response.ItemDataIds[index],
                response.Quantities[index],
                response.ItemData[index],
                response.CurrencyIds[index],
                response.CurrencyAmounts[index],
                response.AttachmentStates[index]));
        }
        return attachments;
    }

    private static void ValidateParallelCount(string packetName, int expectedCount, params int[] actualCounts)
    {
        if (actualCounts.Any(count => count != expectedCount))
        {
            throw new InvalidDataException($"{packetName} contains mismatched parallel list counts.");
        }
    }

    private void ThrowIfDisposed()
    {
        ObjectDisposedException.ThrowIf(Volatile.Read(ref m_disposed) != 0, this);
    }

    private sealed record PendingRequest(Type ResponseType, TaskCompletionSource<object> Completion);

    private sealed class AuctionPacketHandler(AuctionTcpClient owner) : AuctionPacketHandlerBase
    {
        public override bool HandleAuctionAuthRp(AuctionAuthRp packet) =>
            owner.CompleteResponse(packet.RequestId, packet);

        public override bool HandlePingRp(PingRp packet) =>
            owner.CompleteResponse(packet.RequestId, packet);

        public override bool HandleMyBidListRp(MyBidListRp packet) =>
            owner.CompleteResponse(packet.RequestId, packet);

        public override bool HandleInventoryListRp(InventoryListRp packet) =>
            owner.CompleteResponse(packet.RequestId, packet);

        public override bool HandleListingRegisterRp(ListingRegisterRp packet) =>
            owner.CompleteResponse(packet.RequestId, packet);

        public override bool HandleListingSearchRp(ListingSearchRp packet) =>
            owner.CompleteResponse(packet.RequestId, packet);

        public override bool HandleListingDetailRp(ListingDetailRp packet) =>
            owner.CompleteResponse(packet.RequestId, packet);

        public override bool HandleBidRp(BidRp packet) =>
            owner.CompleteResponse(packet.RequestId, packet);

        public override bool HandleAuctionOutbidNoti(AuctionOutbidNoti packet)
        {
            owner.EmitOutbid(new AuctionOutbidNotification(
                packet.ListingId,
                packet.BidId,
                packet.HeldAmount,
                packet.NewHighestAmount));
            return true;
        }

        public override bool HandleBuyoutRp(BuyoutRp packet) =>
            owner.CompleteResponse(packet.RequestId, packet);

        public override bool HandleMailListRp(MailListRp packet) =>
            owner.CompleteResponse(packet.RequestId, packet);

        public override bool HandleMailDetailRp(MailDetailRp packet) =>
            owner.CompleteResponse(packet.RequestId, packet);

        public override bool HandleMailClaimRp(MailClaimRp packet) =>
            owner.CompleteResponse(packet.RequestId, packet);

        public override bool HandleListingCancelRp(ListingCancelRp packet) =>
            owner.CompleteResponse(packet.RequestId, packet);

        public override bool HandleAuctionWonNoti(AuctionWonNoti packet)
        {
            owner.EmitAuctionWon(new AuctionWonNotification(
                packet.ListingId,
                packet.BidId,
                packet.FinalPrice,
                packet.ItemMailId));
            return true;
        }

        public override bool HandleDebugCheatRp(DebugCheatRp packet) =>
            owner.CompleteResponse(packet.RequestId, packet);

        public override bool HandleSaleHistorySearchRp(SaleHistorySearchRp packet) =>
            owner.CompleteResponse(packet.RequestId, packet);

        public override bool HandleSaleHistoryDetailRp(SaleHistoryDetailRp packet) =>
            owner.CompleteResponse(packet.RequestId, packet);

        public override bool HandleBidRefundRp(BidRefundRp packet) =>
            owner.CompleteResponse(packet.RequestId, packet);
    }
}
