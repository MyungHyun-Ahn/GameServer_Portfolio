namespace ClientNetwork.Packet;

public static class FPacketProtocol
{
    public const int TransportHeaderSize = sizeof(ushort) + sizeof(byte) + sizeof(byte);
    public const int ContentHeaderSize = sizeof(ushort);
    public const int MaxFrameSize = 8192;
    public const int MaxContentPayloadSize = MaxFrameSize - TransportHeaderSize;
    public const int MaxContentBodySize = MaxContentPayloadSize - ContentHeaderSize;
    public const int DefaultMaxBufferedBytes = MaxFrameSize * 8;
}
