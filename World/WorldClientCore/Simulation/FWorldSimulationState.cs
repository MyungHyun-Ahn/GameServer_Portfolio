using WorldClientCore.Models;

namespace WorldClientCore.Simulation;

// Not thread-safe by design. The UI frame or dummy frame that owns this object
// is the only writer; network callbacks must be delivered to that owner first.
public sealed class FWorldSimulationState
{
    private readonly Dictionary<ulong, ActorState> m_actors = [];
    private readonly WorldSimulationOptions m_options;

    public FWorldSimulationState(WorldSimulationOptions? options = null)
    {
        m_options = options ?? new WorldSimulationOptions();
        m_options.Validate();
    }

    public ulong LocalEntityId { get; private set; }
    public ulong MapInstanceId { get; private set; }
    public MapEnterResult? LocalPlayerInitialization { get; private set; }
    public float LocalMoveSpeed { get; private set; }
    public uint LocalCurrentHp => TryGetLocalActor(out ActorState actor) ? actor.CurrentHp : 0;
    public uint LocalMaxHp => TryGetLocalActor(out ActorState actor) ? actor.MaxHp : 0;
    public bool LocalIsDead => TryGetLocalActor(out ActorState actor) && actor.IsDead;
    public ulong LocalLifeRevision => TryGetLocalActor(out ActorState actor) ? actor.LifeRevision : 0;
    public int ActorCount => m_actors.Count;
    public float WorldWidth => m_options.WorldWidth;
    public float WorldHeight => m_options.WorldHeight;

    public void EnterLocal(MapEnterResult result)
    {
        ArgumentNullException.ThrowIfNull(result);
        m_actors.Clear();
        MapInstanceId = result.MapInstanceId;
        LocalPlayerInitialization = result;
        LocalEntityId = result.EntityId;
        LocalMoveSpeed = ToMoveSpeed(result.MoveSpeedMilli);
        m_actors[result.EntityId] = new ActorState(
            result.EntityId,
            EWorldActorKind.Player,
            result.CharacterDataId,
            result.PositionX,
            result.PositionY,
            result.DirectionX,
            result.DirectionY,
            EWorldMoveState.Stop,
            0,
            result.ServerTick,
            true,
            result.CurrentHp,
            result.MaxHp,
            1);
    }

    public void Clear()
    {
        m_actors.Clear();
        LocalEntityId = 0;
        MapInstanceId = 0;
        LocalPlayerInitialization = null;
        LocalMoveSpeed = 0.0f;
    }

    public void Spawn(ActorSpawnNotification notification)
    {
        ArgumentNullException.ThrowIfNull(notification);
        if (notification.EntityId == LocalEntityId || notification.LifeRevision == 0)
        {
            return;
        }

        m_actors[notification.EntityId] = new ActorState(
            notification.EntityId,
            notification.ActorKind,
            notification.ActorDataId,
            notification.PositionX,
            notification.PositionY,
            notification.DirectionX,
            notification.DirectionY,
            notification.MoveState,
            notification.MoveSequence,
            notification.ServerTick,
            false,
            notification.CurrentHp,
            notification.MaxHp,
            notification.LifeRevision);
    }

    public bool Despawn(ulong entityId) => m_actors.Remove(entityId);

    public bool ApplyMoveResult(MoveResult result)
    {
        ArgumentNullException.ThrowIfNull(result);
        if (!m_actors.TryGetValue(LocalEntityId, out ActorState? actor) ||
            actor.IsDead ||
            result.Sequence < actor.Sequence)
        {
            return false;
        }

        actor.Sequence = result.Sequence;
        if (!result.Succeeded)
        {
            actor.MoveState = EWorldMoveState.Stop;
            if (result.IsCorrected)
            {
                ApplyAcceptedState(actor, result);
            }
            return true;
        }

        actor.MoveState = result.MoveState;
        actor.DirectionX = result.DirectionX;
        actor.DirectionY = result.DirectionY;
        if (result.IsCorrected || result.MoveState == EWorldMoveState.Stop)
        {
            actor.PositionX = result.AcceptedPositionX;
            actor.PositionY = result.AcceptedPositionY;
        }
        return true;
    }

    public bool ApplyMoveNotification(MoveNotification notification)
    {
        ArgumentNullException.ThrowIfNull(notification);
        if (notification.EntityId == LocalEntityId)
        {
            return false;
        }

        if (!m_actors.TryGetValue(notification.EntityId, out ActorState? actor))
        {
            return false;
        }

        if (actor.IsDead)
        {
            return false;
        }

        if (actor.ActorKind == EWorldActorKind.Monster)
        {
            if (notification.ServerTick < actor.LastServerTick)
            {
                return false;
            }
            actor.LastServerTick = notification.ServerTick;
        }
        else if (notification.Sequence < actor.Sequence)
        {
            return false;
        }

        actor.PositionX = notification.PositionX;
        actor.PositionY = notification.PositionY;
        actor.DirectionX = notification.DirectionX;
        actor.DirectionY = notification.DirectionY;
        actor.MoveState = notification.MoveState;
        if (actor.ActorKind != EWorldActorKind.Monster)
        {
            actor.Sequence = notification.Sequence;
        }
        return true;
    }

    public bool SetLocalInput(
        float directionX,
        float directionY,
        EWorldMoveState moveState,
        uint sequence)
    {
        if (!m_actors.TryGetValue(LocalEntityId, out ActorState? actor))
        {
            return false;
        }

        if (actor.IsDead)
        {
            return false;
        }

        actor.DirectionX = directionX;
        actor.DirectionY = directionY;
        actor.MoveState = moveState;
        actor.Sequence = sequence;
        return true;
    }

    public bool SetLocalMoveSpeed(uint moveSpeedMilli)
    {
        if (moveSpeedMilli == 0)
        {
            return false;
        }

        float moveSpeed = moveSpeedMilli / 1000.0f;
        if (!float.IsFinite(moveSpeed) || moveSpeed <= 0.0f)
        {
            return false;
        }

        LocalMoveSpeed = moveSpeed;
        return true;
    }

    public bool SetLocalHealth(uint currentHp, uint maxHp)
    {
        if (maxHp == 0 || currentHp > maxHp || !TryGetLocalActor(out ActorState actor))
        {
            return false;
        }

        actor.CurrentHp = currentHp;
        actor.MaxHp = maxHp;
        return true;
    }

    public bool ApplyAttackNotification(ActorAttackNotification notification)
    {
        ArgumentNullException.ThrowIfNull(notification);
        if (notification.AttackerEntityId == 0 ||
            notification.TargetEntityId == 0 ||
            notification.TargetMaxHp == 0 ||
            notification.TargetCurrentHp > notification.TargetMaxHp ||
            !m_actors.TryGetValue(notification.TargetEntityId, out ActorState? target) ||
            target.IsDead ||
            notification.ServerTick < target.LastCombatServerTick)
        {
            return false;
        }

        target.CurrentHp = notification.TargetCurrentHp;
        target.MaxHp = notification.TargetMaxHp;
        target.LastCombatServerTick = notification.ServerTick;
        return true;
    }

    public bool ApplyDeathNotification(ActorDeathNotification notification)
    {
        ArgumentNullException.ThrowIfNull(notification);
        if (notification.EntityId == 0 ||
            notification.LifeRevision == 0 ||
            !m_actors.TryGetValue(notification.EntityId, out ActorState? actor) ||
            actor.IsDead ||
            notification.LifeRevision != actor.LifeRevision)
        {
            return false;
        }

        actor.CurrentHp = 0;
        actor.MoveState = EWorldMoveState.Stop;
        actor.LifeRevision = notification.LifeRevision;
        actor.IsDead = true;
        actor.LastCombatServerTick = Math.Max(actor.LastCombatServerTick, notification.ServerTick);
        actor.LastServerTick = Math.Max(actor.LastServerTick, notification.ServerTick);
        return true;
    }

    public bool ApplyRespawnNotification(ActorRespawnNotification notification)
    {
        ArgumentNullException.ThrowIfNull(notification);
        if (notification.EntityId == 0 ||
            notification.LifeRevision == 0 ||
            notification.CurrentHp == 0 ||
            notification.MaxHp == 0 ||
            notification.CurrentHp > notification.MaxHp ||
            !float.IsFinite(notification.PositionX) ||
            !float.IsFinite(notification.PositionY) ||
            !float.IsFinite(notification.DirectionX) ||
            !float.IsFinite(notification.DirectionY) ||
            notification.PositionX < 0.0f || notification.PositionX > m_options.WorldWidth ||
            notification.PositionY < 0.0f || notification.PositionY > m_options.WorldHeight ||
            !m_actors.TryGetValue(notification.EntityId, out ActorState? actor) ||
            !actor.IsDead ||
            actor.LifeRevision == ulong.MaxValue ||
            notification.LifeRevision != actor.LifeRevision + 1)
        {
            return false;
        }

        actor.PositionX = notification.PositionX;
        actor.PositionY = notification.PositionY;
        actor.DirectionX = notification.DirectionX;
        actor.DirectionY = notification.DirectionY;
        actor.MoveState = EWorldMoveState.Stop;
        actor.AnimationTime = 0.0f;
        actor.CurrentHp = notification.CurrentHp;
        actor.MaxHp = notification.MaxHp;
        actor.LifeRevision = notification.LifeRevision;
        actor.IsDead = false;
        actor.LastCombatServerTick = Math.Max(actor.LastCombatServerTick, notification.ServerTick);
        actor.LastServerTick = Math.Max(actor.LastServerTick, notification.ServerTick);
        return true;
    }

    public bool Advance(TimeSpan elapsed)
    {
        float seconds = Math.Clamp((float)elapsed.TotalSeconds, 0.0f, 0.1f);
        if (seconds <= 0.0f)
        {
            return false;
        }

        bool changed = false;
        foreach (ActorState actor in m_actors.Values)
        {
            if (actor.IsDead || actor.MoveState == EWorldMoveState.Stop)
            {
                continue;
            }

            if (actor.ActorKind == EWorldActorKind.Monster)
            {
                // Monster positions are server-authoritative. The client advances only
                // the visual animation between authoritative Move notifications.
                actor.AnimationTime += seconds;
                changed = true;
                continue;
            }

            float moveSpeed = actor.IsLocal && LocalMoveSpeed > 0.0f
                ? LocalMoveSpeed
                : m_options.MoveSpeed;
            actor.PositionX = Math.Clamp(
                actor.PositionX + actor.DirectionX * moveSpeed * seconds,
                0.0f,
                m_options.WorldWidth);
            actor.PositionY = Math.Clamp(
                actor.PositionY + actor.DirectionY * moveSpeed * seconds,
                0.0f,
                m_options.WorldHeight);
            actor.AnimationTime += seconds;
            changed = true;
        }
        return changed;
    }

    public bool TryGetLocalSnapshot(out WorldActorSnapshot snapshot)
    {
        if (m_actors.TryGetValue(LocalEntityId, out ActorState? actor))
        {
            snapshot = actor.ToSnapshot();
            return true;
        }

        snapshot = default;
        return false;
    }

    public bool TryGetActorSnapshot(ulong entityId, out WorldActorSnapshot snapshot)
    {
        if (entityId != 0 && m_actors.TryGetValue(entityId, out ActorState? actor))
        {
            snapshot = actor.ToSnapshot();
            return true;
        }

        snapshot = default;
        return false;
    }

    public void CopyActorSnapshots(List<WorldActorSnapshot> destination)
    {
        ArgumentNullException.ThrowIfNull(destination);
        destination.Clear();
        destination.EnsureCapacity(m_actors.Count);
        foreach (ActorState actor in m_actors.Values)
        {
            destination.Add(actor.ToSnapshot());
        }
    }

    private static void ApplyAcceptedState(ActorState actor, MoveResult result)
    {
        actor.PositionX = result.AcceptedPositionX;
        actor.PositionY = result.AcceptedPositionY;
        actor.DirectionX = result.DirectionX;
        actor.DirectionY = result.DirectionY;
    }

    private float ToMoveSpeed(uint moveSpeedMilli)
    {
        return moveSpeedMilli == 0
            ? m_options.MoveSpeed
            : moveSpeedMilli / 1000.0f;
    }

    private bool TryGetLocalActor(out ActorState actor)
    {
        if (m_actors.TryGetValue(LocalEntityId, out ActorState? found))
        {
            actor = found;
            return true;
        }

        actor = null!;
        return false;
    }

    private sealed class ActorState(
        ulong entityId,
        EWorldActorKind actorKind,
        uint actorDataId,
        float positionX,
        float positionY,
        float directionX,
        float directionY,
        EWorldMoveState moveState,
        uint sequence,
        ulong lastServerTick,
        bool isLocal,
        uint currentHp,
        uint maxHp,
        ulong lifeRevision)
    {
        public ulong EntityId { get; } = entityId;
        public EWorldActorKind ActorKind { get; } = actorKind;
        public uint ActorDataId { get; } = actorDataId;
        public bool IsLocal { get; } = isLocal;
        public float PositionX { get; set; } = positionX;
        public float PositionY { get; set; } = positionY;
        public float DirectionX { get; set; } = directionX;
        public float DirectionY { get; set; } = directionY;
        public EWorldMoveState MoveState { get; set; } = moveState;
        public uint Sequence { get; set; } = sequence;
        public ulong LastServerTick { get; set; } = lastServerTick;
        public ulong LastCombatServerTick { get; set; } = lastServerTick;
        public float AnimationTime { get; set; }
        public uint CurrentHp { get; set; } = currentHp;
        public uint MaxHp { get; set; } = maxHp;
        public bool IsDead { get; set; } = currentHp == 0;
        public ulong LifeRevision { get; set; } = lifeRevision;

        public WorldActorSnapshot ToSnapshot() => new(
            EntityId,
            ActorKind,
            ActorDataId,
            IsLocal,
            PositionX,
            PositionY,
            DirectionX,
            DirectionY,
            MoveState,
            Sequence,
            AnimationTime,
            CurrentHp,
            MaxHp,
            IsDead,
            LifeRevision);
    }
}
