namespace WorldDummyClient.Runtime;

internal enum EDummyUserState : byte
{
    Connecting,
    Authenticating,
    EnteringMap,
    Idle,
    Moving,
    Stopping,
    Failed
}
