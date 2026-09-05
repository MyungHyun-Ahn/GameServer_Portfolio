namespace ClientNetwork.Transport;

public enum EClientDisconnectReason
{
    LocalRequest,
    RemoteClosed,
    ConnectFailed,
    SendFailed,
    ReceiveFailed,
    InvalidFrame,
    ReceiveQueueOverflow,
    Disposed
}

public sealed class FClientDisconnectInfo
{
    public FClientDisconnectInfo(
        long connectionGeneration,
        EClientDisconnectReason reason,
        string message,
        Exception? exception = null)
    {
        ConnectionGeneration = connectionGeneration;
        Reason = reason;
        Message = message;
        Exception = exception;
    }

    public long ConnectionGeneration { get; }
    public EClientDisconnectReason Reason { get; }
    public string Message { get; }
    public Exception? Exception { get; }
}
