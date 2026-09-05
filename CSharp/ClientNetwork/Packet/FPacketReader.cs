using System.Buffers.Binary;
using System.Text;

namespace ClientNetwork.Packet;

public ref struct FPacketReader
{
    private static readonly UTF8Encoding s_utf8 = new(false, true);

    private readonly ReadOnlySpan<byte> m_buffer;
    private int m_offset;

    public FPacketReader(ReadOnlySpan<byte> buffer)
    {
        if (buffer.Length > FPacketProtocol.MaxContentBodySize)
        {
            throw new ArgumentOutOfRangeException(nameof(buffer),
                $"The packet body cannot exceed {FPacketProtocol.MaxContentBodySize} bytes.");
        }

        m_buffer = buffer;
        m_offset = 0;
    }

    public readonly int Offset => m_offset;
    public readonly int Remaining => m_buffer.Length - m_offset;
    public readonly bool IsAtEnd => m_offset == m_buffer.Length;

    public bool TryReadBool(out bool value)
    {
        value = false;
        if (Remaining < sizeof(byte))
        {
            return false;
        }

        byte encodedValue = m_buffer[m_offset];
        if (encodedValue > 1)
        {
            return false;
        }

        ++m_offset;
        value = encodedValue != 0;
        return true;
    }

    public bool TryReadInt8(out sbyte value)
    {
        if (!TryReadUInt8(out byte unsignedValue))
        {
            value = 0;
            return false;
        }

        value = unchecked((sbyte)unsignedValue);
        return true;
    }

    public bool TryReadUInt8(out byte value)
    {
        if (!TryTake(sizeof(byte), out ReadOnlySpan<byte> bytes))
        {
            value = 0;
            return false;
        }

        value = bytes[0];
        return true;
    }

    public bool TryReadByte(out byte value) => TryReadUInt8(out value);

    public bool TryReadSByte(out sbyte value) => TryReadInt8(out value);

    public bool TryReadInt16(out short value)
    {
        if (!TryTake(sizeof(short), out ReadOnlySpan<byte> bytes))
        {
            value = 0;
            return false;
        }

        value = BinaryPrimitives.ReadInt16LittleEndian(bytes);
        return true;
    }

    public bool TryReadUInt16(out ushort value)
    {
        if (!TryTake(sizeof(ushort), out ReadOnlySpan<byte> bytes))
        {
            value = 0;
            return false;
        }

        value = BinaryPrimitives.ReadUInt16LittleEndian(bytes);
        return true;
    }

    public bool TryReadInt32(out int value)
    {
        if (!TryTake(sizeof(int), out ReadOnlySpan<byte> bytes))
        {
            value = 0;
            return false;
        }

        value = BinaryPrimitives.ReadInt32LittleEndian(bytes);
        return true;
    }

    public bool TryReadUInt32(out uint value)
    {
        if (!TryTake(sizeof(uint), out ReadOnlySpan<byte> bytes))
        {
            value = 0;
            return false;
        }

        value = BinaryPrimitives.ReadUInt32LittleEndian(bytes);
        return true;
    }

    public bool TryReadInt64(out long value)
    {
        if (!TryTake(sizeof(long), out ReadOnlySpan<byte> bytes))
        {
            value = 0;
            return false;
        }

        value = BinaryPrimitives.ReadInt64LittleEndian(bytes);
        return true;
    }

    public bool TryReadUInt64(out ulong value)
    {
        if (!TryTake(sizeof(ulong), out ReadOnlySpan<byte> bytes))
        {
            value = 0;
            return false;
        }

        value = BinaryPrimitives.ReadUInt64LittleEndian(bytes);
        return true;
    }

    public bool TryReadFloat(out float value)
    {
        if (!TryReadInt32(out int encodedValue))
        {
            value = 0;
            return false;
        }

        value = BitConverter.Int32BitsToSingle(encodedValue);
        return true;
    }

    public bool TryReadSingle(out float value) => TryReadFloat(out value);

    public bool TryReadDouble(out double value)
    {
        if (!TryReadInt64(out long encodedValue))
        {
            value = 0;
            return false;
        }

        value = BitConverter.Int64BitsToDouble(encodedValue);
        return true;
    }

    public bool TryReadCount(out uint count)
    {
        int originalOffset = m_offset;
        if (!TryReadUInt32(out count) || count > FPacketProtocol.MaxContentBodySize)
        {
            m_offset = originalOffset;
            count = 0;
            return false;
        }

        return true;
    }

    public bool TryReadCount(out uint count, int minimumEncodedElementSize)
    {
        int originalOffset = m_offset;
        if (minimumEncodedElementSize <= 0 ||
            !TryReadCount(out count) ||
            count > (uint)(Remaining / minimumEncodedElementSize))
        {
            m_offset = originalOffset;
            count = 0;
            return false;
        }

        return true;
    }

    public bool TryReadString(out string value)
    {
        value = string.Empty;
        int originalOffset = m_offset;
        if (!TryReadLength(out int length) || !TryTake(length, out ReadOnlySpan<byte> bytes))
        {
            m_offset = originalOffset;
            return false;
        }

        try
        {
            value = s_utf8.GetString(bytes);
            return true;
        }
        catch (DecoderFallbackException)
        {
            m_offset = originalOffset;
            value = string.Empty;
            return false;
        }
    }

    public bool TryReadBytes(out byte[] value)
    {
        value = [];
        int originalOffset = m_offset;
        if (!TryReadLength(out int length) || !TryTake(length, out ReadOnlySpan<byte> bytes))
        {
            m_offset = originalOffset;
            return false;
        }

        value = bytes.ToArray();
        return true;
    }

    public bool TryReadBytesView(out ReadOnlySpan<byte> value)
    {
        int originalOffset = m_offset;
        if (!TryReadLength(out int length) || !TryTake(length, out value))
        {
            m_offset = originalOffset;
            value = default;
            return false;
        }

        return true;
    }

    public bool TryReadRaw(int length, out ReadOnlySpan<byte> value) => TryTake(length, out value);

    public readonly bool CanRead(int length) => length >= 0 && length <= Remaining;

    public readonly bool CanReadElements(uint count, int minimumEncodedElementSize)
    {
        return minimumEncodedElementSize > 0 && count <= (uint)(Remaining / minimumEncodedElementSize);
    }

    public bool TrySkip(int length)
    {
        if (!CanRead(length))
        {
            return false;
        }

        m_offset += length;
        return true;
    }

    private bool TryReadLength(out int length)
    {
        length = 0;
        if (!TryReadUInt32(out uint encodedLength) || encodedLength > int.MaxValue)
        {
            return false;
        }

        length = (int)encodedLength;
        return true;
    }

    private bool TryTake(int length, out ReadOnlySpan<byte> value)
    {
        if (!CanRead(length))
        {
            value = default;
            return false;
        }

        value = m_buffer.Slice(m_offset, length);
        m_offset += length;
        return true;
    }
}
