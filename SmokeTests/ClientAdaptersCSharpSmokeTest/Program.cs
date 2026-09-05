using System.Collections.Concurrent;
using System.Diagnostics;
using System.Net;
using System.Net.Sockets;
using AuctionClientWinForms.Models;
using AuctionClientWinForms.Networking;
using ChattingClientWinForms.Models;
using ChattingClientWinForms.Networking;
using ClientNetwork.Packet;
using ClientNetwork.Threading;
using Generated.Packets.Auction;
using Generated.Packets.Chatting;
using Generated.Packets.Login;

internal static class Program
{
    private const byte PacketKey = 0x37;

    public static async Task<int> Main()
    {
        try
        {
            await TestAuctionAdapterAsync();
            await TestChattingAdapterAsync();
            await TestImmediateRemoteCloseAsync();
            await TestReconnectAgainstStaleDisconnectAsync();
            await TestSerializedCallbackQueueQuiescenceAsync();
            await TestWrapperCallbackQuiescenceAsync();
            await TestPendingConnectCancellationAsync();
            Console.WriteLine("C# client adapter smoke test passed.");
            return 0;
        }
        catch (Exception exception)
        {
            Console.Error.WriteLine($"C# client adapter smoke test failed: {exception}");
            return 1;
        }
    }

    private static async Task TestAuctionAdapterAsync()
    {
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        int port = ((IPEndPoint)listener.LocalEndpoint).Port;
        var releaseServer = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        Task serverTask = RunAuctionServerAsync(listener, releaseServer.Task);

        await using var client = new AuctionTcpClient();
        var connectionStates = new ConcurrentQueue<bool>();
        var outbidCompletion = new TaskCompletionSource<AuctionOutbidNotification>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        client.ConnectionStateChanged += connectionStates.Enqueue;
        client.SystemMessageReceived += _ => throw new InvalidOperationException("Subscriber isolation fixture.");
        client.OutbidReceived += _ => throw new InvalidOperationException("Subscriber isolation fixture.");
        client.OutbidReceived += notification => outbidCompletion.TrySetResult(notification);

        await client.ConnectAsync(new AuctionConnectionSettings(IPAddress.Loopback.ToString(), port, PacketKey));
        AuctionAuthResult auth = await client.AuthenticateAsync("fixture-ticket");
        Require(auth.ResultCode == 0 && auth.UserId == 7001 && auth.MaxActiveListings == 5,
            "AuctionAuth response mapping failed.");

        AuctionOutbidNotification outbid = await outbidCompletion.Task.WaitAsync(TimeSpan.FromSeconds(5));
        Require(outbid.ListingId == 9001 && outbid.NewHighestAmount == 1500,
            "Auction notification mapping failed.");

        (ushort resultCode, IReadOnlyList<BidSummary> bids) = await client.GetMyBidsAsync();
        Require(resultCode == 0 && bids.Count == 21 && bids[0].BidId == 100 && bids[^1].BidId == 80,
            "Auction cursor pagination failed.");

        await client.DisconnectAsync("Smoke complete.");
        releaseServer.TrySetResult();
        await serverTask.WaitAsync(TimeSpan.FromSeconds(5));
        await WaitUntilAsync(
            () => connectionStates.Count >= 2,
            "Auction disconnected event was not delivered.");
        bool[] observedStates = connectionStates.ToArray();
        Require(observedStates.SequenceEqual([true, false]),
            $"Auction connection state events were duplicated or reordered. " +
            $"Observed=[{string.Join(", ", observedStates)}].");
    }

    private static async Task RunAuctionServerAsync(TcpListener listener, Task releaseTask)
    {
        using TcpClient serverClient = await listener.AcceptTcpClientAsync();
        var connection = new FServerConnection(serverClient.GetStream(), PacketKey);

        FDecodedContentFrame authFrame = await connection.ReceiveAsync();
        if (authFrame.Opcode != AuctionAuthRq.OpcodeValue ||
            !AuctionAuthRq.TryDeserializeBody(authFrame.Body.Span, out AuctionAuthRq? authRequest) ||
            authRequest is null || authRequest.Ticket != "fixture-ticket")
        {
            throw new InvalidOperationException("AuctionAuth request did not use generated serialization.");
        }
        await connection.SendAsync(new AuctionAuthRp
        {
            ResultCode = 0,
            RequestId = authRequest.RequestId,
            UserId = 7001,
            MaxActiveListings = 5,
            SearchPageSize = 20,
            InventoryListPageSize = 20,
            MailListPageSize = 20,
            MinimumListingDurationSeconds = 60,
            MaximumListingDurationSeconds = 3600,
            DefaultListingDurationSeconds = 600
        });
        await connection.SendAsync(new AuctionOutbidNoti
        {
            ListingId = 9001,
            BidId = 8001,
            HeldAmount = 1000,
            NewHighestAmount = 1500
        });

        FDecodedContentFrame firstBidFrame = await connection.ReceiveAsync();
        if (!MyBidListRq.TryDeserializeBody(firstBidFrame.Body.Span, out MyBidListRq? firstRequest) ||
            firstRequest is null || firstRequest.CursorBidId != 0 || firstRequest.Limit != 20)
        {
            throw new InvalidOperationException("The first bid page request is invalid.");
        }
        await connection.SendAsync(CreateBidPage(firstRequest.RequestId, 100, 20));

        FDecodedContentFrame secondBidFrame = await connection.ReceiveAsync();
        if (!MyBidListRq.TryDeserializeBody(secondBidFrame.Body.Span, out MyBidListRq? secondRequest) ||
            secondRequest is null || secondRequest.CursorBidId != 81)
        {
            throw new InvalidOperationException("The second bid page did not continue from the last cursor.");
        }
        await connection.SendAsync(CreateBidPage(secondRequest.RequestId, 80, 1));

        await releaseTask.WaitAsync(TimeSpan.FromSeconds(5));
    }

    private static MyBidListRp CreateBidPage(ulong requestId, ulong firstBidId, int count)
    {
        var response = new MyBidListRp { ResultCode = 0, RequestId = requestId };
        for (int index = 0; index < count; ++index)
        {
            ulong bidId = firstBidId - (ulong)index;
            response.BidIds.Add(bidId);
            response.ListingIds.Add(1000 + bidId);
            response.CurrencyIds.Add(1);
            response.BidAmounts.Add(500 + bidId);
            response.BidStates.Add(1);
            response.BidVersions.Add(1);
            response.CurrentBidPrices.Add(500 + bidId);
            response.ListingStates.Add(2);
        }
        return response;
    }

    private static async Task TestChattingAdapterAsync()
    {
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        int port = ((IPEndPoint)listener.LocalEndpoint).Port;
        var releaseServer = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        Task serverTask = RunChattingServerAsync(listener, releaseServer.Task);

        await using var client = new ChattingTcpClient();
        var connectionStates = new ConcurrentQueue<bool>();
        var loginCompletion = new TaskCompletionSource<LoginResult>(TaskCreationOptions.RunContinuationsAsynchronously);
        var roomCompletion = new TaskCompletionSource<IReadOnlyList<ChatRoomInfo>>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        client.ConnectionStateChanged += connectionStates.Enqueue;
        client.LoginResultReceived += result => loginCompletion.TrySetResult(result);
        client.RoomListReceived += rooms => roomCompletion.TrySetResult(rooms);

        await client.ConnectAsync(new ClientConnectionSettings(IPAddress.Loopback.ToString(), port, PacketKey));
        await client.SendLoginAuthAsync("chat-ticket");
        LoginResult login = await loginCompletion.Task.WaitAsync(TimeSpan.FromSeconds(5));
        Require(login.Success && login.UserId == 77, "LoginAuth response mapping failed.");

        await client.SendRoomListAsync();
        IReadOnlyList<ChatRoomInfo> rooms = await roomCompletion.Task.WaitAsync(TimeSpan.FromSeconds(5));
        Require(rooms.Count == 2 && rooms[1].RoomName == "Dungeon" && !rooms[1].Joinable,
            "RoomList response mapping failed.");

        await client.DisconnectAsync("Smoke complete.");
        releaseServer.TrySetResult();
        await serverTask.WaitAsync(TimeSpan.FromSeconds(5));
        await WaitUntilAsync(
            () => connectionStates.Count >= 2,
            "Chatting disconnected event was not delivered.");
        bool[] observedStates = connectionStates.ToArray();
        Require(observedStates.SequenceEqual([true, false]),
            $"Chatting connection state events were duplicated or reordered. " +
            $"Observed=[{string.Join(", ", observedStates)}].");
    }

    private static async Task RunChattingServerAsync(TcpListener listener, Task releaseTask)
    {
        using TcpClient serverClient = await listener.AcceptTcpClientAsync();
        var connection = new FServerConnection(serverClient.GetStream(), PacketKey);

        FDecodedContentFrame loginFrame = await connection.ReceiveAsync();
        Require(loginFrame.Opcode == LoginAuthRq.OpcodeValue &&
                LoginAuthRq.TryDeserializeBody(loginFrame.Body.Span, out LoginAuthRq? loginRequest) &&
                loginRequest is not null && loginRequest.Ticket == "chat-ticket",
            "LoginAuth request did not use generated serialization.");
        await connection.SendAsync(new LoginAuthRp { UserId = 77, Success = true });

        FDecodedContentFrame roomFrame = await connection.ReceiveAsync();
        Require(roomFrame.Opcode == RoomListRq.OpcodeValue &&
                RoomListRq.TryDeserializeBody(roomFrame.Body.Span, out RoomListRq? roomRequest) &&
                roomRequest is not null,
            "RoomList request did not use generated serialization.");
        await connection.SendAsync(new RoomListRp
        {
            RoomIds = [1, 2],
            RoomNames = ["Lobby", "Dungeon"],
            ParticipantCounts = [10, 20],
            Capacities = [100, 20],
            JoinableFlags = [1, 0]
        });

        await releaseTask.WaitAsync(TimeSpan.FromSeconds(5));
    }

    private static async Task TestImmediateRemoteCloseAsync()
    {
        await TestAuctionImmediateRemoteCloseAsync();
        await TestChattingImmediateRemoteCloseAsync();
    }

    private static async Task TestReconnectAgainstStaleDisconnectAsync()
    {
        await TestAuctionReconnectAsync();
        await TestChattingReconnectAsync();
    }

    private static async Task TestAuctionReconnectAsync()
    {
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        int port = ((IPEndPoint)listener.LocalEndpoint).Port;
        var settings = new AuctionConnectionSettings(IPAddress.Loopback.ToString(), port, PacketKey);
        await using var client = new AuctionTcpClient();
        var states = new ConcurrentQueue<bool>();
        client.ConnectionStateChanged += states.Enqueue;

        Task<TcpClient> firstAcceptTask = listener.AcceptTcpClientAsync();
        await client.ConnectAsync(settings);
        using TcpClient firstServerClient = await firstAcceptTask.WaitAsync(TimeSpan.FromSeconds(5));
        await WaitUntilAsync(() => states.Count >= 1, "Auction initial connected event was not delivered.");

        Task<TcpClient> secondAcceptTask = listener.AcceptTcpClientAsync();
        firstServerClient.LingerState = new LingerOption(true, 0);
        firstServerClient.Close();
        await client.ConnectAsync(settings);
        using TcpClient secondServerClient = await secondAcceptTask.WaitAsync(TimeSpan.FromSeconds(5));
        await WaitUntilAsync(() => states.Count >= 3, "Auction reconnect events were not delivered.");
        await Task.Delay(100);
        Require(client.IsConnected && states.SequenceEqual([true, false, true]),
            "A stale Auction disconnect invalidated the replacement connection.");

        await client.DisconnectAsync("Smoke complete.");
        await WaitUntilAsync(() => states.Count >= 4, "Auction reconnect disconnect event was not delivered.");
        Require(states.SequenceEqual([true, false, true, false]),
            "Auction reconnect connection events were duplicated or reordered.");
    }

    private static async Task TestChattingReconnectAsync()
    {
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        int port = ((IPEndPoint)listener.LocalEndpoint).Port;
        var settings = new ClientConnectionSettings(IPAddress.Loopback.ToString(), port, PacketKey);
        await using var client = new ChattingTcpClient();
        var states = new ConcurrentQueue<bool>();
        client.ConnectionStateChanged += states.Enqueue;

        Task<TcpClient> firstAcceptTask = listener.AcceptTcpClientAsync();
        await client.ConnectAsync(settings);
        using TcpClient firstServerClient = await firstAcceptTask.WaitAsync(TimeSpan.FromSeconds(5));
        await WaitUntilAsync(() => states.Count >= 1, "Chatting initial connected event was not delivered.");

        Task<TcpClient> secondAcceptTask = listener.AcceptTcpClientAsync();
        firstServerClient.LingerState = new LingerOption(true, 0);
        firstServerClient.Close();
        await client.ConnectAsync(settings);
        using TcpClient secondServerClient = await secondAcceptTask.WaitAsync(TimeSpan.FromSeconds(5));
        await WaitUntilAsync(() => states.Count >= 3, "Chatting reconnect events were not delivered.");
        await Task.Delay(100);
        Require(client.IsConnected && states.SequenceEqual([true, false, true]),
            "A stale Chatting disconnect invalidated the replacement connection.");

        await client.DisconnectAsync("Smoke complete.");
        await WaitUntilAsync(() => states.Count >= 4, "Chatting reconnect disconnect event was not delivered.");
        Require(states.SequenceEqual([true, false, true, false]),
            "Chatting reconnect connection events were duplicated or reordered.");
    }

    private static async Task TestAuctionImmediateRemoteCloseAsync()
    {
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        int port = ((IPEndPoint)listener.LocalEndpoint).Port;
        Task closeTask = AcceptAndResetAsync(listener);
        await using var client = new AuctionTcpClient();
        var states = new ConcurrentQueue<bool>();
        var disconnected = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        client.ConnectionStateChanged += connected =>
        {
            states.Enqueue(connected);
            if (!connected)
            {
                disconnected.TrySetResult();
            }
        };

        await client.ConnectAsync(new AuctionConnectionSettings(IPAddress.Loopback.ToString(), port, PacketKey));
        await closeTask;
        await disconnected.Task.WaitAsync(TimeSpan.FromSeconds(5));
        Require(states.SequenceEqual([true, false]),
            "Auction immediate close reordered or lost connection events.");
    }

    private static async Task TestChattingImmediateRemoteCloseAsync()
    {
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        int port = ((IPEndPoint)listener.LocalEndpoint).Port;
        Task closeTask = AcceptAndResetAsync(listener);
        await using var client = new ChattingTcpClient();
        var states = new ConcurrentQueue<bool>();
        var disconnected = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        client.ConnectionStateChanged += connected =>
        {
            states.Enqueue(connected);
            if (!connected)
            {
                disconnected.TrySetResult();
            }
        };

        await client.ConnectAsync(new ClientConnectionSettings(IPAddress.Loopback.ToString(), port, PacketKey));
        await closeTask;
        await disconnected.Task.WaitAsync(TimeSpan.FromSeconds(5));
        Require(states.SequenceEqual([true, false]),
            "Chatting immediate close reordered or lost connection events.");
    }

    private static async Task AcceptAndResetAsync(TcpListener listener)
    {
        using TcpClient client = await listener.AcceptTcpClientAsync();
        client.LingerState = new LingerOption(true, 0);
        client.Close();
    }

    private static async Task TestSerializedCallbackQueueQuiescenceAsync()
    {
        var queue = new FSerializedCallbackQueue();
        using var callbackStarted = new ManualResetEventSlim();
        using var releaseCallback = new ManualResetEventSlim();
        int callbackCount = 0;
        queue.Enqueue(() =>
        {
            callbackStarted.Set();
            releaseCallback.Wait();
            Interlocked.Increment(ref callbackCount);
        });

        Require(callbackStarted.Wait(TimeSpan.FromSeconds(5)), "Serialized callback did not start.");
        Task disposeTask = queue.DisposeAsync().AsTask();
        await Task.Delay(100);
        Require(!disposeTask.IsCompleted, "Callback queue disposal did not wait for the active callback.");
        releaseCallback.Set();
        await disposeTask.WaitAsync(TimeSpan.FromSeconds(5));

        queue.Enqueue(() => Interlocked.Increment(ref callbackCount));
        await Task.Delay(50);
        Require(callbackCount == 1, "Callback queue accepted work after disposal.");
    }

    private static async Task TestWrapperCallbackQuiescenceAsync()
    {
        await TestAuctionCallbackQuiescenceAsync();
        await TestChattingCallbackQuiescenceAsync();
    }

    private static async Task TestAuctionCallbackQuiescenceAsync()
    {
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        int port = ((IPEndPoint)listener.LocalEndpoint).Port;
        Task<TcpClient> acceptTask = listener.AcceptTcpClientAsync();
        var client = new AuctionTcpClient();
        using var callbackStarted = new ManualResetEventSlim();
        using var releaseCallback = new ManualResetEventSlim();
        client.ConnectionStateChanged += connected =>
        {
            if (connected)
            {
                callbackStarted.Set();
                releaseCallback.Wait();
            }
        };

        await client.ConnectAsync(new AuctionConnectionSettings(IPAddress.Loopback.ToString(), port, PacketKey));
        using TcpClient serverClient = await acceptTask.WaitAsync(TimeSpan.FromSeconds(5));
        Require(callbackStarted.Wait(TimeSpan.FromSeconds(5)), "Auction callback did not start.");
        Task disposeTask = client.DisposeAsync().AsTask();
        await Task.Delay(100);
        Require(!disposeTask.IsCompleted, "Auction DisposeAsync returned before its callback completed.");
        releaseCallback.Set();
        await disposeTask.WaitAsync(TimeSpan.FromSeconds(5));
    }

    private static async Task TestChattingCallbackQuiescenceAsync()
    {
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        int port = ((IPEndPoint)listener.LocalEndpoint).Port;
        Task<TcpClient> acceptTask = listener.AcceptTcpClientAsync();
        var client = new ChattingTcpClient();
        using var callbackStarted = new ManualResetEventSlim();
        using var releaseCallback = new ManualResetEventSlim();
        client.ConnectionStateChanged += connected =>
        {
            if (connected)
            {
                callbackStarted.Set();
                releaseCallback.Wait();
            }
        };

        await client.ConnectAsync(new ClientConnectionSettings(IPAddress.Loopback.ToString(), port, PacketKey));
        using TcpClient serverClient = await acceptTask.WaitAsync(TimeSpan.FromSeconds(5));
        Require(callbackStarted.Wait(TimeSpan.FromSeconds(5)), "Chatting callback did not start.");
        Task disposeTask = client.DisposeAsync().AsTask();
        await Task.Delay(100);
        Require(!disposeTask.IsCompleted, "Chatting DisposeAsync returned before its callback completed.");
        releaseCallback.Set();
        await disposeTask.WaitAsync(TimeSpan.FromSeconds(5));
    }

    private static async Task TestPendingConnectCancellationAsync()
    {
        await TestAuctionPendingConnectCancellationAsync();
        await TestChattingPendingConnectCancellationAsync();
    }

    private static async Task TestAuctionPendingConnectCancellationAsync()
    {
        (TcpListener listener, List<TcpClient> blockers) = await CreateSaturatedListenerAsync();
        try
        {
            int port = ((IPEndPoint)listener.LocalEndpoint).Port;
            var client = new AuctionTcpClient();
            Task connectTask = client.ConnectAsync(
                new AuctionConnectionSettings(IPAddress.Loopback.ToString(), port, PacketKey));
            await RequirePendingAsync(connectTask, "Auction connect did not remain pending on a saturated backlog.");

            Stopwatch stopwatch = Stopwatch.StartNew();
            await client.DisconnectAsync("Cancel pending connect.").WaitAsync(TimeSpan.FromSeconds(2));
            stopwatch.Stop();
            Require(stopwatch.Elapsed < TimeSpan.FromSeconds(2),
                "Auction DisconnectAsync did not promptly cancel the pending connect.");
            await ObserveCanceledConnectAsync(connectTask);
            await client.DisposeAsync();
        }
        finally
        {
            listener.Stop();
            blockers.ForEach(client => client.Dispose());
        }
    }

    private static async Task TestChattingPendingConnectCancellationAsync()
    {
        (TcpListener listener, List<TcpClient> blockers) = await CreateSaturatedListenerAsync();
        try
        {
            int port = ((IPEndPoint)listener.LocalEndpoint).Port;
            var client = new ChattingTcpClient();
            Task connectTask = client.ConnectAsync(
                new ClientConnectionSettings(IPAddress.Loopback.ToString(), port, PacketKey));
            await RequirePendingAsync(connectTask, "Chatting connect did not remain pending on a saturated backlog.");

            Stopwatch stopwatch = Stopwatch.StartNew();
            await client.DisposeAsync().AsTask().WaitAsync(TimeSpan.FromSeconds(2));
            stopwatch.Stop();
            Require(stopwatch.Elapsed < TimeSpan.FromSeconds(2),
                "Chatting DisposeAsync did not promptly cancel the pending connect.");
            await ObserveCanceledConnectAsync(connectTask);
        }
        finally
        {
            listener.Stop();
            blockers.ForEach(client => client.Dispose());
        }
    }

    private static async Task<(TcpListener Listener, List<TcpClient> Blockers)> CreateSaturatedListenerAsync()
    {
        var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start(1);
        int port = ((IPEndPoint)listener.LocalEndpoint).Port;
        var blockers = new List<TcpClient>();
        var connectTasks = new List<Task>();

        for (int batch = 0; batch < 32; ++batch)
        {
            for (int index = 0; index < 16; ++index)
            {
                var blocker = new TcpClient();
                blockers.Add(blocker);
                connectTasks.Add(blocker.ConnectAsync(IPAddress.Loopback, port));
            }

            await Task.Delay(25);
            if (connectTasks.Any(task => !task.IsCompleted))
            {
                return (listener, blockers);
            }
        }

        listener.Stop();
        blockers.ForEach(client => client.Dispose());
        throw new InvalidOperationException("Could not saturate the local TCP accept backlog.");
    }

    private static async Task RequirePendingAsync(Task connectTask, string failureMessage)
    {
        await Task.Delay(100);
        Require(!connectTask.IsCompleted, failureMessage);
    }

    private static async Task ObserveCanceledConnectAsync(Task connectTask)
    {
        try
        {
            await connectTask.WaitAsync(TimeSpan.FromSeconds(2));
            throw new InvalidOperationException("A canceled connect unexpectedly succeeded.");
        }
        catch (OperationCanceledException)
        {
        }
        catch (ObjectDisposedException)
        {
        }
    }

    private sealed class FServerConnection(NetworkStream stream, byte packetKey)
    {
        private readonly FPacketStreamDecoder m_decoder = new(new FDefaultPacketCipher(packetKey));
        private readonly FPacketFrameEncoder m_encoder = new(new FDefaultPacketCipher(packetKey));
        private readonly byte[] m_buffer = new byte[4096];

        public async Task<FDecodedContentFrame> ReceiveAsync()
        {
            using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(5));
            while (true)
            {
                EFrameReadStatus status = m_decoder.TryReadFrame(
                    out FDecodedContentFrame? frame,
                    out EFrameDecodeError error);
                if (status == EFrameReadStatus.FrameReady && frame is not null)
                {
                    return frame;
                }
                Require(status == EFrameReadStatus.NeedMoreData,
                    $"The client frame is invalid: {error}.");

                int byteCount = await stream.ReadAsync(m_buffer, timeout.Token);
                Require(byteCount > 0, "The client closed before the expected packet arrived.");
                Require(m_decoder.TryAppend(m_buffer.AsSpan(0, byteCount)),
                    "The server decoder rejected the client frame.");
            }
        }

        public Task SendAsync(IContentPacket packet)
        {
            byte[] frame = m_encoder.Encode(packet);
            return stream.WriteAsync(frame).AsTask();
        }
    }

    private static async Task WaitUntilAsync(Func<bool> condition, string failureMessage)
    {
        DateTime deadline = DateTime.UtcNow + TimeSpan.FromSeconds(5);
        while (!condition())
        {
            if (DateTime.UtcNow >= deadline)
            {
                throw new TimeoutException(failureMessage);
            }
            await Task.Delay(10);
        }
    }

    private static void Require(bool condition, string message)
    {
        if (!condition)
        {
            throw new InvalidOperationException(message);
        }
    }
}
