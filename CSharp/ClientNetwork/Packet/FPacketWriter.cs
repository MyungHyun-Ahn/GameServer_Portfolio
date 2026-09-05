using System.Buffers.Binary;
using System.Text;

namespace ClientNetwork.Packet;

public sealed class FPacketWriter
{
    private static readonly UTF8Encoding s_utf8 = new(false, true);

    private byte[] m_buffer;
    private readonly int m_maxLength;
    private int m_length;

    public FPacketWriter(
        int initialCapacity = 256,
        int maxLength = FPacketProtocol.MaxContentBodySize)
    {
        if (initialCapacity < 0)
        {
            throw new ArgumentOutOfRangeException(nameof(initialCapacity));
        }

        if (maxLength < 0 || initialCapacity > maxLength)
        {
            throw new ArgumentOutOfRangeException(nameof(maxLength));
        }

        m_buffer = initialCapacity == 0 ? [] : new byte[initialCapacity];
        m_maxLength = maxLength;
    }

    public int Length => m_length;
    public int RemainingCapacity => m_maxLength - m_length;
    public ReadOnlySpan<byte> WrittenSpan => m_buffer.AsSpan(0, m_length);
    public ReadOnlyMemory<byte> WrittenMemory => m_buffer.AsMemory(0, m_length);

    public void Reset() => m_length = 0;

    public byte[] ToArray() => WrittenSpan.ToArray();

    public void WriteBool(bool value) => WriteUInt8(value ? (byte)1 : (byte)0);

    public void WriteInt8(sbyte value) => WriteUInt8(unchecked((byte)value));

    public void WriteUInt8(byte value)
    {
        Span<byte> destination = GetDestination(sizeof(byte));
        destination[0] = value;
    }

    public void WriteByte(byte value) => WriteUInt8(value);

    public void WriteSByte(sbyte value) => WriteInt8(value);

    public void WriteInt16(short value)
    {
        BinaryPrimitives.WriteInt16LittleEndian(GetDestination(sizeof(short)), value);
    }

    public void WriteUInt16(ushort value)
    {
        BinaryPrimitives.WriteUInt16LittleEndian(GetDestination(sizeof(ushort)), value);
    }

    public void WriteInt32(int value)
    {
        BinaryPrimitives.WriteInt32LittleEndian(GetDestination(sizeof(int)), value);
    }

    public void WriteUInt32(uint value)
    {
        BinaryPrimitives.WriteUInt32LittleEndian(GetDestination(sizeof(uint)), value);
    }

    public void WriteInt64(long value)
    {
        BinaryPrimitives.WriteInt64LittleEndian(GetDestination(sizeof(long)), value);
    }

    public void WriteUInt64(ulong value)
    {
        BinaryPrimitives.WriteUInt64LittleEndian(GetDestination(sizeof(ulong)), value);
    }

    public void WriteFloat(float value)
    {
        BinaryPrimitives.WriteInt32LittleEndian(GetDestination(sizeof(float)), BitConverter.SingleToInt32Bits(value));
    }

    public void WriteSingle(float value) => WriteFloat(value);

    public void WriteDouble(double value)
    {
        BinaryPrimitives.WriteInt64LittleEndian(GetDestination(sizeof(double)), BitConverter.DoubleToInt64Bits(value));
    }

    public void WriteCount(int count)
    {
        if (count < 0)
        {
            throw new ArgumentOutOfRangeException(nameof(count));
        }

        WriteUInt32((uint)count);
    }

    public void WriteCount(uint count) => WriteUInt32(count);

    public void WriteString(string value)
    {
        ArgumentNullException.ThrowIfNull(value);

        int byteCount = s_utf8.GetByteCount(value);
        Span<byte> destination = GetLengthPrefixedDestination(byteCount);
        s_utf8.GetBytes(value.AsSpan(), destination);
    }

    public void WriteBytes(ReadOnlySpan<byte> value)
    {
        Span<byte> destination = GetLengthPrefixedDestination(value.Length);
        value.CopyTo(destination);
    }

    public void WriteRaw(ReadOnlySpan<byte> value)
    {
        if (value.IsEmpty)
        {
            return;
        }

        value.CopyTo(GetDestination(value.Length));
    }

    private Span<byte> GetLengthPrefixedDestination(int length)
    {
        int totalLength;
        try
        {
            totalLength = checked(sizeof(uint) + length);
        }
        catch (OverflowException exception)
        {
            throw new InvalidOperationException("The length-prefixed value exceeds the packet size limit.", exception);
        }

        Span<byte> destination = GetDestination(totalLength);
        BinaryPrimitives.WriteUInt32LittleEndian(destination, (uint)length);
        return destination.Slice(sizeof(uint), length);
    }

    private Span<byte> GetDestination(int length)
    {
        EnsureCanWrite(length);
        Span<byte> destination = m_buffer.AsSpan(m_length, length);
        m_length += length;
        return destination;
    }

    private void EnsureCanWrite(int length)
    {
        if (length < 0 || length > m_maxLength - m_length)
        {
            throw new InvalidOperationException($"The packet body cannot exceed {m_maxLength} bytes.");
        }

        int requiredLength = m_length + length;
        if (requiredLength <= m_buffer.Length)
        {
            return;
        }

        int doubledLength = m_buffer.Length == 0 ? 16 : m_buffer.Length * 2;
        int newLength = Math.Min(m_maxLength, Math.Max(requiredLength, doubledLength));
        Array.Resize(ref m_buffer, newLength);
    }
}
