using WorldClientCore.Models;
using WorldClientCore.Simulation;
using WorldClientWinForms.Controls;

namespace WorldClientWinForms.SmokeTests;

internal static class WorldSimulationSmokeTest
{
    public static int Run()
    {
        try
        {
            var simulation = new FWorldSimulationState();
            simulation.EnterLocal(CreateMapEnterResult(moveSpeedMilli: 98_000));
            Ensure(Math.Abs(simulation.LocalMoveSpeed - 98.0f) < 0.001f, "MapEnter MoveSpeed was not applied.");
            Ensure(simulation.SetLocalInput(1.0f, 0.0f, EWorldMoveState.Start, 1), "Local input was rejected.");
            Ensure(simulation.Advance(TimeSpan.FromMilliseconds(100)), "Local actor did not advance.");
            Ensure(simulation.TryGetLocalSnapshot(out WorldActorSnapshot first), "Local actor snapshot is missing.");
            Ensure(Math.Abs(first.PositionX - 109.8f) < 0.001f, "Initial server MoveSpeed was not used.");

            Ensure(simulation.SetLocalMoveSpeed(120_000), "Equipment MoveSpeed update was rejected.");
            Ensure(simulation.Advance(TimeSpan.FromMilliseconds(100)), "Updated local actor did not advance.");
            Ensure(simulation.TryGetLocalSnapshot(out WorldActorSnapshot second), "Updated local actor snapshot is missing.");
            Ensure(Math.Abs(second.PositionX - 121.8f) < 0.001f, "Equipment MoveSpeed was not used.");
            Ensure(!simulation.SetLocalMoveSpeed(0), "Zero MoveSpeed must be rejected.");
            Ensure(simulation.LocalCurrentHp == 120 && simulation.LocalMaxHp == 120,
                "MapEnter HP was not applied to the local simulation state.");

            Ensure(simulation.ApplyAttackNotification(new ActorAttackNotification(
                    9001,
                    1,
                    17,
                    103,
                    120,
                    10)),
                "A valid ActorAttackNoti was rejected.");
            Ensure(simulation.LocalCurrentHp == 103 && simulation.LocalMaxHp == 120,
                "ActorAttackNoti did not update the local HP snapshot.");
            Ensure(simulation.TryGetLocalSnapshot(out WorldActorSnapshot damaged),
                "Damaged local actor snapshot is missing.");
            Ensure(damaged.CurrentHp == 103 && damaged.MaxHp == 120,
                "Damaged HP was not copied into WorldActorSnapshot.");
            Ensure(!simulation.ApplyAttackNotification(new ActorAttackNotification(
                    9001,
                    1,
                    99,
                    4,
                    120,
                    9)),
                "A stale ActorAttackNoti was accepted.");
            Ensure(simulation.LocalCurrentHp == 103,
                "A stale ActorAttackNoti changed the local HP.");
            Ensure(!simulation.ApplyAttackNotification(new ActorAttackNotification(
                    9001,
                    1,
                    1,
                    121,
                    120,
                    11)),
                "An invalid ActorAttackNoti HP snapshot was accepted.");
            Ensure(simulation.LocalLifeRevision == 1 && !simulation.LocalIsDead,
                "Initial local life state is invalid.");
            Ensure(!simulation.ApplyDeathNotification(new ActorDeathNotification(1, 9001, 3, 11)),
                "A future ActorDeathNoti revision jump was accepted.");
            Ensure(simulation.ApplyDeathNotification(new ActorDeathNotification(1, 9001, 1, 11)),
                "A valid ActorDeathNoti was rejected.");
            Ensure(simulation.LocalIsDead && simulation.LocalCurrentHp == 0 && simulation.LocalLifeRevision == 1,
                "ActorDeathNoti did not commit the local death state.");
            Ensure(!simulation.SetLocalInput(1.0f, 0.0f, EWorldMoveState.Start, 2),
                "Dead local actor accepted movement input.");
            Ensure(!simulation.ApplyRespawnNotification(new ActorRespawnNotification(
                    1, 130.0f, 140.0f, 0.0f, 1.0f, 120, 120, 1, 12)),
                "Respawn without a newer LifeRevision was accepted.");
            Ensure(!simulation.ApplyRespawnNotification(new ActorRespawnNotification(
                    1, 130.0f, 140.0f, 0.0f, 1.0f, 120, 120, 4, 12)),
                "A future ActorRespawnNoti revision jump was accepted.");
            Ensure(simulation.ApplyRespawnNotification(new ActorRespawnNotification(
                    1, 130.0f, 140.0f, 0.0f, 1.0f, 120, 120, 2, 12)),
                "A valid ActorRespawnNoti was rejected.");
            Ensure(!simulation.LocalIsDead && simulation.LocalCurrentHp == 120 && simulation.LocalLifeRevision == 2,
                "ActorRespawnNoti did not restore the local life state.");
            Ensure(simulation.TryGetLocalSnapshot(out WorldActorSnapshot respawned) &&
                   Math.Abs(respawned.PositionX - 130.0f) < 0.001f &&
                   Math.Abs(respawned.PositionY - 140.0f) < 0.001f,
                "ActorRespawnNoti did not apply the authoritative position.");
            Ensure(!simulation.ApplyDeathNotification(new ActorDeathNotification(1, 9001, 1, 13)),
                "A stale ActorDeathNoti reverted a newer life.");
            Ensure(!simulation.ApplyDeathNotification(new ActorDeathNotification(1, 9001, 3, 13)),
                "A future ActorDeathNoti revision jump changed the current life.");
            Ensure(simulation.SetLocalHealth(80, 100), "A valid equipment HP snapshot was rejected.");
            Ensure(simulation.LocalCurrentHp == 80 && simulation.LocalMaxHp == 100,
                "Equipment HP snapshot did not replace the local simulation state.");

            simulation.Spawn(new ActorSpawnNotification(
                2,
                EWorldActorKind.Player,
                1,
                100.0f,
                100.0f,
                1.0f,
                0.0f,
                1,
                EWorldMoveState.Start,
                1,
                100,
                100,
                1));
            Ensure(simulation.Advance(TimeSpan.FromMilliseconds(100)), "Remote actor did not advance.");
            var actors = new List<WorldActorSnapshot>();
            simulation.CopyActorSnapshots(actors);
            WorldActorSnapshot remote = actors.Single(actor => !actor.IsLocal);
            Ensure(Math.Abs(remote.PositionX - 109.6f) < 0.001f, "Local equipment speed leaked into remote prediction.");

            simulation.Clear();
            simulation.Spawn(new ActorSpawnNotification(
                3,
                EWorldActorKind.Monster,
                1001,
                200.0f,
                200.0f,
                1.0f,
                0.0f,
                uint.MaxValue,
                EWorldMoveState.Start,
                10,
                60,
                100,
                1));
            Ensure(simulation.ApplyMoveNotification(new MoveNotification(
                3, 0, EWorldMoveState.Start, 202.0f, 200.0f, 1.0f, 0.0f, 11)),
                "Monster MoveNoti must use ServerTick instead of the Player sequence policy.");
            Ensure(!simulation.ApplyMoveNotification(new MoveNotification(
                3, uint.MaxValue, EWorldMoveState.Start, 999.0f, 999.0f, 1.0f, 0.0f, 9)),
                "A stale Monster MoveNoti was accepted.");
            Ensure(simulation.Advance(TimeSpan.FromMilliseconds(100)), "Moving monster animation did not advance.");
            actors.Clear();
            simulation.CopyActorSnapshots(actors);
            WorldActorSnapshot monster = actors.Single();
            Ensure(Math.Abs(monster.PositionX - 202.0f) < 0.001f && Math.Abs(monster.PositionY - 200.0f) < 0.001f,
                "Monster authoritative position was changed by a stale packet or client extrapolation.");
            Ensure(Math.Abs(monster.AnimationTime - 0.1f) < 0.001f,
                "Moving monster animation time did not advance independently from position prediction.");

            Ensure(simulation.ApplyMoveNotification(new MoveNotification(
                3, 0, EWorldMoveState.Stop, 202.0f, 200.0f, 1.0f, 0.0f, 12)),
                "Monster stop notification was rejected.");
            Ensure(!simulation.Advance(TimeSpan.FromMilliseconds(100)),
                "Stopped monster must keep its idle animation frame.");
            actors.Clear();
            simulation.CopyActorSnapshots(actors);
            WorldActorSnapshot stoppedMonster = actors.Single();
            Ensure(Math.Abs(stoppedMonster.AnimationTime - monster.AnimationTime) < 0.001f,
                "Stopped monster animation time changed.");
            Ensure(simulation.TryGetActorSnapshot(3, out WorldActorSnapshot targetMonster) &&
                   targetMonster.ActorKind == EWorldActorKind.Monster &&
                   !targetMonster.IsDead,
                "A living Monster could not be resolved as an attack target.");
            Ensure(simulation.ApplyAttackNotification(new ActorAttackNotification(
                    1, 3, 40, 20, 100, 13)),
                "Player attack damage was not applied to the Monster snapshot.");
            Ensure(simulation.TryGetActorSnapshot(3, out targetMonster) && targetMonster.CurrentHp == 20,
                "The selected Monster HP snapshot was not updated.");
            Ensure(simulation.ApplyDeathNotification(new ActorDeathNotification(3, 1, 1, 14)),
                "Monster death notification was rejected.");
            Ensure(simulation.TryGetActorSnapshot(3, out targetMonster) && targetMonster.IsDead,
                "A dead Monster remained eligible as a living target.");

            Rectangle? downIdleFrame = WorldViewControl.CalculateMonsterFrameSource(
                new Size(64, 64), 0.0f, 1.0f, EWorldMoveState.Stop, 1.0f, false);
            Ensure(downIdleFrame == new Rectangle(0, 0, 16, 16),
                "Monster idle frame did not select the down-direction first row.");
            Rectangle? rightWalkFrame = WorldViewControl.CalculateMonsterFrameSource(
                new Size(64, 64), 1.0f, 0.0f, EWorldMoveState.Start, 0.25f, false);
            Ensure(rightWalkFrame == new Rectangle(48, 32, 16, 16),
                "Monster moving frame did not select the expected direction and animation row.");
            Ensure(WorldViewControl.CalculateMonsterFrameSource(
                    new Size(38, 38), 1.0f, 0.0f, EWorldMoveState.Start, 0.25f, false) is null,
                "Static monster image was incorrectly treated as a 4x4 animation sheet.");

            Console.WriteLine("[WorldSimulation smoke] PASS");
            return 0;
        }
        catch (Exception exception)
        {
            Console.Error.WriteLine($"[WorldSimulation smoke] FAIL: {exception}");
            return 1;
        }
    }

    private static MapEnterResult CreateMapEnterResult(uint moveSpeedMilli) => new(
        0,
        1,
        1,
        1,
        100.0f,
        100.0f,
        1.0f,
        0.0f,
        1,
        1,
        1,
        1,
        0,
        100,
        4,
        4,
        4,
        4,
        0,
        1,
        1,
        4,
        4,
        4,
        4,
        120,
        120,
        70,
        70,
        26,
        9,
        moveSpeedMilli,
        1,
        1);

    private static void Ensure(bool condition, string message)
    {
        if (!condition)
        {
            throw new InvalidOperationException(message);
        }
    }
}
