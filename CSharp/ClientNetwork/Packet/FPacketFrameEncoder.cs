using System.Buffers.Binary;

namespace ClientNetwork.Packet;

public sealed class FPacketFrameEncoder
{
    private readonly FDefaultPacketCipher m_cipher;

    public FPacketFrameEncoder(FDefaultPacketCipher? cipher = null)
    {
        m_cipher = cipher ?? new FDefaultPacketCipher();
    }

    public byte[] Encode(IContentPacket packet)
    {
        return Encode(packet, (byte)Random.Shared.Next(byte.MinValue, byte.MaxValue + 1));
    }

    public byte[] Encode(
        IContentPacket packet,
        byte randomKey)
    {
        ArgumentNullException.ThrowIfNull(packet);

        FPacketWriter writer = new();
        packet.SerializeBody(writer);
        return Encode(packet.Opcode, writer.WrittenSpan, randomKey);
    }

    public byte[] Encode(
        ushort opcode,
        ReadOnlySpan<byte> body,
        byte randomKey)
    {
        if (body.Length > FPacketProtocol.MaxContentBodySize)
        {
            throw new ArgumentOutOfRangeException(nameof(body),
                $"The packet body cannot exceed {FPacketProtocol.MaxContentBodySize} bytes.");
        }

        int payloadLength = checked(FPacketProtocol.ContentHeaderSize + body.Length);
        int frameLength = checked(FPacketProtocol.TransportHeaderSize + payloadLength);
        if (frameLength > FPacketProtocol.MaxFrameSize || payloadLength > ushort.MaxValue)
        {
            throw new InvalidOperationException($"The encoded frame cannot exceed {FPacketProtocol.MaxFrameSize} bytes.");
        }

        byte[] frame = new byte[frameLength];
        BinaryPrimitives.WriteUInt16LittleEndian(frame.AsSpan(0, sizeof(ushort)), (ushort)payloadLength);
        frame[2] = randomKey;

        Span<byte> payload = frame.AsSpan(FPacketProtocol.TransportHeaderSize, payloadLength);
        BinaryPrimitives.WriteUInt16LittleEndian(payload, opcode);
        body.CopyTo(payload.Slice(FPacketProtocol.ContentHeaderSize));

        m_cipher.Encode(payload, randomKey);
        frame[3] = m_cipher.CalculateChecksum(payload);
        return frame;
    }
}
