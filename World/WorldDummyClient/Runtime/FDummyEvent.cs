namespace WorldDummyClient.Runtime;

internal enum EDummyEventType : byte
{
    ConnectionChanged,
    ConnectFailed,
    AuthenticationResult,
    AuthenticationFailed,
    MapEnterSendFailed,
    MapEnterResult,
    ActorSpawn,
    ActorDespawn,
    MoveResult,
    MoveNotification
}

internal readonly record struct FDummyEvent(
    FVirtualWorldUser User,
    EDummyEventType Type,
    object? Payload,
    long ReceivedAt);
