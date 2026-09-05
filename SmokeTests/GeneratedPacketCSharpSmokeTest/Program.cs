using Generated.Packets;
using Generated.Packets.Chat;
using Generated.Packets.Echo;
using Generated.Packets.Map;
using ClientNetwork.Packet;

internal static class Program
{
    private static readonly byte[] EchoFixture = Convert.FromHexString("06000000ED959CEAB880");

    public static int Main()
    {
        try
        {
            TestCppCompatibleStringFixture();
            TestGeneratedVectorRoundTrip();
            TestMalformedBodyRejection();
            TestGeneratedRouter();
            TestGeneratedMapPacketRoundTrips();
            TestGeneratedMapRouter();
            TestGeneratedPacketFramePipeline();
            Console.WriteLine("Generated C# packet smoke test passed.");
            return 0;
        }
        catch (Exception exception)
        {
            Console.Error.WriteLine($"Generated C# packet smoke test failed: {exception}");
            return 1;
        }
    }

    private static void TestCppCompatibleStringFixture()
    {
        var outgoing = new EchoRq
        {
            Message = "한글"
        };

        var writer = new FPacketWriter();
        outgoing.SerializeBody(writer);
        Require(writer.WrittenSpan.SequenceEqual(EchoFixture), "Generated string serialization differs from the C++ wire fixture.");

        Require(EchoRq.TryDeserializeBody(EchoFixture, out EchoRq? incoming) && incoming is not null,
            "Generated Echo packet could not read the C++ wire fixture.");
        EchoRq decoded = incoming ?? throw new InvalidOperationException("Decoded Echo packet is null.");
        Require(decoded.Message == outgoing.Message, "Generated Echo packet value differs after deserialization.");
    }

    private static void TestGeneratedVectorRoundTrip()
    {
        var outgoing = new RoomListRp
        {
            RoomIds = [10, 20],
            RoomNames = ["Alpha", "Beta"],
            ParticipantCounts = [1, 2],
            Capacities = [8, 16],
            JoinableFlags = [1, 0]
        };

        var writer = new FPacketWriter();
        outgoing.SerializeBody(writer);

        Require(RoomListRp.TryDeserializeBody(writer.WrittenSpan, out RoomListRp? incoming) && incoming is not null,
            "Generated vector packet round-trip failed.");
        RoomListRp decoded = incoming ?? throw new InvalidOperationException("Decoded RoomList packet is null.");
        Require(decoded.RoomIds.SequenceEqual(outgoing.RoomIds), "uint32 vector differs after round-trip.");
        Require(decoded.RoomNames.SequenceEqual(outgoing.RoomNames), "string vector differs after round-trip.");
        Require(decoded.JoinableFlags.SequenceEqual(outgoing.JoinableFlags), "uint8 vector differs after round-trip.");
    }

    private static void TestMalformedBodyRejection()
    {
        byte[] truncated = EchoFixture[..^1];
        Require(!EchoRq.TryDeserializeBody(truncated, out _), "A truncated generated packet must be rejected.");

        byte[] trailing = [.. EchoFixture, 0x00];
        Require(!EchoRq.TryDeserializeBody(trailing, out _), "A generated packet with trailing bytes must be rejected.");

        byte[] impossibleVectorCount = [0xFF, 0xFF, 0xFF, 0x7F];
        Require(!RoomListRp.TryDeserializeBody(impossibleVectorCount, out _),
            "An impossible generated vector count must be rejected before allocation.");

        byte[] oversizedBody = new byte[FPacketProtocol.MaxContentBodySize + 1];
        Require(!EchoRq.TryDeserializeBody(oversizedBody, out _),
            "An oversized generated packet body must be rejected without throwing.");
    }

    private static void TestGeneratedRouter()
    {
        var handler = new EchoTestHandler();
        var router = new PacketRouter();
        router.SetEchoHandler(handler);

        Require(router.DispatchPacket(EchoRq.OpcodeValue, EchoFixture), "Generated router did not dispatch a valid packet.");
        Require(handler.Message == "한글", "Generated handler did not receive the decoded packet.");
        Require(!router.DispatchPacket(ushort.MaxValue, []), "Generated router accepted an unknown opcode.");
    }

    private static void TestGeneratedMapPacketRoundTrips()
    {
        var mapEnterRequest = new MapEnterRq
        {
            RequestId = 1001,
            MapDataId = 7
        };
        byte[] mapEnterRequestBody = SerializeBody(mapEnterRequest);
        Require(MapEnterRq.TryDeserializeBody(mapEnterRequestBody, out MapEnterRq? decodedMapEnterRequest) &&
                decodedMapEnterRequest is not null,
            "Generated MapEnterRq round-trip failed.");
        MapEnterRq mapEnterRequestRoundTrip =
            decodedMapEnterRequest ?? throw new InvalidOperationException("Decoded MapEnterRq packet is null.");
        Require(mapEnterRequestRoundTrip.RequestId == mapEnterRequest.RequestId &&
                mapEnterRequestRoundTrip.MapDataId == mapEnterRequest.MapDataId,
            "Generated MapEnterRq values differ after round-trip.");

        var mapEnterResponse = new MapEnterRp
        {
            ResultCode = 0,
            RequestId = mapEnterRequest.RequestId,
            MapInstanceId = 7001,
            EntityId = 9001,
            PositionX = 125.5f,
            PositionY = 64.25f,
            DirectionX = 1.0f,
            DirectionY = 0.0f,
            ServerTick = 123456,
            CharacterId = 7101,
            CharacterDataId = 31,
            Level = 17,
            Exp = 1701,
            RequiredExpToNextLevel = 1801,
            StrStat = 41,
            DexStat = 42,
            IntStat = 43,
            LukStat = 44,
            UnspentStatPoints = 5,
            ProgressVersion = 6,
            StatVersion = 7,
            FinalStr = 51,
            FinalDex = 52,
            FinalInt = 53,
            FinalLuk = 54,
            CurrentHp = 801,
            MaxHp = 901,
            CurrentMp = 401,
            MaxMp = 501,
            Attack = 61,
            Defense = 62,
            MoveSpeedMilli = 4_500,
            EquipmentVersion = 8,
            StatRevision = 9
        };
        byte[] mapEnterResponseBody = SerializeBody(mapEnterResponse);
        Require(MapEnterRp.TryDeserializeBody(mapEnterResponseBody, out MapEnterRp? decodedMapEnterResponse) &&
                decodedMapEnterResponse is not null,
            "Generated MapEnterRp round-trip failed.");
        MapEnterRp mapEnterResponseRoundTrip =
            decodedMapEnterResponse ?? throw new InvalidOperationException("Decoded MapEnterRp packet is null.");
        Require(mapEnterResponseRoundTrip.ResultCode == mapEnterResponse.ResultCode &&
                mapEnterResponseRoundTrip.RequestId == mapEnterResponse.RequestId &&
                mapEnterResponseRoundTrip.MapInstanceId == mapEnterResponse.MapInstanceId &&
                mapEnterResponseRoundTrip.EntityId == mapEnterResponse.EntityId &&
                mapEnterResponseRoundTrip.PositionX == mapEnterResponse.PositionX &&
                mapEnterResponseRoundTrip.PositionY == mapEnterResponse.PositionY &&
                mapEnterResponseRoundTrip.DirectionX == mapEnterResponse.DirectionX &&
                mapEnterResponseRoundTrip.DirectionY == mapEnterResponse.DirectionY &&
                mapEnterResponseRoundTrip.ServerTick == mapEnterResponse.ServerTick &&
                mapEnterResponseRoundTrip.CharacterId == mapEnterResponse.CharacterId &&
                mapEnterResponseRoundTrip.CharacterDataId == mapEnterResponse.CharacterDataId &&
                mapEnterResponseRoundTrip.Level == mapEnterResponse.Level &&
                mapEnterResponseRoundTrip.Exp == mapEnterResponse.Exp &&
                mapEnterResponseRoundTrip.RequiredExpToNextLevel == mapEnterResponse.RequiredExpToNextLevel &&
                mapEnterResponseRoundTrip.StrStat == mapEnterResponse.StrStat &&
                mapEnterResponseRoundTrip.DexStat == mapEnterResponse.DexStat &&
                mapEnterResponseRoundTrip.IntStat == mapEnterResponse.IntStat &&
                mapEnterResponseRoundTrip.LukStat == mapEnterResponse.LukStat &&
                mapEnterResponseRoundTrip.UnspentStatPoints == mapEnterResponse.UnspentStatPoints &&
                mapEnterResponseRoundTrip.ProgressVersion == mapEnterResponse.ProgressVersion &&
                mapEnterResponseRoundTrip.StatVersion == mapEnterResponse.StatVersion &&
                mapEnterResponseRoundTrip.FinalStr == mapEnterResponse.FinalStr &&
                mapEnterResponseRoundTrip.FinalDex == mapEnterResponse.FinalDex &&
                mapEnterResponseRoundTrip.FinalInt == mapEnterResponse.FinalInt &&
                mapEnterResponseRoundTrip.FinalLuk == mapEnterResponse.FinalLuk &&
                mapEnterResponseRoundTrip.CurrentHp == mapEnterResponse.CurrentHp &&
                mapEnterResponseRoundTrip.MaxHp == mapEnterResponse.MaxHp &&
                mapEnterResponseRoundTrip.CurrentMp == mapEnterResponse.CurrentMp &&
                mapEnterResponseRoundTrip.MaxMp == mapEnterResponse.MaxMp &&
                mapEnterResponseRoundTrip.Attack == mapEnterResponse.Attack &&
                mapEnterResponseRoundTrip.Defense == mapEnterResponse.Defense &&
                mapEnterResponseRoundTrip.MoveSpeedMilli == mapEnterResponse.MoveSpeedMilli &&
                mapEnterResponseRoundTrip.EquipmentVersion == mapEnterResponse.EquipmentVersion &&
                mapEnterResponseRoundTrip.StatRevision == mapEnterResponse.StatRevision,
            "Generated MapEnterRp values differ after round-trip.");

        var actorSpawn = new ActorSpawnNoti
        {
            EntityId = 9002,
            ActorKind = 2,
            ActorDataId = 1001,
            PositionX = 12.5f,
            PositionY = 42.75f,
            DirectionX = 0.0f,
            DirectionY = -1.0f,
            MoveSequence = 31,
            MoveState = 1,
            ServerTick = 123457,
            CurrentHp = 73,
            MaxHp = 100,
            LifeRevision = 1
        };
        byte[] actorSpawnBody = SerializeBody(actorSpawn);
        Require(ActorSpawnNoti.TryDeserializeBody(actorSpawnBody, out ActorSpawnNoti? decodedActorSpawn) &&
                decodedActorSpawn is not null,
            "Generated ActorSpawnNoti round-trip failed.");
        ActorSpawnNoti actorSpawnRoundTrip =
            decodedActorSpawn ?? throw new InvalidOperationException("Decoded ActorSpawnNoti packet is null.");
        Require(actorSpawnRoundTrip.EntityId == actorSpawn.EntityId &&
                actorSpawnRoundTrip.ActorKind == actorSpawn.ActorKind &&
                actorSpawnRoundTrip.ActorDataId == actorSpawn.ActorDataId &&
                actorSpawnRoundTrip.PositionX == actorSpawn.PositionX &&
                actorSpawnRoundTrip.PositionY == actorSpawn.PositionY &&
                actorSpawnRoundTrip.DirectionX == actorSpawn.DirectionX &&
                actorSpawnRoundTrip.DirectionY == actorSpawn.DirectionY &&
                actorSpawnRoundTrip.MoveSequence == actorSpawn.MoveSequence &&
                actorSpawnRoundTrip.MoveState == actorSpawn.MoveState &&
                actorSpawnRoundTrip.ServerTick == actorSpawn.ServerTick &&
                actorSpawnRoundTrip.CurrentHp == actorSpawn.CurrentHp &&
                actorSpawnRoundTrip.MaxHp == actorSpawn.MaxHp &&
                actorSpawnRoundTrip.LifeRevision == actorSpawn.LifeRevision,
            "Generated ActorSpawnNoti values differ after round-trip.");

        var actorDespawn = new ActorDespawnNoti
        {
            EntityId = actorSpawn.EntityId
        };
        byte[] actorDespawnBody = SerializeBody(actorDespawn);
        Require(ActorDespawnNoti.TryDeserializeBody(actorDespawnBody, out ActorDespawnNoti? decodedActorDespawn) &&
                decodedActorDespawn is not null,
            "Generated ActorDespawnNoti round-trip failed.");
        ActorDespawnNoti actorDespawnRoundTrip =
            decodedActorDespawn ?? throw new InvalidOperationException("Decoded ActorDespawnNoti packet is null.");
        Require(actorDespawnRoundTrip.EntityId == actorDespawn.EntityId,
            "Generated ActorDespawnNoti values differ after round-trip.");

        var moveRequest = new MoveRq
        {
            Sequence = 32,
            MoveState = 3,
            ClientPositionX = 128.0f,
            ClientPositionY = 72.0f,
            DirectionX = 0.6f,
            DirectionY = 0.8f
        };
        byte[] moveRequestBody = SerializeBody(moveRequest);
        Require(MoveRq.TryDeserializeBody(moveRequestBody, out MoveRq? decodedMoveRequest) && decodedMoveRequest is not null,
            "Generated MoveRq round-trip failed.");
        MoveRq moveRequestRoundTrip =
            decodedMoveRequest ?? throw new InvalidOperationException("Decoded MoveRq packet is null.");
        Require(moveRequestRoundTrip.Sequence == moveRequest.Sequence &&
                moveRequestRoundTrip.MoveState == moveRequest.MoveState &&
                moveRequestRoundTrip.ClientPositionX == moveRequest.ClientPositionX &&
                moveRequestRoundTrip.ClientPositionY == moveRequest.ClientPositionY &&
                moveRequestRoundTrip.DirectionX == moveRequest.DirectionX &&
                moveRequestRoundTrip.DirectionY == moveRequest.DirectionY,
            "Generated MoveRq values differ after round-trip.");

        var moveResponse = new MoveRp
        {
            ResultCode = 0,
            Sequence = moveRequest.Sequence,
            MoveState = moveRequest.MoveState,
            AcceptedPositionX = 127.5f,
            AcceptedPositionY = 71.5f,
            DirectionX = moveRequest.DirectionX,
            DirectionY = moveRequest.DirectionY,
            IsCorrected = true
        };
        byte[] moveResponseBody = SerializeBody(moveResponse);
        Require(MoveRp.TryDeserializeBody(moveResponseBody, out MoveRp? decodedMoveResponse) && decodedMoveResponse is not null,
            "Generated MoveRp round-trip failed.");
        MoveRp moveResponseRoundTrip =
            decodedMoveResponse ?? throw new InvalidOperationException("Decoded MoveRp packet is null.");
        Require(moveResponseRoundTrip.ResultCode == moveResponse.ResultCode &&
                moveResponseRoundTrip.Sequence == moveResponse.Sequence &&
                moveResponseRoundTrip.MoveState == moveResponse.MoveState &&
                moveResponseRoundTrip.AcceptedPositionX == moveResponse.AcceptedPositionX &&
                moveResponseRoundTrip.AcceptedPositionY == moveResponse.AcceptedPositionY &&
                moveResponseRoundTrip.DirectionX == moveResponse.DirectionX &&
                moveResponseRoundTrip.DirectionY == moveResponse.DirectionY &&
                moveResponseRoundTrip.IsCorrected == moveResponse.IsCorrected,
            "Generated MoveRp values differ after round-trip.");

        var moveNotification = new MoveNoti
        {
            EntityId = actorSpawn.EntityId,
            Sequence = moveResponse.Sequence,
            MoveState = moveResponse.MoveState,
            PositionX = moveResponse.AcceptedPositionX,
            PositionY = moveResponse.AcceptedPositionY,
            DirectionX = moveResponse.DirectionX,
            DirectionY = moveResponse.DirectionY,
            ServerTick = 123458
        };
        byte[] moveNotificationBody = SerializeBody(moveNotification);
        Require(MoveNoti.TryDeserializeBody(moveNotificationBody, out MoveNoti? decodedMoveNotification) &&
                decodedMoveNotification is not null,
            "Generated MoveNoti round-trip failed.");
        MoveNoti moveNotificationRoundTrip =
            decodedMoveNotification ?? throw new InvalidOperationException("Decoded MoveNoti packet is null.");
        Require(moveNotificationRoundTrip.EntityId == moveNotification.EntityId &&
                moveNotificationRoundTrip.Sequence == moveNotification.Sequence &&
                moveNotificationRoundTrip.MoveState == moveNotification.MoveState &&
                moveNotificationRoundTrip.PositionX == moveNotification.PositionX &&
                moveNotificationRoundTrip.PositionY == moveNotification.PositionY &&
                moveNotificationRoundTrip.DirectionX == moveNotification.DirectionX &&
                moveNotificationRoundTrip.DirectionY == moveNotification.DirectionY &&
                moveNotificationRoundTrip.ServerTick == moveNotification.ServerTick,
            "Generated MoveNoti values differ after round-trip.");

        var actorAttack = new ActorAttackNoti
        {
            AttackerEntityId = actorSpawn.EntityId,
            TargetEntityId = mapEnterResponse.EntityId,
            Damage = 17,
            TargetCurrentHp = 784,
            TargetMaxHp = 901,
            ServerTick = 123459
        };
        byte[] actorAttackBody = SerializeBody(actorAttack);
        Require(ActorAttackNoti.TryDeserializeBody(actorAttackBody, out ActorAttackNoti? decodedActorAttack) &&
                decodedActorAttack is not null,
            "Generated ActorAttackNoti round-trip failed.");
        ActorAttackNoti actorAttackRoundTrip =
            decodedActorAttack ?? throw new InvalidOperationException("Decoded ActorAttackNoti packet is null.");
        Require(actorAttackRoundTrip.AttackerEntityId == actorAttack.AttackerEntityId &&
                actorAttackRoundTrip.TargetEntityId == actorAttack.TargetEntityId &&
                actorAttackRoundTrip.Damage == actorAttack.Damage &&
                actorAttackRoundTrip.TargetCurrentHp == actorAttack.TargetCurrentHp &&
                actorAttackRoundTrip.TargetMaxHp == actorAttack.TargetMaxHp &&
                actorAttackRoundTrip.ServerTick == actorAttack.ServerTick,
            "Generated ActorAttackNoti values differ after round-trip.");

        var actorDeath = new ActorDeathNoti
        {
            EntityId = mapEnterResponse.EntityId,
            KillerEntityId = actorSpawn.EntityId,
            LifeRevision = 1,
            ServerTick = 123460
        };
        byte[] actorDeathBody = SerializeBody(actorDeath);
        Require(ActorDeathNoti.TryDeserializeBody(actorDeathBody, out ActorDeathNoti? decodedActorDeath) &&
                decodedActorDeath is not null,
            "Generated ActorDeathNoti round-trip failed.");
        ActorDeathNoti actorDeathRoundTrip =
            decodedActorDeath ?? throw new InvalidOperationException("Decoded ActorDeathNoti packet is null.");
        Require(actorDeathRoundTrip.EntityId == actorDeath.EntityId &&
                actorDeathRoundTrip.KillerEntityId == actorDeath.KillerEntityId &&
                actorDeathRoundTrip.LifeRevision == actorDeath.LifeRevision &&
                actorDeathRoundTrip.ServerTick == actorDeath.ServerTick,
            "Generated ActorDeathNoti values differ after round-trip.");

        var actorRespawn = new ActorRespawnNoti
        {
            EntityId = mapEnterResponse.EntityId,
            PositionX = 310.0f,
            PositionY = 420.0f,
            DirectionX = 0.0f,
            DirectionY = 1.0f,
            CurrentHp = 901,
            MaxHp = 901,
            LifeRevision = 2,
            ServerTick = 123461
        };
        byte[] actorRespawnBody = SerializeBody(actorRespawn);
        Require(ActorRespawnNoti.TryDeserializeBody(actorRespawnBody, out ActorRespawnNoti? decodedActorRespawn) &&
                decodedActorRespawn is not null,
            "Generated ActorRespawnNoti round-trip failed.");
        ActorRespawnNoti actorRespawnRoundTrip =
            decodedActorRespawn ?? throw new InvalidOperationException("Decoded ActorRespawnNoti packet is null.");
        Require(actorRespawnRoundTrip.EntityId == actorRespawn.EntityId &&
                actorRespawnRoundTrip.PositionX == actorRespawn.PositionX &&
                actorRespawnRoundTrip.PositionY == actorRespawn.PositionY &&
                actorRespawnRoundTrip.DirectionX == actorRespawn.DirectionX &&
                actorRespawnRoundTrip.DirectionY == actorRespawn.DirectionY &&
                actorRespawnRoundTrip.CurrentHp == actorRespawn.CurrentHp &&
                actorRespawnRoundTrip.MaxHp == actorRespawn.MaxHp &&
                actorRespawnRoundTrip.LifeRevision == actorRespawn.LifeRevision &&
                actorRespawnRoundTrip.ServerTick == actorRespawn.ServerTick,
            "Generated ActorRespawnNoti values differ after round-trip.");

        var basicAttackRequest = new BasicAttackRq
        {
            AttackSequence = 71,
            TargetEntityId = actorSpawn.EntityId
        };
        byte[] basicAttackRequestBody = SerializeBody(basicAttackRequest);
        Require(BasicAttackRq.TryDeserializeBody(basicAttackRequestBody, out BasicAttackRq? decodedBasicAttackRequest) &&
                decodedBasicAttackRequest is not null,
            "Generated BasicAttackRq round-trip failed.");
        BasicAttackRq basicAttackRequestRoundTrip =
            decodedBasicAttackRequest ?? throw new InvalidOperationException("Decoded BasicAttackRq packet is null.");
        Require(basicAttackRequestRoundTrip.AttackSequence == basicAttackRequest.AttackSequence &&
                basicAttackRequestRoundTrip.TargetEntityId == basicAttackRequest.TargetEntityId,
            "Generated BasicAttackRq values differ after round-trip.");

        var basicAttackResponse = new BasicAttackRp
        {
            ResultCode = 0,
            AttackSequence = basicAttackRequest.AttackSequence,
            ServerTick = 123462
        };
        byte[] basicAttackResponseBody = SerializeBody(basicAttackResponse);
        Require(BasicAttackRp.TryDeserializeBody(basicAttackResponseBody, out BasicAttackRp? decodedBasicAttackResponse) &&
                decodedBasicAttackResponse is not null,
            "Generated BasicAttackRp round-trip failed.");
        BasicAttackRp basicAttackResponseRoundTrip =
            decodedBasicAttackResponse ?? throw new InvalidOperationException("Decoded BasicAttackRp packet is null.");
        Require(basicAttackResponseRoundTrip.ResultCode == basicAttackResponse.ResultCode &&
                basicAttackResponseRoundTrip.AttackSequence == basicAttackResponse.AttackSequence &&
                basicAttackResponseRoundTrip.ServerTick == basicAttackResponse.ServerTick,
            "Generated BasicAttackRp values differ after round-trip.");

        Require(mapEnterRequest.PacketKind == EContentPacketKind.Request &&
                mapEnterResponse.PacketKind == EContentPacketKind.Response &&
                actorSpawn.PacketKind == EContentPacketKind.Notification &&
                actorDespawn.PacketKind == EContentPacketKind.Notification &&
                moveRequest.PacketKind == EContentPacketKind.Request &&
                moveResponse.PacketKind == EContentPacketKind.Response &&
                moveNotification.PacketKind == EContentPacketKind.Notification &&
                actorAttack.PacketKind == EContentPacketKind.Notification &&
                actorDeath.PacketKind == EContentPacketKind.Notification &&
                actorRespawn.PacketKind == EContentPacketKind.Notification &&
                basicAttackRequest.PacketKind == EContentPacketKind.Request &&
                basicAttackResponse.PacketKind == EContentPacketKind.Response,
            "Generated Map packet kinds are invalid.");
    }

    private static void TestGeneratedMapRouter()
    {
        var handler = new MapTestHandler();
        var router = new PacketRouter();
        router.SetMapHandler(handler);

        IContentPacket[] packets =
        [
            new MapEnterRq { RequestId = 41, MapDataId = 1 },
            new MapEnterRp { ResultCode = 0, RequestId = 41, MapInstanceId = 11, EntityId = 21 },
            new ActorSpawnNoti
            {
                EntityId = 22,
                ActorKind = 2,
                ActorDataId = 1001,
                MoveState = 3,
                CurrentHp = 100,
                MaxHp = 100,
                LifeRevision = 1
            },
            new ActorDespawnNoti { EntityId = 22 },
            new MoveRq { Sequence = 5, MoveState = 1, DirectionX = 1.0f },
            new MoveRp { ResultCode = 0, Sequence = 5, MoveState = 1 },
            new MoveNoti { EntityId = 22, Sequence = 5, MoveState = 1, ServerTick = 77 },
            new ActorAttackNoti
            {
                AttackerEntityId = 22,
                TargetEntityId = 21,
                Damage = 7,
                TargetCurrentHp = 93,
                TargetMaxHp = 100,
                ServerTick = 78
            },
            new ActorDeathNoti { EntityId = 21, KillerEntityId = 22, LifeRevision = 1, ServerTick = 79 },
            new ActorRespawnNoti
            {
                EntityId = 21,
                PositionX = 12.0f,
                PositionY = 34.0f,
                DirectionY = 1.0f,
                CurrentHp = 100,
                MaxHp = 100,
                LifeRevision = 2,
                ServerTick = 80
            },
            new BasicAttackRq { AttackSequence = 6, TargetEntityId = 22 },
            new BasicAttackRp { ResultCode = 0, AttackSequence = 6, ServerTick = 81 }
        ];

        foreach (IContentPacket packet in packets)
        {
            Require(router.DispatchPacket(packet.Opcode, SerializeBody(packet)),
                $"Generated global router did not dispatch Map opcode {packet.Opcode}.");
        }

        Require(handler.HandledCount == packets.Length, "Generated global router did not route every Map packet kind.");
        Require(handler.LastMapDataId == 1 && handler.LastEntityId == 21 &&
                handler.LastMoveSequence == 5 && handler.LastAttackSequence == 6 &&
                handler.LastLifeRevision == 2,
            "Generated Map handler received unexpected values.");
    }

    private static void TestGeneratedPacketFramePipeline()
    {
        var outgoing = new EchoRq
        {
            Message = "한글"
        };
        Require(outgoing.PacketKind == EContentPacketKind.Request, "Generated request packet kind is invalid.");

        var cipher = new FDefaultPacketCipher(packetKey: 0x37);
        var encoder = new FPacketFrameEncoder(cipher);
        byte[] frameBytes = encoder.Encode(outgoing, randomKey: 0x5A);

        var decoder = new FPacketStreamDecoder(cipher);
        Require(decoder.TryAppend(frameBytes.AsSpan(0, 3)), "Could not append the first frame fragment.");
        Require(decoder.TryReadFrame(out _, out _) == EFrameReadStatus.NeedMoreData,
            "A partial transport header must not produce a frame.");
        Require(decoder.TryAppend(frameBytes.AsSpan(3)), "Could not append the remaining frame bytes.");
        Require(decoder.TryReadFrame(out FDecodedContentFrame? frame, out EFrameDecodeError error) == EFrameReadStatus.FrameReady,
            $"Generated packet frame could not be decoded: {error}");
        Require(frame is not null && frame.Opcode == EchoRq.OpcodeValue, "Decoded generated packet opcode differs.");

        var handler = new EchoTestHandler();
        var router = new PacketRouter();
        router.SetEchoHandler(handler);
        Require(frame is not null && router.DispatchPacket(frame.Opcode, frame.Body.Span),
            "Decoded frame was not dispatched through the generated router.");
        Require(handler.Message == outgoing.Message, "End-to-end generated packet value differs.");
    }

    private static void Require(bool condition, string message)
    {
        if (!condition)
        {
            throw new InvalidOperationException(message);
        }
    }

    private static byte[] SerializeBody(IContentPacket packet)
    {
        var writer = new FPacketWriter();
        packet.SerializeBody(writer);
        return writer.WrittenSpan.ToArray();
    }

    private sealed class EchoTestHandler : EchoPacketHandlerBase
    {
        public string Message { get; private set; } = string.Empty;

        public override bool HandleEchoRq(EchoRq packet)
        {
            Message = packet.Message;
            return true;
        }
    }

    private sealed class MapTestHandler : MapPacketHandlerBase
    {
        public int HandledCount { get; private set; }

        public uint LastMapDataId { get; private set; }

        public ulong LastEntityId { get; private set; }

        public uint LastMoveSequence { get; private set; }

        public uint LastAttackSequence { get; private set; }

        public ulong LastLifeRevision { get; private set; }

        public override bool HandleMapEnterRq(MapEnterRq packet)
        {
            ++HandledCount;
            LastMapDataId = packet.MapDataId;
            return true;
        }

        public override bool HandleMapEnterRp(MapEnterRp packet)
        {
            ++HandledCount;
            LastEntityId = packet.EntityId;
            return true;
        }

        public override bool HandleActorSpawnNoti(ActorSpawnNoti packet)
        {
            ++HandledCount;
            LastEntityId = packet.EntityId;
            return true;
        }

        public override bool HandleActorDespawnNoti(ActorDespawnNoti packet)
        {
            ++HandledCount;
            LastEntityId = packet.EntityId;
            return true;
        }

        public override bool HandleMoveRq(MoveRq packet)
        {
            ++HandledCount;
            LastMoveSequence = packet.Sequence;
            return true;
        }

        public override bool HandleMoveRp(MoveRp packet)
        {
            ++HandledCount;
            LastMoveSequence = packet.Sequence;
            return true;
        }

        public override bool HandleMoveNoti(MoveNoti packet)
        {
            ++HandledCount;
            LastEntityId = packet.EntityId;
            LastMoveSequence = packet.Sequence;
            return true;
        }

        public override bool HandleActorAttackNoti(ActorAttackNoti packet)
        {
            ++HandledCount;
            LastEntityId = packet.AttackerEntityId;
            return true;
        }

        public override bool HandleActorDeathNoti(ActorDeathNoti packet)
        {
            ++HandledCount;
            LastEntityId = packet.EntityId;
            LastLifeRevision = packet.LifeRevision;
            return true;
        }

        public override bool HandleActorRespawnNoti(ActorRespawnNoti packet)
        {
            ++HandledCount;
            LastEntityId = packet.EntityId;
            LastLifeRevision = packet.LifeRevision;
            return true;
        }

        public override bool HandleBasicAttackRq(BasicAttackRq packet)
        {
            ++HandledCount;
            LastAttackSequence = packet.AttackSequence;
            return true;
        }

        public override bool HandleBasicAttackRp(BasicAttackRp packet)
        {
            ++HandledCount;
            LastAttackSequence = packet.AttackSequence;
            return true;
        }
    }
}
