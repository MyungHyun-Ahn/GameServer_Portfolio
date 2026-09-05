using WorldClientCore.Models;
using WorldClientCore.Simulation;

namespace WorldClientCoreSmokeTest;

internal static class Program
{
    private static int Main()
    {
        try
        {
            var simulation = new FWorldSimulationState(new WorldSimulationOptions
            {
                WorldWidth = 100.0f,
                WorldHeight = 100.0f,
                MoveSpeed = 10.0f
            });
            var enter = new MapEnterResult(
                ResultCode: 0,
                RequestId: 1,
                MapInstanceId: 10,
                EntityId: 100,
                PositionX: 95.0f,
                PositionY: 50.0f,
                DirectionX: 1.0f,
                DirectionY: 0.0f,
                ServerTick: 500,
                CharacterId: 1000,
                CharacterDataId: 1,
                Level: 2,
                Exp: 25,
                RequiredExpToNextLevel: 100,
                Str: 5,
                Dex: 6,
                Intelligence: 7,
                Luk: 8,
                UnspentStatPoints: 3,
                ProgressVersion: 4,
                StatVersion: 5,
                FinalStr: 9,
                FinalDex: 10,
                FinalIntelligence: 11,
                FinalLuk: 12,
                CurrentHp: 110,
                MaxHp: 120,
                CurrentMp: 60,
                MaxMp: 70,
                Attack: 26,
                Defense: 9,
                MoveSpeedMilli: 10_000,
                EquipmentVersion: 6,
                StatRevision: 7);
            simulation.EnterLocal(enter);
            Ensure(simulation.LocalEntityId == 100 && simulation.MapInstanceId == 10, "EnterLocal identity failed.");
            Ensure(simulation.LocalPlayerInitialization == enter && enter.HasPlayerSnapshot,
                "EnterLocal Player Snapshot failed.");
            Ensure(simulation.TryGetLocalSnapshot(out WorldActorSnapshot localActor) &&
                   localActor.ActorKind == EWorldActorKind.Player &&
                   localActor.ActorDataId == enter.CharacterDataId,
                "Local actor identity failed.");

            Ensure(simulation.SetLocalInput(1.0f, 0.0f, EWorldMoveState.Start, 1), "Local input failed.");
            simulation.Advance(TimeSpan.FromSeconds(1.0));
            EnsureLocalPosition(simulation, 96.0f, 50.0f, "Elapsed clamp failed.");
            for (int index = 0; index < 10; ++index)
            {
                simulation.Advance(TimeSpan.FromMilliseconds(100.0));
            }
            EnsureLocalPosition(simulation, 100.0f, 50.0f, "World boundary clamp failed.");

            var staleResult = new MoveResult(0, 0, EWorldMoveState.Stop, 20.0f, 20.0f, 0.0f, 1.0f, true);
            Ensure(!simulation.ApplyMoveResult(staleResult), "Stale local sequence was accepted.");
            var correctedResult = new MoveResult(0, 2, EWorldMoveState.Stop, 30.0f, 40.0f, 0.0f, 1.0f, true);
            Ensure(simulation.ApplyMoveResult(correctedResult), "Corrected MoveRp was ignored.");
            EnsureLocalPosition(simulation, 30.0f, 40.0f, "MoveRp correction failed.");
            Ensure(!simulation.ApplyDeathNotification(new ActorDeathNotification(100, 200, 7, 504)),
                "Future local death revision jump was accepted.");
            Ensure(simulation.ApplyDeathNotification(new ActorDeathNotification(100, 200, 1, 504)),
                "Local death transition failed.");
            Ensure(simulation.LocalIsDead && simulation.LocalCurrentHp == 0 && simulation.LocalLifeRevision == 1,
                "Local death state is invalid.");
            Ensure(!simulation.ApplyMoveResult(new MoveResult(
                    0, 3, EWorldMoveState.Start, 31.0f, 40.0f, 1.0f, 0.0f, false)),
                "Dead actor accepted MoveRp.");
            Ensure(!simulation.ApplyRespawnNotification(new ActorRespawnNotification(
                    100, 12.0f, 18.0f, 0.0f, 1.0f, 120, 120, 7, 505)),
                "Future local respawn revision jump was accepted.");
            Ensure(simulation.ApplyRespawnNotification(new ActorRespawnNotification(
                    100, 12.0f, 18.0f, 0.0f, 1.0f, 120, 120, 2, 505)),
                "Local respawn transition failed.");
            Ensure(!simulation.LocalIsDead && simulation.LocalLifeRevision == 2 && simulation.LocalCurrentHp == 120,
                "Local respawn state is invalid.");
            EnsureLocalPosition(simulation, 12.0f, 18.0f, "Respawn position failed.");
            Ensure(!simulation.ApplyDeathNotification(new ActorDeathNotification(100, 200, 1, 506)),
                "Stale death reverted the respawned actor.");

            simulation.Spawn(new ActorSpawnNotification(
                300, EWorldActorKind.Player, 1, 40.0f, 40.0f, 0.0f, 1.0f, 0,
                EWorldMoveState.Stop, 506, 0, 100, ulong.MaxValue));
            Ensure(!simulation.ApplyRespawnNotification(new ActorRespawnNotification(
                    300, 41.0f, 41.0f, 0.0f, 1.0f, 100, 100, 1, 507)),
                "LifeRevision overflow wrapped to a valid respawn transition.");
            Ensure(simulation.Despawn(300), "Overflow boundary test actor was not removed.");

            Ensure(!simulation.ApplyMoveNotification(new MoveNotification(
                    200, 4, EWorldMoveState.Stop, 80.0f, 80.0f, 1.0f, 0.0f, 500)),
                "MoveNoti created an actor before ActorSpawnNoti.");
            simulation.Spawn(new ActorSpawnNotification(
                200, EWorldActorKind.Monster, 1001, 10.0f, 20.0f, 0.0f, 1.0f, 5,
                EWorldMoveState.Start, 501, 80, 100, 1));
            var snapshots = new List<WorldActorSnapshot>();
            simulation.CopyActorSnapshots(snapshots);
            Ensure(snapshots.Count == 2, "Spawn or snapshot copy failed.");
            WorldActorSnapshot monster = snapshots.Single(actor => actor.EntityId == 200);
            Ensure(monster.ActorKind == EWorldActorKind.Monster && monster.ActorDataId == 1001 &&
                   monster.CurrentHp == 80 && monster.MaxHp == 100,
                "Spawned monster identity or HP snapshot was not retained.");

            Ensure(!simulation.ApplyMoveNotification(new MoveNotification(
                200, 4, EWorldMoveState.Stop, 80.0f, 80.0f, 1.0f, 0.0f, 500)),
                "Stale Monster server tick was accepted.");
            Ensure(simulation.ApplyMoveNotification(new MoveNotification(
                200, 6, EWorldMoveState.Stop, 25.0f, 35.0f, 1.0f, 0.0f, 503)),
                "Remote MoveNoti was ignored.");
            Ensure(simulation.Despawn(200) && simulation.ActorCount == 1, "Despawn failed.");
            Ensure(!simulation.ApplyMoveNotification(new MoveNotification(
                    200, 7, EWorldMoveState.Start, 30.0f, 40.0f, 1.0f, 0.0f, 504)) &&
                   simulation.ActorCount == 1,
                "A late MoveNoti recreated a despawned actor.");
            simulation.Spawn(new ActorSpawnNotification(
                201, EWorldActorKind.Monster, 1001, 30.0f, 40.0f, 0.0f, 1.0f, 0,
                EWorldMoveState.Stop, 505, 100, 100, 2));
            simulation.CopyActorSnapshots(snapshots);
            Ensure(snapshots.Any(actor => actor.EntityId == 201 &&
                                          actor.ActorKind == EWorldActorKind.Monster &&
                                          actor.ActorDataId == 1001 &&
                                          actor.LifeRevision == 2),
                "Respawned monster state was not created from ActorSpawnNoti.");

            simulation.Clear();
            Ensure(simulation.LocalEntityId == 0 && simulation.MapInstanceId == 0 && simulation.ActorCount == 0,
                "Clear failed.");
            Console.WriteLine("[PASS] WorldClientCore simulation state");
            return 0;
        }
        catch (Exception exception)
        {
            Console.Error.WriteLine($"[FAIL] {exception}");
            return 1;
        }
    }

    private static void EnsureLocalPosition(
        FWorldSimulationState simulation,
        float expectedX,
        float expectedY,
        string message)
    {
        Ensure(simulation.TryGetLocalSnapshot(out WorldActorSnapshot snapshot), "Local actor missing.");
        Ensure(Math.Abs(snapshot.PositionX - expectedX) < 0.001f &&
               Math.Abs(snapshot.PositionY - expectedY) < 0.001f,
            $"{message} actual=({snapshot.PositionX}, {snapshot.PositionY})");
    }

    private static void Ensure(bool condition, string message)
    {
        if (!condition)
        {
            throw new InvalidOperationException(message);
        }
    }
}
