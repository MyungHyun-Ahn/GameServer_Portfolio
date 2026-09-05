namespace ClientNetwork.Transport;

public readonly struct FReceivedPacket
{
    internal FReceivedPacket(
        long connectionGeneration,
        ushort opcode,
        ReadOnlyMemory<byte> body)
    {
        ConnectionGeneration = connectionGeneration;
        Opcode = opcode;
        Body = body;
    }

    public long ConnectionGeneration { get; }
    public ushort Opcode { get; }
    public ReadOnlyMemory<byte> Body { get; }
}
