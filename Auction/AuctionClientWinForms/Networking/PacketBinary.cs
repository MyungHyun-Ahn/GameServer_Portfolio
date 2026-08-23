using System.Buffers.Binary;
using System.Text;

namespace AuctionClientWinForms.Networking;

internal sealed class PacketBinaryWriter
{
    private readonly List<byte> m_buffer = [];

    public void WriteByte(byte value) => m_buffer.Add(value);

    public void WriteUInt16(ushort value)
    {
        Span<byte> bytes = stackalloc byte[sizeof(ushort)];
        BinaryPrimitives.WriteUInt16LittleEndian(bytes, value);
        WriteRaw(bytes);
    }

    public void WriteUInt32(uint value)
    {
        Span<byte> bytes = stackalloc byte[sizeof(uint)];
        BinaryPrimitives.WriteUInt32LittleEndian(bytes, value);
        WriteRaw(bytes);
    }

    public void WriteUInt64(ulong value)
    {
        Span<byte> bytes = stackalloc byte[sizeof(ulong)];
        BinaryPrimitives.WriteUInt64LittleEndian(bytes, value);
        WriteRaw(bytes);
    }

    public void WriteString(string value)
    {
        byte[] bytes = Encoding.UTF8.GetBytes(value);
        WriteUInt32((uint)bytes.Length);
        WriteRaw(bytes);
    }

    public void WriteUInt32List(IReadOnlyList<uint> values)
    {
        WriteUInt32((uint)values.Count);
        foreach (uint value in values)
        {
            WriteUInt32(value);
        }
    }

    public void WriteRaw(ReadOnlySpan<byte> bytes)
    {
        foreach (byte value in bytes)
        {
            m_buffer.Add(value);
        }
    }

    public byte[] ToArray() => [.. m_buffer];
}

internal ref struct PacketBinaryReader
{
    private readonly ReadOnlySpan<byte> m_buffer;
    private int m_offset;

    public PacketBinaryReader(ReadOnlySpan<byte> buffer)
    {
        m_buffer = buffer;
        m_offset = 0;
    }

    public bool IsAtEnd => m_offset == m_buffer.Length;

    public bool TryReadByte(out byte value)
    {
        if (!CanRead(sizeof(byte)))
        {
            value = 0;
            return false;
        }

        value = m_buffer[m_offset++];
        return true;
    }

    public bool TryReadUInt16(out ushort value)
    {
        if (!CanRead(sizeof(ushort)))
        {
            value = 0;
            return false;
        }

        value = BinaryPrimitives.ReadUInt16LittleEndian(m_buffer.Slice(m_offset, sizeof(ushort)));
        m_offset += sizeof(ushort);
        return true;
    }

    public bool TryReadUInt32(out uint value)
    {
        if (!CanRead(sizeof(uint)))
        {
            value = 0;
            return false;
        }

        value = BinaryPrimitives.ReadUInt32LittleEndian(m_buffer.Slice(m_offset, sizeof(uint)));
        m_offset += sizeof(uint);
        return true;
    }

    public bool TryReadUInt64(out ulong value)
    {
        if (!CanRead(sizeof(ulong)))
        {
            value = 0;
            return false;
        }

        value = BinaryPrimitives.ReadUInt64LittleEndian(m_buffer.Slice(m_offset, sizeof(ulong)));
        m_offset += sizeof(ulong);
        return true;
    }

    public bool TryReadString(out string value)
    {
        if (!TryReadUInt32(out uint length) || length > int.MaxValue || !CanRead((int)length))
        {
            value = string.Empty;
            return false;
        }

        value = Encoding.UTF8.GetString(m_buffer.Slice(m_offset, (int)length));
        m_offset += (int)length;
        return true;
    }

    public bool TryReadByteList(out List<byte> values)
    {
        values = [];
        if (!TryReadCount(out int count) || !CanRead(count))
        {
            return false;
        }

        values = m_buffer.Slice(m_offset, count).ToArray().ToList();
        m_offset += count;
        return true;
    }

    public bool TryReadUInt16List(out List<ushort> values)
    {
        values = [];
        if (!TryReadCount(out int count))
        {
            return false;
        }

        values.Capacity = count;
        for (int index = 0; index < count; ++index)
        {
            if (!TryReadUInt16(out ushort value))
            {
                return false;
            }
            values.Add(value);
        }
        return true;
    }

    public bool TryReadUInt32List(out List<uint> values)
    {
        values = [];
        if (!TryReadCount(out int count))
        {
            return false;
        }

        values.Capacity = count;
        for (int index = 0; index < count; ++index)
        {
            if (!TryReadUInt32(out uint value))
            {
                return false;
            }
            values.Add(value);
        }
        return true;
    }

    public bool TryReadUInt64List(out List<ulong> values)
    {
        values = [];
        if (!TryReadCount(out int count))
        {
            return false;
        }

        values.Capacity = count;
        for (int index = 0; index < count; ++index)
        {
            if (!TryReadUInt64(out ulong value))
            {
                return false;
            }
            values.Add(value);
        }
        return true;
    }

    public bool TryReadStringList(out List<string> values)
    {
        values = [];
        if (!TryReadCount(out int count))
        {
            return false;
        }

        values.Capacity = count;
        for (int index = 0; index < count; ++index)
        {
            if (!TryReadString(out string value))
            {
                return false;
            }
            values.Add(value);
        }
        return true;
    }

    private bool TryReadCount(out int count)
    {
        count = 0;
        if (!TryReadUInt32(out uint rawCount) || rawCount > int.MaxValue)
        {
            return false;
        }

        count = (int)rawCount;
        return true;
    }

    private bool CanRead(int size) => size >= 0 && m_offset <= m_buffer.Length - size;
}
