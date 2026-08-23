using System.Buffers.Binary;
using ChattingClientWinForms.Models;

namespace ChattingClientWinForms.Networking;

internal sealed record DecodedPacket(ushort Opcode, byte[] Body);

internal static class ChattingPacketCodec
{
    public const ushort LoginRqOpcode = 2000;
    public const ushort LoginRpOpcode = 2001;
    public const ushort LoginAuthRqOpcode = 2002;
    public const ushort LoginAuthRpOpcode = 2003;
    public const ushort RoomListRqOpcode = 3100;
    public const ushort RoomListRpOpcode = 3101;
    public const ushort RoomChangeRqOpcode = 3102;
    public const ushort RoomChangeRpOpcode = 3103;
    public const ushort ChattingRqOpcode = 3104;
    public const ushort ChattingRpOpcode = 3105;
    public const ushort BroadcastOpcode = 3106;

    private const int TransportHeaderSize = 4;
    private const int ContentHeaderSize = 2;

    public static byte[] CreateLoginRequestPacket(uint userId, byte packetKey)
    {
        return BuildPacket(LoginRqOpcode, writer => writer.WriteUInt32(userId), packetKey);
    }

    public static byte[] CreateLoginAuthRequestPacket(string ticket, byte packetKey)
    {
        return BuildPacket(LoginAuthRqOpcode, writer => writer.WriteString(ticket), packetKey);
    }

    public static byte[] CreateRoomListRequestPacket(byte packetKey)
    {
        return BuildPacket(RoomListRqOpcode, static _ => { }, packetKey);
    }

    public static byte[] CreateRoomChangeRequestPacket(uint targetRoomId, byte packetKey)
    {
        return BuildPacket(RoomChangeRqOpcode, writer => writer.WriteUInt32(targetRoomId), packetKey);
    }

    public static byte[] CreateChattingRequestPacket(
        uint roomId,
        ulong clientMessageId,
        ulong sentTick,
        byte[] payload,
        byte packetKey)
    {
        return BuildPacket(ChattingRqOpcode, writer =>
        {
            writer.WriteUInt32(roomId);
            writer.WriteUInt64(clientMessageId);
            writer.WriteUInt64(sentTick);
            writer.WriteBytes(payload);
        }, packetKey);
    }

    public static bool TryExtractPacket(
        List<byte> receiveBuffer,
        byte packetKey,
        out DecodedPacket? decodedPacket,
        out string? errorMessage)
    {
        decodedPacket = null;
        errorMessage = null;

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
        byte[] encodedPayload = receiveBuffer.GetRange(TransportHeaderSize, payloadLength).ToArray();
        receiveBuffer.RemoveRange(0, packetLength);

        byte actualChecksum = CalculateChecksum(encodedPayload);
        if (actualChecksum != checksum)
        {
            errorMessage = $"packet checksum mismatch. expected={checksum}, actual={actualChecksum}";
            return false;
        }

        DecodeInPlace(encodedPayload, randomKey, packetKey);
        if (encodedPayload.Length < ContentHeaderSize)
        {
            errorMessage = "content payload too short.";
            return false;
        }

        ushort opcode = BinaryPrimitives.ReadUInt16LittleEndian(encodedPayload.AsSpan(0, ContentHeaderSize));
        decodedPacket = new DecodedPacket(opcode, encodedPayload.AsSpan(ContentHeaderSize).ToArray());
        return true;
    }

    public static bool TryReadLoginResult(DecodedPacket packet, out LoginResult result)
    {
        result = new LoginResult(0, false);
        if (packet.Opcode != LoginRpOpcode && packet.Opcode != LoginAuthRpOpcode)
        {
            return false;
        }

        var reader = new PacketBinaryReader(packet.Body);
        return reader.TryReadUInt32(out uint userId)
            && reader.TryReadBool(out bool success)
            && reader.IsAtEnd
            && Assign(out result, new LoginResult(userId, success));
    }

    public static bool TryReadRoomListResult(DecodedPacket packet, out IReadOnlyList<ChatRoomInfo> rooms)
    {
        rooms = [];
        if (packet.Opcode != RoomListRpOpcode)
        {
            return false;
        }

        var reader = new PacketBinaryReader(packet.Body);
        if (!reader.TryReadUInt32List(out List<uint> roomIds) ||
            !reader.TryReadStringList(out List<string> roomNames) ||
            !reader.TryReadUInt32List(out List<uint> participantCounts) ||
            !reader.TryReadUInt32List(out List<uint> capacities) ||
            !reader.TryReadByteList(out List<byte> joinableFlags) ||
            !reader.IsAtEnd)
        {
            return false;
        }

        if (roomIds.Count != roomNames.Count ||
            roomIds.Count != participantCounts.Count ||
            roomIds.Count != capacities.Count ||
            roomIds.Count != joinableFlags.Count)
        {
            return false;
        }

        List<ChatRoomInfo> parsedRooms = new(roomIds.Count);
        for (int index = 0; index < roomIds.Count; ++index)
        {
            parsedRooms.Add(new ChatRoomInfo(
                roomIds[index],
                roomNames[index],
                participantCounts[index],
                capacities[index],
                joinableFlags[index] != 0));
        }

        rooms = parsedRooms;
        return true;
    }

    public static bool TryReadRoomChangeResult(DecodedPacket packet, out RoomChangeResult result)
    {
        result = new RoomChangeResult(0, 0, false, RoomFlowResultCode.InternalError);
        if (packet.Opcode != RoomChangeRpOpcode)
        {
            return false;
        }

        var reader = new PacketBinaryReader(packet.Body);
        if (!reader.TryReadUInt32(out uint previousRoomId) ||
            !reader.TryReadUInt32(out uint currentRoomId) ||
            !reader.TryReadBool(out bool success) ||
            !reader.TryReadUInt16(out ushort resultCode) ||
            !reader.IsAtEnd)
        {
            return false;
        }

        result = new RoomChangeResult(
            previousRoomId,
            currentRoomId,
            success,
            Enum.IsDefined(typeof(RoomFlowResultCode), resultCode)
                ? (RoomFlowResultCode)resultCode
                : RoomFlowResultCode.InternalError);
        return true;
    }

    public static bool TryReadChattingResult(DecodedPacket packet, out ChattingResult result)
    {
        result = new ChattingResult(false);
        if (packet.Opcode != ChattingRpOpcode)
        {
            return false;
        }

        var reader = new PacketBinaryReader(packet.Body);
        return reader.TryReadBool(out bool success)
            && reader.IsAtEnd
            && Assign(out result, new ChattingResult(success));
    }

    public static bool TryReadBroadcast(DecodedPacket packet, out BroadcastMessage broadcastMessage)
    {
        broadcastMessage = new BroadcastMessage(0, 0, 0, 0, []);
        if (packet.Opcode != BroadcastOpcode)
        {
            return false;
        }

        var reader = new PacketBinaryReader(packet.Body);
        if (!reader.TryReadUInt32(out uint roomId) ||
            !reader.TryReadUInt64(out ulong senderUserId) ||
            !reader.TryReadUInt64(out ulong messageId) ||
            !reader.TryReadUInt64(out ulong sentTick) ||
            !reader.TryReadByteArray(out byte[] payload) ||
            !reader.IsAtEnd)
        {
            return false;
        }

        broadcastMessage = new BroadcastMessage(roomId, senderUserId, messageId, sentTick, payload);
        return true;
    }

    private static byte[] BuildPacket(ushort opcode, Action<PacketBinaryWriter> writeBody, byte packetKey)
    {
        PacketBinaryWriter payloadWriter = new();
        payloadWriter.WriteUInt16(opcode);
        writeBody(payloadWriter);

        byte[] payload = payloadWriter.ToArray();
        byte randomKey = (byte)Random.Shared.Next(0, byte.MaxValue + 1);
        EncodeInPlace(payload, randomKey, packetKey);
        byte checksum = CalculateChecksum(payload);

        byte[] framedPacket = new byte[TransportHeaderSize + payload.Length];
        BinaryPrimitives.WriteUInt16LittleEndian(framedPacket.AsSpan(0, sizeof(ushort)), (ushort)payload.Length);
        framedPacket[2] = randomKey;
        framedPacket[3] = checksum;
        payload.CopyTo(framedPacket, TransportHeaderSize);
        return framedPacket;
    }

    private static void EncodeInPlace(byte[] buffer, byte randomKey, byte packetKey)
    {
        byte plainState = 0;
        byte encodedState = 0;
        byte randomKeyPlusOne = (byte)(randomKey + 1);
        byte packetKeyPlusOne = (byte)(packetKey + 1);

        for (int index = 0; index < buffer.Length; ++index)
        {
            byte plainValue = buffer[index];
            plainState = (byte)(plainValue ^ (byte)(plainState + randomKeyPlusOne + index));
            encodedState = (byte)(plainState ^ (byte)(encodedState + packetKeyPlusOne + index));
            buffer[index] = encodedState;
        }
    }

    private static void DecodeInPlace(byte[] buffer, byte randomKey, byte packetKey)
    {
        byte previousPlainState = 0;
        byte previousEncodedState = 0;
        byte randomKeyPlusOne = (byte)(randomKey + 1);
        byte packetKeyPlusOne = (byte)(packetKey + 1);

        for (int index = 0; index < buffer.Length; ++index)
        {
            byte encodedValue = buffer[index];
            byte plainState = (byte)(encodedValue ^ (byte)(previousEncodedState + packetKeyPlusOne + index));
            byte decodedValue = (byte)(plainState ^ (byte)(previousPlainState + randomKeyPlusOne + index));
            buffer[index] = decodedValue;
            previousEncodedState = encodedValue;
            previousPlainState = plainState;
        }
    }

    private static byte CalculateChecksum(byte[] buffer)
    {
        uint sum = 0;
        for (int index = 0; index < buffer.Length; ++index)
        {
            sum += buffer[index];
        }

        return (byte)(sum & 0xFF);
    }

    private static bool Assign<T>(out T target, T value)
    {
        target = value;
        return true;
    }
}
