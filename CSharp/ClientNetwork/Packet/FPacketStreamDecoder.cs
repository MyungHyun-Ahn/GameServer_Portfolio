using System.Buffers.Binary;

namespace ClientNetwork.Packet;

public enum EFrameReadStatus
{
    NeedMoreData,
    FrameReady,
    InvalidData
}

public enum EFrameDecodeError
{
    None,
    BufferLimitExceeded,
    InvalidPayloadLength,
    ChecksumMismatch
}

public sealed class FDecodedContentFrame
{
    private readonly byte[] m_contentPayload;

    internal FDecodedContentFrame(
        byte randomKey,
        ushort opcode,
        byte[] contentPayload)
    {
        RandomKey = randomKey;
        Opcode = opcode;
        m_contentPayload = contentPayload;
    }

    public byte RandomKey { get; }
    public ushort Opcode { get; }
    public ReadOnlyMemory<byte> Body => m_contentPayload.AsMemory(FPacketProtocol.ContentHeaderSize);
}

public sealed class FPacketStreamDecoder
{
    private readonly FDefaultPacketCipher m_cipher;
    private readonly bool m_validateChecksum;
    private readonly int m_maxBufferedBytes;

    private byte[] m_buffer;
    private int m_readOffset;
    private int m_writeOffset;
    private bool m_isFaulted;
    private EFrameDecodeError m_lastError;

    public FPacketStreamDecoder(
        FDefaultPacketCipher? cipher = null,
        bool validateChecksum = true,
        int maxBufferedBytes = FPacketProtocol.DefaultMaxBufferedBytes)
    {
        if (maxBufferedBytes < FPacketProtocol.MaxFrameSize)
        {
            throw new ArgumentOutOfRangeException(nameof(maxBufferedBytes),
                $"The stream buffer must hold at least one {FPacketProtocol.MaxFrameSize}-byte frame.");
        }

        m_cipher = cipher ?? new FDefaultPacketCipher();
        m_validateChecksum = validateChecksum;
        m_maxBufferedBytes = maxBufferedBytes;
        m_buffer = new byte[FPacketProtocol.MaxFrameSize];
    }

    public int BufferedByteCount => m_writeOffset - m_readOffset;
    public bool IsFaulted => m_isFaulted;
    public EFrameDecodeError LastError => m_lastError;

    public bool TryAppend(ReadOnlySpan<byte> bytes)
    {
        if (m_isFaulted)
        {
            return false;
        }

        if (bytes.IsEmpty)
        {
            return true;
        }

        int bufferedByteCount = BufferedByteCount;
        if (bytes.Length > m_maxBufferedBytes - bufferedByteCount)
        {
            SetFault(EFrameDecodeError.BufferLimitExceeded);
            return false;
        }

        EnsureWritable(bytes.Length);
        bytes.CopyTo(m_buffer.AsSpan(m_writeOffset));
        m_writeOffset += bytes.Length;
        return true;
    }

    public EFrameReadStatus TryReadFrame(
        out FDecodedContentFrame? frame,
        out EFrameDecodeError error)
    {
        frame = null;
        error = EFrameDecodeError.None;

        if (m_isFaulted)
        {
            error = m_lastError;
            return EFrameReadStatus.InvalidData;
        }

        if (BufferedByteCount < FPacketProtocol.TransportHeaderSize)
        {
            return EFrameReadStatus.NeedMoreData;
        }

        ReadOnlySpan<byte> transportHeader = m_buffer.AsSpan(m_readOffset, FPacketProtocol.TransportHeaderSize);
        int payloadLength = BinaryPrimitives.ReadUInt16LittleEndian(transportHeader);
        int frameLength = FPacketProtocol.TransportHeaderSize + payloadLength;
        if (payloadLength < FPacketProtocol.ContentHeaderSize || frameLength > FPacketProtocol.MaxFrameSize)
        {
            SetFault(EFrameDecodeError.InvalidPayloadLength);
            error = m_lastError;
            return EFrameReadStatus.InvalidData;
        }

        if (BufferedByteCount < frameLength)
        {
            return EFrameReadStatus.NeedMoreData;
        }

        byte randomKey = transportHeader[2];
        byte expectedChecksum = transportHeader[3];
        ReadOnlySpan<byte> encodedPayload =
            m_buffer.AsSpan(m_readOffset + FPacketProtocol.TransportHeaderSize, payloadLength);
        if (m_validateChecksum && m_cipher.CalculateChecksum(encodedPayload) != expectedChecksum)
        {
            SetFault(EFrameDecodeError.ChecksumMismatch);
            error = m_lastError;
            return EFrameReadStatus.InvalidData;
        }

        byte[] contentPayload = encodedPayload.ToArray();
        m_cipher.Decode(contentPayload, randomKey);
        ushort opcode = BinaryPrimitives.ReadUInt16LittleEndian(contentPayload);

        Consume(frameLength);
        frame = new FDecodedContentFrame(randomKey, opcode, contentPayload);
        return EFrameReadStatus.FrameReady;
    }

    public void Reset()
    {
        m_readOffset = 0;
        m_writeOffset = 0;
        m_isFaulted = false;
        m_lastError = EFrameDecodeError.None;
    }

    private void EnsureWritable(int length)
    {
        if (length <= m_buffer.Length - m_writeOffset)
        {
            return;
        }

        int bufferedByteCount = BufferedByteCount;
        if (m_readOffset > 0 && length <= m_buffer.Length - bufferedByteCount)
        {
            m_buffer.AsSpan(m_readOffset, bufferedByteCount).CopyTo(m_buffer);
            m_readOffset = 0;
            m_writeOffset = bufferedByteCount;
            return;
        }

        int requiredLength = bufferedByteCount + length;
        int doubledLength = m_buffer.Length > m_maxBufferedBytes / 2
            ? m_maxBufferedBytes
            : m_buffer.Length * 2;
        int newLength = Math.Min(m_maxBufferedBytes, Math.Max(requiredLength, doubledLength));
        byte[] newBuffer = new byte[newLength];
        m_buffer.AsSpan(m_readOffset, bufferedByteCount).CopyTo(newBuffer);
        m_buffer = newBuffer;
        m_readOffset = 0;
        m_writeOffset = bufferedByteCount;
    }

    private void Consume(int length)
    {
        m_readOffset += length;
        if (m_readOffset == m_writeOffset)
        {
            m_readOffset = 0;
            m_writeOffset = 0;
        }
    }

    private void SetFault(EFrameDecodeError error)
    {
        m_isFaulted = true;
        m_lastError = error;
    }
}
