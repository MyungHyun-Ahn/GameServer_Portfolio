using System.Buffers.Binary;
using System.Text;

namespace ChattingClientWinForms.Networking;

internal sealed class PacketBinaryWriter
{
    private readonly List<byte> m_buffer = [];

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

    public void WriteBool(bool value)
    {
        m_buffer.Add(value ? (byte)1 : (byte)0);
    }

    public void WriteString(string value)
    {
        byte[] textBytes = Encoding.UTF8.GetBytes(value);
        WriteUInt32((uint)textBytes.Length);
        WriteRaw(textBytes);
    }

    public void WriteBytes(byte[] value)
    {
        WriteUInt32((uint)value.Length);
        WriteRaw(value);
    }

    public void WriteRaw(ReadOnlySpan<byte> bytes)
    {
        for (int index = 0; index < bytes.Length; ++index)
        {
            m_buffer.Add(bytes[index]);
        }
    }

    public byte[] ToArray()
    {
        return [.. m_buffer];
    }
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

        value = m_buffer[m_offset];
        m_offset += sizeof(byte);
        return true;
    }

    public bool TryReadBool(out bool value)
    {
        if (!TryReadByte(out byte byteValue))
        {
            value = false;
            return false;
        }

        value = byteValue != 0;
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
        if (!TryReadUInt32(out uint length) || !CanRead((int)length))
        {
            value = string.Empty;
            return false;
        }

        value = Encoding.UTF8.GetString(m_buffer.Slice(m_offset, (int)length));
        m_offset += (int)length;
        return true;
    }

    public bool TryReadByteArray(out byte[] value)
    {
        if (!TryReadUInt32(out uint length) || !CanRead((int)length))
        {
            value = [];
            return false;
        }

        value = m_buffer.Slice(m_offset, (int)length).ToArray();
        m_offset += (int)length;
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

    public bool TryReadByteList(out List<byte> values)
    {
        values = [];
        if (!TryReadCount(out int count) || !CanRead(count))
        {
            return false;
        }

        values.Capacity = count;
        for (int index = 0; index < count; ++index)
        {
            values.Add(m_buffer[m_offset + index]);
        }

        m_offset += count;
        return true;
    }

    private bool TryReadCount(out int count)
    {
        count = 0;
        if (!TryReadUInt32(out uint rawCount))
        {
            return false;
        }

        if (rawCount > int.MaxValue)
        {
            return false;
        }

        count = (int)rawCount;
        return true;
    }

    private bool CanRead(int size)
    {
        return size >= 0 && m_offset + size <= m_buffer.Length;
    }
}
