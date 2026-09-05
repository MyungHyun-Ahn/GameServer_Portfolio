using WorldClientCore.Models;

namespace WorldClientCore.Simulation;

public readonly record struct WorldActorSnapshot(
    ulong EntityId,
    EWorldActorKind ActorKind,
    uint ActorDataId,
    bool IsLocal,
    float PositionX,
    float PositionY,
    float DirectionX,
    float DirectionY,
    EWorldMoveState MoveState,
    uint Sequence,
    float AnimationTime,
    uint CurrentHp,
    uint MaxHp,
    bool IsDead,
    ulong LifeRevision);
