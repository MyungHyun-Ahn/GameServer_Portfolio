using System.Security.Cryptography;
using System.Text;

namespace ChattingClientWinForms.Models;

internal sealed record ClientConnectionSettings(string Host, int Port, byte PacketKey);

internal sealed record ChatRoomInfo(
    uint RoomId,
    string RoomName,
    uint ParticipantCount,
    uint Capacity,
    bool Joinable);

internal sealed record LoginResult(uint UserId, bool Success);

internal enum RoomFlowResultCode : ushort
{
    Success = 0,
    InvalidRoomId = 1,
    SameRoomNotAllowed = 2,
    RoomFull = 3,
    RetryRequired = 4,
    MissingContentInstance = 100,
    RuntimeRouteFailure = 101,
    InternalError = 102
}

internal sealed record RoomChangeResult(
    uint PreviousRoomId,
    uint CurrentRoomId,
    bool Success,
    RoomFlowResultCode ResultCode);

internal sealed record ChattingResult(bool Success);

internal sealed record BroadcastMessage(
    uint RoomId,
    ulong SenderUserId,
    ulong MessageId,
    ulong SentTick,
    byte[] Payload)
{
    public string PayloadText => Encoding.UTF8.GetString(Payload);
}

internal static class TemporaryUserIdFactory
{
    public static uint Create(string loginId, string password)
    {
        string normalizedLoginId = loginId.Trim();
        string normalizedPassword = password.Trim();

        if (string.IsNullOrEmpty(normalizedLoginId) && string.IsNullOrEmpty(normalizedPassword))
        {
            return (uint)RandomNumberGenerator.GetInt32(1, int.MaxValue);
        }

        byte[] sourceBytes = Encoding.UTF8.GetBytes($"{normalizedLoginId}|{normalizedPassword}");
        unchecked
        {
            uint hash = 2166136261;
            foreach (byte sourceByte in sourceBytes)
            {
                hash ^= sourceByte;
                hash *= 16777619;
            }

            return hash == 0 ? 1u : hash;
        }
    }
}
