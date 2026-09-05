namespace ClientNetwork.Packet;

public enum EContentPacketKind : byte
{
    Request = 0,
    Response = 1,
    Notification = 2,
    Broadcast = 3
}

public interface IContentPacket
{
    ushort Opcode { get; }
    EContentPacketKind PacketKind { get; }

    void SerializeBody(FPacketWriter writer);
}
