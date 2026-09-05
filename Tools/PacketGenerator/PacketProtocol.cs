internal enum EPacketEndpointKind
{
    Request,
    Response,
    Notification,
    Broadcast
}

internal static class PacketWireContract
{
    public const ulong MaxFrameSize = 8UL * 1024UL;
    public const ulong TransportHeaderSize = sizeof(ushort) + sizeof(byte) + sizeof(byte);
    public const ulong ContentHeaderSize = sizeof(ushort);
    public const ulong MaxContentBodySize = MaxFrameSize - TransportHeaderSize - ContentHeaderSize;
}

internal sealed class PacketProtocolDocument
{
    public required string Content { get; init; }

    public required string SourcePath { get; init; }

    public required IReadOnlyList<PacketDefinition> Packets { get; init; }
}

internal sealed class PacketDefinition
{
    public required string MessageName { get; init; }

    public required string CppClassName { get; init; }

    public required string CSharpClassName { get; init; }

    public required string HandlerMethodName { get; init; }

    public required ushort Opcode { get; init; }

    public required EPacketEndpointKind Kind { get; init; }

    public required IReadOnlyList<PacketFieldDefinition> Fields { get; init; }
}

internal sealed class PacketFieldDefinition
{
    public required string SchemaName { get; init; }

    public required string CSharpName { get; init; }

    public required SchemaTypeNode Type { get; init; }
}

internal static class PacketProtocolBuilder
{
    public static PacketProtocolDocument Build(PacketSchemaDocument schema, string sourcePath)
    {
        var packets = new List<PacketDefinition>();
        foreach (PacketSchemaMessage message in schema.Messages)
        {
            AddPacket(packets, message.Name, message.Rq, EPacketEndpointKind.Request);
            AddPacket(packets, message.Name, message.Rp, EPacketEndpointKind.Response);
            AddPacket(packets, message.Name, message.Noti, EPacketEndpointKind.Notification);
            AddPacket(packets, message.Name, message.Broadcast, EPacketEndpointKind.Broadcast);
        }

        return new PacketProtocolDocument
        {
            Content = schema.Content,
            SourcePath = sourcePath,
            Packets = packets
        };
    }

    private static void AddPacket(
        List<PacketDefinition> packets,
        string messageName,
        PacketSchemaEndpoint? endpoint,
        EPacketEndpointKind kind)
    {
        if (endpoint == null)
        {
            return;
        }

        packets.Add(new PacketDefinition
        {
            MessageName = messageName,
            CppClassName = PacketNaming.BuildCppClassName(messageName, kind),
            CSharpClassName = PacketNaming.BuildCSharpClassName(messageName, kind),
            HandlerMethodName = PacketNaming.BuildHandlerMethodName(messageName, kind),
            Opcode = checked((ushort)endpoint.Opcode),
            Kind = kind,
            Fields = endpoint.Fields.Select(field => new PacketFieldDefinition
            {
                SchemaName = field.Name,
                CSharpName = PacketNaming.ToCSharpIdentifier(field.Name),
                Type = SchemaTypeParser.Parse(field.Type)
            }).ToArray()
        });
    }
}

internal static class PacketProtocolSetValidator
{
    public static void Validate(IReadOnlyList<PacketProtocolDocument> protocols)
    {
        var usedContentNames = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var usedOpcodes = new Dictionary<ushort, string>();

        foreach (PacketProtocolDocument protocol in protocols)
        {
            if (!usedContentNames.Add(protocol.Content))
            {
                throw new InvalidOperationException($"Duplicate content name in schema set: {protocol.Content}");
            }

            foreach (PacketDefinition packet in protocol.Packets)
            {
                string packetName = $"{protocol.Content}.{packet.CppClassName}";
                if (!usedOpcodes.TryAdd(packet.Opcode, packetName))
                {
                    throw new InvalidOperationException(
                        $"Duplicate opcode {packet.Opcode} across schema set: {usedOpcodes[packet.Opcode]} and {packetName}");
                }
            }
        }
    }
}

internal static class PacketNaming
{
    public static string BuildCppClassName(string messageName, EPacketEndpointKind kind)
    {
        return kind == EPacketEndpointKind.Broadcast
            ? $"F{messageName}"
            : $"F{messageName}{GetEndpointSuffix(kind)}";
    }

    public static string BuildCSharpClassName(string messageName, EPacketEndpointKind kind)
    {
        return kind == EPacketEndpointKind.Broadcast
            ? messageName
            : $"{messageName}{GetEndpointSuffix(kind)}";
    }

    public static string BuildHandlerMethodName(string messageName, EPacketEndpointKind kind)
    {
        return kind == EPacketEndpointKind.Broadcast
            ? $"Handle{messageName}"
            : $"Handle{messageName}{GetEndpointSuffix(kind)}";
    }

    public static string GetEndpointSuffix(EPacketEndpointKind kind)
    {
        return kind switch
        {
            EPacketEndpointKind.Request => "Rq",
            EPacketEndpointKind.Response => "Rp",
            EPacketEndpointKind.Notification => "Noti",
            EPacketEndpointKind.Broadcast => "Broadcast",
            _ => throw new ArgumentOutOfRangeException(nameof(kind), kind, null)
        };
    }

    public static string GetPacketKindName(EPacketEndpointKind kind)
    {
        return kind switch
        {
            EPacketEndpointKind.Request => "Request",
            EPacketEndpointKind.Response => "Response",
            EPacketEndpointKind.Notification => "Notification",
            EPacketEndpointKind.Broadcast => "Broadcast",
            _ => throw new ArgumentOutOfRangeException(nameof(kind), kind, null)
        };
    }

    public static string ToCSharpIdentifier(string schemaName)
    {
        var characters = new List<char>(schemaName.Length);
        bool uppercaseNext = true;
        foreach (char character in schemaName)
        {
            if (character == '_')
            {
                uppercaseNext = true;
                continue;
            }

            characters.Add(uppercaseNext ? char.ToUpperInvariant(character) : character);
            uppercaseNext = false;
        }

        return new string(characters.ToArray());
    }

    public static string ToMemberName(string contentName)
    {
        return string.IsNullOrEmpty(contentName)
            ? "content"
            : char.ToLowerInvariant(contentName[0]) + contentName[1..];
    }

    public static string ToCppAccessorSuffix(string fieldName)
    {
        return string.IsNullOrEmpty(fieldName)
            ? "Field"
            : char.ToUpperInvariant(fieldName[0]) + fieldName[1..];
    }
}
