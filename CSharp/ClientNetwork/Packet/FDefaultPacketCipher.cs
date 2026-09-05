namespace ClientNetwork.Packet;

public sealed class FDefaultPacketCipher
{
    public FDefaultPacketCipher(
        byte packetKey = 0,
        bool enabled = true)
    {
        PacketKey = packetKey;
        Enabled = enabled;
    }

    public byte PacketKey { get; }
    public bool Enabled { get; }

    public void Encode(
        Span<byte> buffer,
        byte randomKey)
    {
        if (!Enabled || buffer.IsEmpty)
        {
            return;
        }

        byte plainState = 0;
        byte encodedState = 0;
        byte randomKeyPlusOne = unchecked((byte)(randomKey + 1));
        byte packetKeyPlusOne = unchecked((byte)(PacketKey + 1));

        for (int index = 0; index < buffer.Length; ++index)
        {
            byte plainValue = buffer[index];
            plainState = unchecked((byte)(plainValue ^ unchecked((byte)(plainState + randomKeyPlusOne + index))));
            encodedState = unchecked((byte)(plainState ^ unchecked((byte)(encodedState + packetKeyPlusOne + index))));
            buffer[index] = encodedState;
        }
    }

    public void Decode(
        Span<byte> buffer,
        byte randomKey)
    {
        if (!Enabled || buffer.IsEmpty)
        {
            return;
        }

        byte previousPlainState = 0;
        byte previousEncodedState = 0;
        byte randomKeyPlusOne = unchecked((byte)(randomKey + 1));
        byte packetKeyPlusOne = unchecked((byte)(PacketKey + 1));

        for (int index = 0; index < buffer.Length; ++index)
        {
            byte encodedValue = buffer[index];
            byte plainState = unchecked((byte)(encodedValue ^
                unchecked((byte)(previousEncodedState + packetKeyPlusOne + index))));
            byte decodedValue = unchecked((byte)(plainState ^
                unchecked((byte)(previousPlainState + randomKeyPlusOne + index))));
            buffer[index] = decodedValue;
            previousEncodedState = encodedValue;
            previousPlainState = plainState;
        }
    }

    public byte CalculateChecksum(ReadOnlySpan<byte> buffer)
    {
        uint sum = 0;
        foreach (byte value in buffer)
        {
            sum += value;
        }

        return unchecked((byte)sum);
    }
}
