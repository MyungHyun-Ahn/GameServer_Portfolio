using System.Net;
using System.Net.Sockets;
using ClientNetwork.Packet;
using ClientNetwork.Transport;

internal static class Program
{
    private const ushort Opcode = 0x7E01;
    private const byte PacketKey = 0x37;
    private const byte RandomKey = 0x5A;

    private static readonly byte[] ExpectedBody = Convert.FromHexString((
        "01FEA5CC EDEF BEEB 32A4F8EF CDAB89F8 F8F9FAFB FCFDFEEF " +
        "CDAB8967 45230100 00C03F00 00000000 0002C006 000000ED " +
        "959CEAB8 80040000 00007F80 FF030000 00010034 12FFFF").Replace(" ", string.Empty));

    private static readonly byte[] ExpectedFrame = Convert.FromHexString((
        "49005A3C 6253A998 AAF5AD03 AF4B9F3E 39B2008E 6B656979 " +
        "039D29B1 C3CD9CBD 5179D29D DB9EDF5F 60E698A9 E82C64B5 " +
        "2042F885 88ED6B80 E377DD82 93F1E97C 384D459F A9780514 " +
        "A3C7DF9D 84").Replace(" ", string.Empty));

    public static async Task<int> Main()
    {
        try
        {
            TestScalarWriterAndReader();
            TestKnownFrameAndFragmentedDecode();
            TestInvalidInputRejection();
            await TestTransportOrderingAndReconnectAsync();
            await TestReceiveQueueOverflowAsync();
            await TestCallbackOrderingAndReentryAsync();
            Console.WriteLine("ClientNetwork smoke test passed.");
            return 0;
        }
        catch (Exception exception)
        {
            Console.Error.WriteLine($"ClientNetwork smoke test failed: {exception}");
            return 1;
        }
    }

    private static void TestScalarWriterAndReader()
    {
        FPacketWriter writer = BuildCanonicalBody();
        Require(writer.Length == ExpectedBody.Length, "Unexpected serialized body length.");
        Require(writer.WrittenSpan.SequenceEqual(ExpectedBody), "C# body bytes differ from the C++ wire fixture.");

        FPacketReader reader = new(writer.WrittenSpan);
        Require(reader.TryReadBool(out bool boolValue) && boolValue, "bool round-trip failed.");
        Require(reader.TryReadInt8(out sbyte int8Value) && int8Value == -2, "int8 round-trip failed.");
        Require(reader.TryReadUInt8(out byte uint8Value) && uint8Value == 0xA5, "uint8 round-trip failed.");
        Require(reader.TryReadInt16(out short int16Value) && int16Value == -0x1234, "int16 round-trip failed.");
        Require(reader.TryReadUInt16(out ushort uint16Value) && uint16Value == 0xBEEF, "uint16 round-trip failed.");
        Require(reader.TryReadInt32(out int int32Value) && int32Value == -123456789, "int32 round-trip failed.");
        Require(reader.TryReadUInt32(out uint uint32Value) && uint32Value == 0x89ABCDEF, "uint32 round-trip failed.");
        Require(reader.TryReadInt64(out long int64Value) && int64Value == -0x0102030405060708L, "int64 round-trip failed.");
        Require(reader.TryReadUInt64(out ulong uint64Value) && uint64Value == 0x0123456789ABCDEFUL, "uint64 round-trip failed.");
        Require(reader.TryReadFloat(out float floatValue) && floatValue == 1.5F, "float round-trip failed.");
        Require(reader.TryReadDouble(out double doubleValue) && doubleValue == -2.25, "double round-trip failed.");
        Require(reader.TryReadString(out string text) && text == "한글", "UTF-8 string round-trip failed.");
        Require(reader.TryReadBytes(out byte[] bytes) && bytes.AsSpan().SequenceEqual(new byte[] { 0x00, 0x7F, 0x80, 0xFF }),
            "bytes round-trip failed.");
        Require(reader.TryReadCount(out uint count, sizeof(ushort)) && count == 3, "vector count round-trip failed.");
        Require(reader.TryReadUInt16(out ushort first) && first == 1, "vector element 0 round-trip failed.");
        Require(reader.TryReadUInt16(out ushort second) && second == 0x1234, "vector element 1 round-trip failed.");
        Require(reader.TryReadUInt16(out ushort third) && third == 0xFFFF, "vector element 2 round-trip failed.");
        Require(reader.IsAtEnd, "Reader did not consume the entire packet body.");
    }

    private static void TestKnownFrameAndFragmentedDecode()
    {
        FDefaultPacketCipher cipher = new(PacketKey);
        FPacketFrameEncoder encoder = new(cipher);
        byte[] frame = encoder.Encode(Opcode, BuildCanonicalBody().WrittenSpan, RandomKey);
        Require(frame.SequenceEqual(ExpectedFrame), "Encoded frame differs from the C++ wire fixture.");

        FPacketStreamDecoder decoder = new(cipher);
        Require(decoder.TryAppend(frame.AsSpan(0, 1)), "Failed to append the first fragment.");
        Require(decoder.TryReadFrame(out _, out _) == EFrameReadStatus.NeedMoreData,
            "A one-byte fragment must remain incomplete.");

        Require(decoder.TryAppend(frame.AsSpan(1, 3)), "Failed to append the transport header fragment.");
        Require(decoder.TryReadFrame(out _, out _) == EFrameReadStatus.NeedMoreData,
            "A header-only fragment must remain incomplete.");

        Require(decoder.TryAppend(frame.AsSpan(4)), "Failed to append the payload fragment.");
        EFrameReadStatus status = decoder.TryReadFrame(out FDecodedContentFrame? decodedFrame, out EFrameDecodeError error);
        Require(status == EFrameReadStatus.FrameReady && error == EFrameDecodeError.None,
            "The complete frame was not decoded.");

        FDecodedContentFrame validFrame = decodedFrame ?? throw new InvalidOperationException("Decoded frame is null.");
        Require(validFrame.Opcode == Opcode, "Decoded opcode differs from the fixture.");
        Require(validFrame.RandomKey == RandomKey, "Decoded random key differs from the fixture.");
        Require(validFrame.Body.Span.SequenceEqual(ExpectedBody), "Decoded body differs from the fixture.");
        Require(decoder.BufferedByteCount == 0, "Decoded bytes remain in the stream buffer.");
    }

    private static void TestInvalidInputRejection()
    {
        byte[] corruptedFrame = (byte[])ExpectedFrame.Clone();
        corruptedFrame[^1] ^= 0x01;
        FPacketStreamDecoder checksumDecoder = new(new FDefaultPacketCipher(PacketKey));
        Require(checksumDecoder.TryAppend(corruptedFrame), "Failed to append the checksum test frame.");
        Require(checksumDecoder.TryReadFrame(out _, out EFrameDecodeError checksumError) == EFrameReadStatus.InvalidData &&
                checksumError == EFrameDecodeError.ChecksumMismatch && checksumDecoder.IsFaulted,
            "A checksum mismatch must fault the decoder.");

        FPacketStreamDecoder lengthDecoder = new();
        Require(lengthDecoder.TryAppend([0xFD, 0x1F, 0x00, 0x00]), "Failed to append the length test header.");
        Require(lengthDecoder.TryReadFrame(out _, out EFrameDecodeError lengthError) == EFrameReadStatus.InvalidData &&
                lengthError == EFrameDecodeError.InvalidPayloadLength,
            "A frame larger than 8192 bytes must be rejected from its header.");

        ExpectThrows<InvalidOperationException>(() =>
        {
            FPacketWriter oversizedWriter = new();
            oversizedWriter.WriteRaw(new byte[FPacketProtocol.MaxContentBodySize + 1]);
        }, "An oversized body must be rejected by the writer.");

        FPacketReader invalidUtf8Reader = new(new byte[] { 1, 0, 0, 0, 0xFF });
        Require(!invalidUtf8Reader.TryReadString(out _) && invalidUtf8Reader.Offset == 0,
            "Invalid UTF-8 must be rejected without consuming reader state.");

        FPacketReader invalidBoolReader = new([0x02]);
        Require(!invalidBoolReader.TryReadBool(out _) && invalidBoolReader.Offset == 0,
            "bool values other than 0 and 1 must be rejected without consuming reader state.");

        FPacketReader invalidCountReader = new([0xFF, 0xFF, 0xFF, 0xFF]);
        Require(!invalidCountReader.TryReadCount(out _) && invalidCountReader.Offset == 0,
            "An impossible collection count must be rejected without consuming reader state.");
    }

    private static async Task TestTransportOrderingAndReconnectAsync()
    {
        var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        try
        {
            int port = ((IPEndPoint)listener.LocalEndpoint).Port;
            await using var session = new FClientSession(receiveQueueCapacity: 8);
            var options = new FClientConnectionOptions(IPAddress.Loopback.ToString(), port)
            {
                PacketKey = PacketKey,
                ConnectTimeout = TimeSpan.FromSeconds(5),
                MaxQueuedSendPackets = 8
            };

            Task<TcpClient> firstAcceptTask = listener.AcceptTcpClientAsync();
            await session.ConnectAsync(options);
            using TcpClient firstServerClient = await firstAcceptTask.WaitAsync(TimeSpan.FromSeconds(5));
            long firstGeneration = session.ConnectionGeneration;
            Require(firstGeneration > 0 && session.IsConnected, "The first transport connection was not established.");

            bool duplicateConnectRejected = false;
            try
            {
                await session.ConnectAsync(options);
            }
            catch (InvalidOperationException)
            {
                duplicateConnectRejected = true;
            }

            Require(duplicateConnectRejected && session.IsConnected && session.ConnectionGeneration == firstGeneration,
                "A duplicate connect attempt corrupted the active connection state.");

            Require(session.TrySend(new FTestPacket(0x7101, [0x11])), "The first packet was not queued for send.");
            Require(session.TrySend(new FTestPacket(0x7102, [0x22])), "The second packet was not queued for send.");

            IReadOnlyList<FDecodedContentFrame> sentFrames = await ReadFramesAsync(
                firstServerClient.GetStream(),
                expectedCount: 2);
            Require(sentFrames[0].Opcode == 0x7101 && sentFrames[0].Body.Span.SequenceEqual(new byte[] { 0x11 }),
                "The first queued packet was sent out of order.");
            Require(sentFrames[1].Opcode == 0x7102 && sentFrames[1].Body.Span.SequenceEqual(new byte[] { 0x22 }),
                "The second queued packet was sent out of order.");

            var serverEncoder = new FPacketFrameEncoder(new FDefaultPacketCipher(PacketKey));
            byte[] firstReply = serverEncoder.Encode(0x7201, [0x31, 0x32], randomKey: 0x41);
            byte[] secondReply = serverEncoder.Encode(0x7202, [0x42], randomKey: 0x52);
            NetworkStream firstServerStream = firstServerClient.GetStream();
            await firstServerStream.WriteAsync(firstReply.AsMemory(0, 1));
            await firstServerStream.WriteAsync(firstReply.AsMemory(1));
            await firstServerStream.WriteAsync(secondReply);

            await WaitForPacketAsync(session, "The first fragmented reply was not queued.");
            Require(session.TryDequeuePacket(out FReceivedPacket firstPacket), "The first received packet is missing.");
            await WaitForPacketAsync(session, "The second reply was not queued.");
            Require(session.TryDequeuePacket(out FReceivedPacket secondPacket), "The second received packet is missing.");
            Require(firstPacket.ConnectionGeneration == firstGeneration &&
                    firstPacket.Opcode == 0x7201 && firstPacket.Body.Span.SequenceEqual(new byte[] { 0x31, 0x32 }),
                "The first received packet is invalid.");
            Require(secondPacket.ConnectionGeneration == firstGeneration &&
                    secondPacket.Opcode == 0x7202 && secondPacket.Body.Span.SequenceEqual(new byte[] { 0x42 }),
                "The second received packet is invalid.");

            firstServerClient.Dispose();
            await WaitUntilAsync(() => !session.IsConnected, "Remote close was not observed.");
            Require(session.LastDisconnectInfo?.Reason == EClientDisconnectReason.RemoteClosed,
                "Remote close produced an unexpected disconnect reason.");

            Task<TcpClient> secondAcceptTask = listener.AcceptTcpClientAsync();
            await session.ConnectAsync(options);
            using TcpClient secondServerClient = await secondAcceptTask.WaitAsync(TimeSpan.FromSeconds(5));
            Require(session.ConnectionGeneration > firstGeneration,
                "The reconnect did not advance the connection generation.");

            await session.DisconnectAsync();
            Require(session.State == EClientConnectionState.Disconnected,
                "Local disconnect did not return the session to Disconnected.");
            Require(session.LastDisconnectInfo?.Reason == EClientDisconnectReason.LocalRequest,
                "Local disconnect produced an unexpected disconnect reason.");
        }
        finally
        {
            listener.Stop();
        }
    }

    private static async Task TestReceiveQueueOverflowAsync()
    {
        var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        try
        {
            int port = ((IPEndPoint)listener.LocalEndpoint).Port;
            await using var session = new FClientSession(receiveQueueCapacity: 1);
            var options = new FClientConnectionOptions(IPAddress.Loopback.ToString(), port)
            {
                PacketKey = PacketKey,
                ConnectTimeout = TimeSpan.FromSeconds(5)
            };

            Task<TcpClient> acceptTask = listener.AcceptTcpClientAsync();
            await session.ConnectAsync(options);
            using TcpClient serverClient = await acceptTask.WaitAsync(TimeSpan.FromSeconds(5));

            var encoder = new FPacketFrameEncoder(new FDefaultPacketCipher(PacketKey));
            byte[] firstFrame = encoder.Encode(0x7301, [], randomKey: 0x61);
            byte[] secondFrame = encoder.Encode(0x7302, [], randomKey: 0x62);
            byte[] combinedFrames = new byte[firstFrame.Length + secondFrame.Length];
            firstFrame.CopyTo(combinedFrames, 0);
            secondFrame.CopyTo(combinedFrames, firstFrame.Length);
            await serverClient.GetStream().WriteAsync(combinedFrames);

            await WaitUntilAsync(() => !session.IsConnected, "Receive queue overflow did not close the connection.");
            Require(session.LastDisconnectInfo?.Reason == EClientDisconnectReason.ReceiveQueueOverflow,
                "Receive queue overflow produced an unexpected disconnect reason.");
            Require(session.PendingPacketCount == 1,
                "The bounded receive queue did not preserve its configured limit.");
        }
        finally
        {
            listener.Stop();
        }
    }

    private static async Task TestCallbackOrderingAndReentryAsync()
    {
        var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        try
        {
            int port = ((IPEndPoint)listener.LocalEndpoint).Port;
            await using var session = new FClientSession();
            var callbackOrder = new List<string>();
            var disconnectCallbackSource = new TaskCompletionSource<bool>(
                TaskCreationOptions.RunContinuationsAsynchronously);

            session.Connected += _ =>
            {
                callbackOrder.Add("Connected");
                session.DisconnectAsync().GetAwaiter().GetResult();
            };
            session.Disconnected += _ =>
            {
                callbackOrder.Add("Disconnected");
                disconnectCallbackSource.TrySetResult(true);
            };

            Task<TcpClient> acceptTask = listener.AcceptTcpClientAsync();
            await session.ConnectAsync(new FClientConnectionOptions(IPAddress.Loopback.ToString(), port)
            {
                ConnectTimeout = TimeSpan.FromSeconds(5)
            });
            using TcpClient serverClient = await acceptTask.WaitAsync(TimeSpan.FromSeconds(5));
            await disconnectCallbackSource.Task.WaitAsync(TimeSpan.FromSeconds(5));

            Require(callbackOrder.SequenceEqual(new[] { "Connected", "Disconnected" }),
                "Connection callbacks were reordered or callback reentry deadlocked.");
            Require(session.State == EClientConnectionState.Disconnected,
                "A disconnect requested from the Connected callback did not complete.");
        }
        finally
        {
            listener.Stop();
        }
    }

    private static async Task<IReadOnlyList<FDecodedContentFrame>> ReadFramesAsync(
        NetworkStream stream,
        int expectedCount)
    {
        using var timeoutSource = new CancellationTokenSource(TimeSpan.FromSeconds(5));
        var decoder = new FPacketStreamDecoder(new FDefaultPacketCipher(PacketKey));
        var frames = new List<FDecodedContentFrame>(expectedCount);
        byte[] buffer = new byte[256];

        while (frames.Count < expectedCount)
        {
            int receivedByteCount = await stream.ReadAsync(buffer, timeoutSource.Token);
            Require(receivedByteCount > 0, "The client connection closed before all sent packets arrived.");
            Require(decoder.TryAppend(buffer.AsSpan(0, receivedByteCount)), "The server-side decoder rejected sent bytes.");

            while (true)
            {
                EFrameReadStatus status = decoder.TryReadFrame(
                    out FDecodedContentFrame? frame,
                    out EFrameDecodeError error);
                if (status == EFrameReadStatus.NeedMoreData)
                {
                    break;
                }

                Require(status == EFrameReadStatus.FrameReady && error == EFrameDecodeError.None && frame is not null,
                    $"A sent frame could not be decoded: {error}.");
                frames.Add(frame ?? throw new InvalidOperationException("Decoded sent frame is null."));
            }
        }

        return frames;
    }

    private static async Task WaitUntilAsync(
        Func<bool> condition,
        string failureMessage)
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

    private static async Task WaitForPacketAsync(
        FClientSession session,
        string failureMessage)
    {
        using var timeoutSource = new CancellationTokenSource(TimeSpan.FromSeconds(5));
        try
        {
            Require(await session.WaitToReadPacketAsync(timeoutSource.Token), failureMessage);
        }
        catch (OperationCanceledException exception) when (timeoutSource.IsCancellationRequested)
        {
            throw new TimeoutException(failureMessage, exception);
        }
    }

    private static FPacketWriter BuildCanonicalBody()
    {
        FPacketWriter writer = new();
        writer.WriteBool(true);
        writer.WriteInt8(-2);
        writer.WriteUInt8(0xA5);
        writer.WriteInt16(-0x1234);
        writer.WriteUInt16(0xBEEF);
        writer.WriteInt32(-123456789);
        writer.WriteUInt32(0x89ABCDEF);
        writer.WriteInt64(-0x0102030405060708L);
        writer.WriteUInt64(0x0123456789ABCDEFUL);
        writer.WriteFloat(1.5F);
        writer.WriteDouble(-2.25);
        writer.WriteString("한글");
        writer.WriteBytes([0x00, 0x7F, 0x80, 0xFF]);
        writer.WriteCount(3);
        writer.WriteUInt16(1);
        writer.WriteUInt16(0x1234);
        writer.WriteUInt16(0xFFFF);
        return writer;
    }

    private sealed class FTestPacket : IContentPacket
    {
        private readonly byte[] m_body;

        public FTestPacket(
            ushort opcode,
            byte[] body)
        {
            Opcode = opcode;
            m_body = body;
        }

        public ushort Opcode { get; }
        public EContentPacketKind PacketKind => EContentPacketKind.Request;

        public void SerializeBody(FPacketWriter writer) => writer.WriteRaw(m_body);
    }

    private static void ExpectThrows<TException>(Action action, string message)
        where TException : Exception
    {
        try
        {
            action();
        }
        catch (TException)
        {
            return;
        }

        throw new InvalidOperationException(message);
    }

    private static void Require(bool condition, string message)
    {
        if (!condition)
        {
            throw new InvalidOperationException(message);
        }
    }
}
