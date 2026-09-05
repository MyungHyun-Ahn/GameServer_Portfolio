using System.Threading.Channels;
using WorldClientCore.Models;
using WorldClientCore.Networking;
using WorldClientWinForms.Configuration;

namespace WorldClientWinForms.SmokeTests;

internal static class ClientSmokeTest
{
    private static readonly TimeSpan s_stepTimeout = TimeSpan.FromSeconds(10.0);

    public static async Task<int> RunAsync(ClientSettings settings, uint mapDataId)
    {
        await using var clientA = new WorldTcpClient();
        await using var clientB = new WorldTcpClient();
        var eventsA = new SmokeEventSink("A", clientA);
        var eventsB = new SmokeEventSink("B", clientB);

        try
        {
            Console.WriteLine($"[WorldClient smoke] connect two independent sessions. mapDataId={mapDataId}");
            await Task.WhenAll(
                clientA.ConnectAsync(CreateConnectionSettings(settings)),
                clientB.ConnectAsync(CreateConnectionSettings(settings)));

            const ulong requestA = 10_001;
            const ulong requestB = 20_001;
            await clientA.SendMapEnterAsync(requestA, mapDataId);
            MapEnterResult enterA = await ReadUntilAsync(
                eventsA.MapEnters.Reader,
                result => result.RequestId == requestA,
                "A MapEnterRp");
            Ensure(enterA.Succeeded, $"A MapEnter failed. result={enterA.ResultCode}");
            ValidateOptionalPlayerSnapshot(enterA, "A");

            await clientB.SendMapEnterAsync(requestB, mapDataId);
            MapEnterResult enterB = await ReadUntilAsync(
                eventsB.MapEnters.Reader,
                result => result.RequestId == requestB,
                "B MapEnterRp");
            Ensure(enterB.Succeeded, $"B MapEnter failed. result={enterB.ResultCode}");
            ValidateOptionalPlayerSnapshot(enterB, "B");
            Ensure(enterA.MapInstanceId == enterB.MapInstanceId, "Clients entered different map instances.");

            ActorSpawnNotification spawnBAtA = await ReadUntilAsync(
                eventsA.Spawns.Reader,
                notification => notification.EntityId == enterB.EntityId,
                "A observes B spawn");
            ActorSpawnNotification spawnAAtB = await ReadUntilAsync(
                eventsB.Spawns.Reader,
                notification => notification.EntityId == enterA.EntityId,
                "B observes A spawn");
            Ensure(spawnBAtA.EntityId == enterB.EntityId && spawnAAtB.EntityId == enterA.EntityId,
                "Mutual spawn entity mismatch.");
            Console.WriteLine($"[PASS] mutual spawn: A={enterA.EntityId}, B={enterB.EntityId}");

            const uint moveSequence = 1;
            bool moveQueued = clientA.TrySendMove(
                moveSequence,
                EWorldMoveState.Start,
                enterA.PositionX + 8.0f,
                enterA.PositionY,
                1.0f,
                0.0f);
            Ensure(moveQueued, "A MoveRq could not be queued.");

            MoveResult moveResult = await ReadUntilAsync(
                eventsA.MoveResults.Reader,
                result => result.Sequence == moveSequence,
                "A MoveRp");
            Ensure(moveResult.Succeeded, $"A MoveRp failed. result={moveResult.ResultCode}");
            MoveNotification moveAtB = await ReadUntilAsync(
                eventsB.Moves.Reader,
                notification => notification.EntityId == enterA.EntityId && notification.Sequence == moveSequence,
                "B observes A MoveNoti");
            Ensure(moveAtB.MoveState == EWorldMoveState.Start, "B observed an unexpected move state.");
            Console.WriteLine("[PASS] MoveRq/Rp and remote MoveNoti");

            const uint burstFirstSequence = moveSequence + 1;
            const uint burstRequestCount = 16;
            var pendingBurstSequences = new HashSet<uint>();
            for (uint offset = 0; offset < burstRequestCount; ++offset)
            {
                uint sequence = burstFirstSequence + offset;
                pendingBurstSequences.Add(sequence);
                Ensure(clientA.TrySendMove(
                        sequence,
                        EWorldMoveState.Sync,
                        enterA.PositionX + 9.0f + offset,
                        enterA.PositionY,
                        1.0f,
                        0.0f),
                    $"Burst MoveRq could not be queued. sequence={sequence}");
            }

            int acceptedBurstCount = 0;
            int supersededBurstCount = 0;
            while (pendingBurstSequences.Count > 0)
            {
                MoveResult burstResult = await ReadUntilAsync(
                    eventsA.MoveResults.Reader,
                    result => pendingBurstSequences.Contains(result.Sequence),
                    "burst MoveRp");
                Ensure(pendingBurstSequences.Remove(burstResult.Sequence),
                    $"Duplicate burst MoveRp. sequence={burstResult.Sequence}");
                if (burstResult.ResultCode == 0)
                {
                    acceptedBurstCount++;
                }
                else
                {
                    Ensure(burstResult.ResultCode == 11,
                        $"Unexpected burst MoveRp result. sequence={burstResult.Sequence}, result={burstResult.ResultCode}");
                    supersededBurstCount++;
                }
            }
            Ensure(acceptedBurstCount > 0, "No burst MoveRq reached the Tick input buffer.");
            Console.WriteLine(
                $"[PASS] burst MoveRq received 1:1 responses: accepted={acceptedBurstCount}, superseded={supersededBurstCount}");

            const uint tapStartSequence = burstFirstSequence + burstRequestCount;
            const uint tapStopSequence = tapStartSequence + 1;
            Ensure(clientA.TrySendMove(
                    tapStartSequence,
                    EWorldMoveState.Start,
                    enterA.PositionX + 25.0f,
                    enterA.PositionY,
                    0.0f,
                    -1.0f),
                "Rapid-tap Start MoveRq could not be queued.");
            Ensure(clientA.TrySendMove(
                    tapStopSequence,
                    EWorldMoveState.Stop,
                    enterA.PositionX + 25.0f,
                    enterA.PositionY,
                    0.0f,
                    -1.0f),
                "Rapid-tap Stop MoveRq could not be queued.");

            var pendingTapSequences = new HashSet<uint> { tapStartSequence, tapStopSequence };
            MoveResult? tapStopResult = null;
            while (pendingTapSequences.Count > 0)
            {
                MoveResult tapResult = await ReadUntilAsync(
                    eventsA.MoveResults.Reader,
                    result => pendingTapSequences.Contains(result.Sequence),
                    "rapid-tap MoveRp");
                Ensure(pendingTapSequences.Remove(tapResult.Sequence),
                    $"Duplicate rapid-tap MoveRp. sequence={tapResult.Sequence}");
                Ensure(tapResult.ResultCode is 0 or 11,
                    $"Unexpected rapid-tap MoveRp result. sequence={tapResult.Sequence}, result={tapResult.ResultCode}");
                if (tapResult.Sequence == tapStopSequence)
                {
                    tapStopResult = tapResult;
                }
            }

            Ensure(tapStopResult is { Succeeded: true, MoveState: EWorldMoveState.Stop },
                "The final rapid-tap Stop input was not accepted by the server.");
            MoveNotification tapStopAtB = await ReadUntilAsync(
                eventsB.Moves.Reader,
                notification => notification.EntityId == enterA.EntityId && notification.Sequence == tapStopSequence,
                "B observes rapid-tap Stop MoveNoti");
            Ensure(tapStopAtB.MoveState == EWorldMoveState.Stop,
                "The remote client did not observe the final rapid-tap Stop state.");
            Console.WriteLine("[PASS] rapid key tap preserves the final Stop state");

            await clientA.DisconnectAsync("Smoke test disconnect A.");
            ActorDespawnNotification despawnAtB = await ReadUntilAsync(
                eventsB.Despawns.Reader,
                notification => notification.EntityId == enterA.EntityId,
                "B observes A despawn");
            Ensure(despawnAtB.EntityId == enterA.EntityId, "Despawn entity mismatch.");
            Console.WriteLine("[PASS] remote ActorDespawnNoti");

            await clientB.DisconnectAsync("Smoke test complete.");
            Console.WriteLine("[WorldClient smoke] PASS");
            return 0;
        }
        catch (Exception exception)
        {
            Console.Error.WriteLine($"[WorldClient smoke] FAIL: {exception}");
            return 1;
        }
        finally
        {
            eventsA.Dispose();
            eventsB.Dispose();
        }
    }

    public static async Task<int> RunMonsterSpawnAsync(ClientSettings settings, uint mapDataId)
    {
        await using var client = new WorldTcpClient();
        using var events = new SmokeEventSink("Monster", client);
        try
        {
            Console.WriteLine($"[WorldClient monster smoke] connect. mapDataId={mapDataId}");
            await client.ConnectAsync(CreateConnectionSettings(settings));

            const ulong requestId = 30_001;
            await client.SendMapEnterAsync(requestId, mapDataId);
            MapEnterResult enter = await ReadUntilAsync(
                events.MapEnters.Reader,
                result => result.RequestId == requestId,
                "MapEnterRp");
            Ensure(enter.Succeeded, $"MapEnter failed. result={enter.ResultCode}");

            float currentX = enter.PositionX;
            float currentY = enter.PositionY;
            const float targetX = 256.0f;
            const float targetY = 256.0f;
            uint sequence = 0;
            for (int stepIndex = 0; stepIndex < 16; ++stepIndex)
            {
                float deltaX = targetX - currentX;
                float deltaY = targetY - currentY;
                float distance = MathF.Sqrt(deltaX * deltaX + deltaY * deltaY);
                if (distance <= 1.0f)
                {
                    break;
                }

                float stepDistance = MathF.Min(48.0f, distance);
                float directionX = deltaX / distance;
                float directionY = deltaY / distance;
                float requestedX = currentX + directionX * stepDistance;
                float requestedY = currentY + directionY * stepDistance;
                ++sequence;
                Ensure(client.TrySendMove(
                        sequence,
                        EWorldMoveState.Sync,
                        requestedX,
                        requestedY,
                        directionX,
                        directionY),
                    $"MoveRq could not be queued. sequence={sequence}");

                MoveResult move = await ReadUntilAsync(
                    events.MoveResults.Reader,
                    result => result.Sequence == sequence,
                    $"MoveRp sequence={sequence}");
                Ensure(move.Succeeded, $"MoveRp failed. sequence={sequence}, result={move.ResultCode}");
                currentX = move.AcceptedPositionX;
                currentY = move.AcceptedPositionY;
            }

            Ensure(MathF.Abs(currentX - targetX) <= 1.0f && MathF.Abs(currentY - targetY) <= 1.0f,
                $"Could not reach the Monster AOI test point. position=({currentX:F1}, {currentY:F1})");

            ActorSpawnNotification monster = await ReadUntilAsync(
                events.Spawns.Reader,
                notification => notification.ActorKind == EWorldActorKind.Monster,
                "Monster ActorSpawnNoti");
            Ensure(monster.ActorDataId != 0, "Monster ActorDataId is invalid.");
            Console.WriteLine(
                $"[PASS] Monster ActorSpawnNoti kind={monster.ActorKind}, data={monster.ActorDataId}, entity={monster.EntityId}");

            MoveNotification monsterMove = await ReadUntilAsync(
                events.Moves.Reader,
                notification => (notification.EntityId & (1UL << 63)) != 0,
                "Monster MoveNoti");
            Ensure(float.IsFinite(monsterMove.PositionX) && float.IsFinite(monsterMove.PositionY),
                "Monster MoveNoti contains a non-finite position.");
            Ensure(monsterMove.ServerTick != 0, "Monster MoveNoti ServerTick is invalid.");
            Console.WriteLine(
                $"[PASS] Monster MoveNoti entity={monsterMove.EntityId}, tick={monsterMove.ServerTick}, " +
                $"position=({monsterMove.PositionX:F1}, {monsterMove.PositionY:F1})");

            ActorSpawnNotification combatMonster = monster.EntityId == monsterMove.EntityId
                ? monster
                : await ReadUntilAsync(
                    events.Spawns.Reader,
                    notification => notification.EntityId == monsterMove.EntityId &&
                        notification.ActorKind == EWorldActorKind.Monster,
                    "moving Monster ActorSpawnNoti");

            ActorAttackNotification monsterAttack = await ReadUntilAsync(
                events.Attacks.Reader,
                notification => notification.AttackerEntityId == combatMonster.EntityId &&
                    notification.TargetEntityId == enter.EntityId &&
                    notification.ServerTick >= monsterMove.ServerTick,
                "Monster ActorAttackNoti");
            Ensure((monsterAttack.AttackerEntityId & (1UL << 63)) != 0,
                "ActorAttackNoti attacker is not a Monster entity.");
            Ensure(monsterAttack.Damage > 0,
                "ActorAttackNoti damage must be positive.");
            Ensure(monsterAttack.TargetMaxHp > 0 && monsterAttack.TargetCurrentHp <= monsterAttack.TargetMaxHp,
                "ActorAttackNoti target HP snapshot is invalid.");
            Console.WriteLine(
                $"[PASS] Monster ActorAttackNoti attacker={monsterAttack.AttackerEntityId}, " +
                $"damage={monsterAttack.Damage}, hp={monsterAttack.TargetCurrentHp}/{monsterAttack.TargetMaxHp}, " +
                $"tick={monsterAttack.ServerTick}");

            while (events.Spawns.Reader.TryRead(out _))
            {
            }

            uint attackSequence = 0;
            uint monsterCurrentHp = combatMonster.CurrentHp;
            while (monsterCurrentHp > 0)
            {
                ++attackSequence;
                Ensure(client.TrySendBasicAttack(attackSequence, monsterAttack.AttackerEntityId),
                    $"BasicAttackRq could not be queued. sequence={attackSequence}");

                BasicAttackResult attackResult = await ReadUntilAsync(
                    events.BasicAttackResults.Reader,
                    result => result.AttackSequence == attackSequence,
                    $"BasicAttackRp sequence={attackSequence}");
                Ensure(attackResult.Succeeded,
                    $"BasicAttackRp failed. sequence={attackSequence}, result={attackResult.ResultCode}");

                ActorAttackNotification playerAttack = await ReadUntilAsync(
                    events.Attacks.Reader,
                    notification => notification.AttackerEntityId == enter.EntityId &&
                        notification.TargetEntityId == monsterAttack.AttackerEntityId &&
                        notification.ServerTick == attackResult.ServerTick,
                    $"Player ActorAttackNoti sequence={attackSequence}");
                Ensure(playerAttack.Damage > 0 && playerAttack.TargetCurrentHp < monsterCurrentHp,
                    "Player BasicAttack did not reduce the Monster HP.");
                monsterCurrentHp = playerAttack.TargetCurrentHp;

                if (monsterCurrentHp > 0)
                {
                    await Task.Delay(TimeSpan.FromMilliseconds(1_100));
                }
            }

            ActorDeathNotification monsterDeath = await ReadUntilAsync(
                events.Deaths.Reader,
                notification => notification.EntityId == monsterAttack.AttackerEntityId &&
                    notification.KillerEntityId == enter.EntityId,
                "Monster ActorDeathNoti");
            Ensure(monsterDeath.LifeRevision == combatMonster.LifeRevision,
                "Monster death revision does not match the visible spawn generation.");
            ActorDespawnNotification monsterDespawn = await ReadUntilAsync(
                events.Despawns.Reader,
                notification => notification.EntityId == monsterAttack.AttackerEntityId,
                "dead Monster ActorDespawnNoti");
            Ensure(monsterDespawn.EntityId == monsterAttack.AttackerEntityId,
                "Dead Monster Despawn entity mismatch.");
            Console.WriteLine(
                $"[PASS] Player BasicAttack -> Monster death/despawn attacks={attackSequence}, " +
                $"entity={monsterDeath.EntityId}, tick={monsterDeath.ServerTick}");

            ActorSpawnNotification respawnedMonster = await ReadUntilAsync(
                events.Spawns.Reader,
                notification => notification.ActorKind == EWorldActorKind.Monster &&
                    notification.ActorDataId == combatMonster.ActorDataId &&
                    notification.EntityId != combatMonster.EntityId &&
                    notification.LifeRevision > combatMonster.LifeRevision,
                "Monster respawn ActorSpawnNoti");
            Ensure(respawnedMonster.CurrentHp == respawnedMonster.MaxHp && respawnedMonster.MaxHp > 0,
                "Respawned Monster did not start at full HP.");
            Console.WriteLine(
                $"[PASS] Monster Spawner respawn entity={respawnedMonster.EntityId}, " +
                $"revision={respawnedMonster.LifeRevision}");

            ++sequence;
            Ensure(client.TrySendMove(
                    sequence,
                    EWorldMoveState.Stop,
                    currentX,
                    currentY,
                    0.0f,
                    1.0f),
                "Final Stop MoveRq could not be queued.");
            MoveResult stop = await ReadUntilAsync(
                events.MoveResults.Reader,
                result => result.Sequence == sequence,
                "final Stop MoveRp");
            Ensure(stop.Succeeded, $"Final Stop MoveRp failed. result={stop.ResultCode}");

            await client.DisconnectAsync("Monster smoke complete.");
            Console.WriteLine("[WorldClient monster smoke] PASS");
            return 0;
        }
        catch (Exception exception)
        {
            Console.Error.WriteLine($"[WorldClient monster smoke] FAIL: {exception}");
            return 1;
        }
    }

    private static void ValidateOptionalPlayerSnapshot(MapEnterResult result, string clientName)
    {
        if (!result.HasPlayerSnapshot)
        {
            return;
        }

        Ensure(result.CharacterDataId != 0 && result.Level != 0, $"{clientName} Player Snapshot identity is invalid.");
        Ensure(result.ProgressVersion != 0 && result.StatVersion != 0 && result.StatRevision != 0,
            $"{clientName} Player Snapshot version is invalid.");
        Ensure(result.MaxHp != 0 && result.CurrentHp == result.MaxHp, $"{clientName} HP was not initialized from Cache Snapshot.");
        Ensure(result.MaxMp != 0 && result.CurrentMp == result.MaxMp, $"{clientName} MP was not initialized from Cache Snapshot.");
    }

    private static WorldConnectionSettings CreateConnectionSettings(ClientSettings settings) => new(
        settings.WorldServerHost,
        settings.WorldServerPort,
        settings.WorldPacketKey);

    private static async Task<T> ReadUntilAsync<T>(
        ChannelReader<T> reader,
        Func<T, bool> predicate,
        string step)
    {
        using var timeout = new CancellationTokenSource(s_stepTimeout);
        try
        {
            while (await reader.WaitToReadAsync(timeout.Token))
            {
                while (reader.TryRead(out T? value))
                {
                    if (predicate(value))
                    {
                        return value;
                    }
                }
            }
        }
        catch (OperationCanceledException) when (timeout.IsCancellationRequested)
        {
            throw new TimeoutException($"Timed out while waiting for {step}.");
        }
        throw new InvalidOperationException($"Event stream completed while waiting for {step}.");
    }

    private static void Ensure(bool condition, string message)
    {
        if (!condition)
        {
            throw new InvalidOperationException(message);
        }
    }

    private sealed class SmokeEventSink : IDisposable
    {
        private readonly string m_name;
        private readonly WorldTcpClient m_client;

        public SmokeEventSink(string name, WorldTcpClient client)
        {
            m_name = name;
            m_client = client;
            m_client.SystemMessageReceived += HandleSystemMessage;
            m_client.MapEnterResultReceived += HandleMapEnter;
            m_client.ActorSpawnReceived += HandleSpawn;
            m_client.ActorDespawnReceived += HandleDespawn;
            m_client.MoveResultReceived += HandleMoveResult;
            m_client.MoveNotificationReceived += HandleMove;
            m_client.BasicAttackResultReceived += HandleBasicAttackResult;
            m_client.ActorAttackReceived += HandleAttack;
            m_client.ActorDeathReceived += HandleDeath;
            m_client.ActorRespawnReceived += HandleRespawn;
        }

        public Channel<MapEnterResult> MapEnters { get; } = Channel.CreateUnbounded<MapEnterResult>();
        public Channel<ActorSpawnNotification> Spawns { get; } = Channel.CreateUnbounded<ActorSpawnNotification>();
        public Channel<ActorDespawnNotification> Despawns { get; } = Channel.CreateUnbounded<ActorDespawnNotification>();
        public Channel<MoveResult> MoveResults { get; } = Channel.CreateUnbounded<MoveResult>();
        public Channel<MoveNotification> Moves { get; } = Channel.CreateUnbounded<MoveNotification>();
        public Channel<BasicAttackResult> BasicAttackResults { get; } = Channel.CreateUnbounded<BasicAttackResult>();
        public Channel<ActorAttackNotification> Attacks { get; } = Channel.CreateUnbounded<ActorAttackNotification>();
        public Channel<ActorDeathNotification> Deaths { get; } = Channel.CreateUnbounded<ActorDeathNotification>();
        public Channel<ActorRespawnNotification> Respawns { get; } = Channel.CreateUnbounded<ActorRespawnNotification>();

        public void Dispose()
        {
            m_client.SystemMessageReceived -= HandleSystemMessage;
            m_client.MapEnterResultReceived -= HandleMapEnter;
            m_client.ActorSpawnReceived -= HandleSpawn;
            m_client.ActorDespawnReceived -= HandleDespawn;
            m_client.MoveResultReceived -= HandleMoveResult;
            m_client.MoveNotificationReceived -= HandleMove;
            m_client.BasicAttackResultReceived -= HandleBasicAttackResult;
            m_client.ActorAttackReceived -= HandleAttack;
            m_client.ActorDeathReceived -= HandleDeath;
            m_client.ActorRespawnReceived -= HandleRespawn;
        }

        private void HandleSystemMessage(string message) => Console.WriteLine($"[{m_name}] {message}");
        private void HandleMapEnter(MapEnterResult value) => MapEnters.Writer.TryWrite(value);
        private void HandleSpawn(ActorSpawnNotification value) => Spawns.Writer.TryWrite(value);
        private void HandleDespawn(ActorDespawnNotification value) => Despawns.Writer.TryWrite(value);
        private void HandleMoveResult(MoveResult value) => MoveResults.Writer.TryWrite(value);
        private void HandleMove(MoveNotification value) => Moves.Writer.TryWrite(value);
        private void HandleBasicAttackResult(BasicAttackResult value) => BasicAttackResults.Writer.TryWrite(value);
        private void HandleAttack(ActorAttackNotification value) => Attacks.Writer.TryWrite(value);
        private void HandleDeath(ActorDeathNotification value) => Deaths.Writer.TryWrite(value);
        private void HandleRespawn(ActorRespawnNotification value) => Respawns.Writer.TryWrite(value);
    }
}
