namespace WorldClientCore.Models;

public enum EWorldMoveState : byte
{
    Start = 1,
    Sync = 2,
    Stop = 3
}

public enum EWorldActorKind : byte
{
    Player = 1,
    Monster = 2
}

public sealed record WorldConnectionSettings(string Host, int Port, byte PacketKey);

public sealed record WorldAuthResult(ushort ResultCode, ulong RequestId, ulong UserId)
{
    public bool Succeeded => ResultCode == 0;
}

public sealed record EquipmentMutationResult(
    ushort ResultCode,
    ulong RequestId,
    ulong ItemInstanceId,
    ulong ItemVersion,
    bool Equipped,
    uint FinalStr,
    uint FinalDex,
    uint FinalIntelligence,
    uint FinalLuk,
    uint CurrentHp,
    uint MaxHp,
    uint CurrentMp,
    uint MaxMp,
    uint Attack,
    uint Defense,
    uint MoveSpeedMilli,
    ulong EquipmentVersion,
    ulong StatRevision)
{
    public bool Succeeded => ResultCode == 0;
}

public sealed record MapEnterResult(
    ushort ResultCode,
    ulong RequestId,
    ulong MapInstanceId,
    ulong EntityId,
    float PositionX,
    float PositionY,
    float DirectionX,
    float DirectionY,
    ulong ServerTick,
    ulong CharacterId,
    uint CharacterDataId,
    uint Level,
    ulong Exp,
    ulong RequiredExpToNextLevel,
    uint Str,
    uint Dex,
    uint Intelligence,
    uint Luk,
    uint UnspentStatPoints,
    ulong ProgressVersion,
    ulong StatVersion,
    uint FinalStr,
    uint FinalDex,
    uint FinalIntelligence,
    uint FinalLuk,
    uint CurrentHp,
    uint MaxHp,
    uint CurrentMp,
    uint MaxMp,
    uint Attack,
    uint Defense,
    uint MoveSpeedMilli,
    ulong EquipmentVersion,
    ulong StatRevision)
{
    public bool Succeeded => ResultCode == 0;
    public bool HasPlayerSnapshot => CharacterId != 0;
}

public sealed record ActorSpawnNotification(
    ulong EntityId,
    EWorldActorKind ActorKind,
    uint ActorDataId,
    float PositionX,
    float PositionY,
    float DirectionX,
    float DirectionY,
    uint MoveSequence,
    EWorldMoveState MoveState,
    ulong ServerTick,
    uint CurrentHp,
    uint MaxHp,
    ulong LifeRevision);

public sealed record ActorDespawnNotification(ulong EntityId);

public sealed record MoveResult(
    ushort ResultCode,
    uint Sequence,
    EWorldMoveState MoveState,
    float AcceptedPositionX,
    float AcceptedPositionY,
    float DirectionX,
    float DirectionY,
    bool IsCorrected)
{
    public bool Succeeded => ResultCode == 0;
}

public sealed record MoveNotification(
    ulong EntityId,
    uint Sequence,
    EWorldMoveState MoveState,
    float PositionX,
    float PositionY,
    float DirectionX,
    float DirectionY,
    ulong ServerTick);

public sealed record BasicAttackResult(
    ushort ResultCode,
    uint AttackSequence,
    ulong ServerTick)
{
    public bool Succeeded => ResultCode == 0;
}

public sealed record ActorAttackNotification(
    ulong AttackerEntityId,
    ulong TargetEntityId,
    uint Damage,
    uint TargetCurrentHp,
    uint TargetMaxHp,
    ulong ServerTick);

public sealed record ActorDeathNotification(
    ulong EntityId,
    ulong KillerEntityId,
    ulong LifeRevision,
    ulong ServerTick);

public sealed record ActorRespawnNotification(
    ulong EntityId,
    float PositionX,
    float PositionY,
    float DirectionX,
    float DirectionY,
    uint CurrentHp,
    uint MaxHp,
    ulong LifeRevision,
    ulong ServerTick);
