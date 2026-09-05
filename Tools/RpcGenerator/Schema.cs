internal sealed class RpcSchemaDocument
{
    public int SchemaVersion { get; set; }

    public string Namespace { get; set; } = string.Empty;

    public string Output { get; set; } = string.Empty;

    public RpcServiceSchema Service { get; set; } = new();

    public List<RpcAliasSchema> Aliases { get; set; } = [];

    public List<RpcEnumSchema> Enums { get; set; } = [];

    public List<RpcStructSchema> Structs { get; set; } = [];

    public List<RpcMethodSchema> Methods { get; set; } = [];

    public string SourcePath { get; set; } = string.Empty;
}

internal sealed class RpcServiceSchema
{
    public string Name { get; set; } = string.Empty;

    public uint Id { get; set; }

    public List<uint> ReservedMethodIds { get; set; } = [];
}

internal sealed class RpcAliasSchema
{
    public string Name { get; set; } = string.Empty;

    public string Type { get; set; } = string.Empty;
}

internal sealed class RpcEnumSchema
{
    public string Name { get; set; } = string.Empty;

    public string Underlying { get; set; } = string.Empty;

    public List<RpcEnumValueSchema> Values { get; set; } = [];
}

internal sealed class RpcEnumValueSchema
{
    public string Name { get; set; } = string.Empty;

    public long Value { get; set; }
}

internal sealed class RpcStructSchema
{
    public string Name { get; set; } = string.Empty;

    public List<RpcFieldSchema> Fields { get; set; } = [];
}

internal sealed class RpcMethodSchema
{
    public string Name { get; set; } = string.Empty;

    public uint Id { get; set; }

    public string? RoutingKey { get; set; }

    public RpcEndpointSchema? Request { get; set; }

    public RpcEndpointSchema? Response { get; set; }

    public RpcEndpointSchema? Noti { get; set; }
}

internal sealed class RpcEndpointSchema
{
    public List<RpcFieldSchema> Fields { get; set; } = [];
}

internal sealed class RpcFieldSchema
{
    public string Name { get; set; } = string.Empty;

    public string Type { get; set; } = string.Empty;
}

internal sealed class RpcGeneratorOptions
{
    public required string SchemaRoot { get; init; }

    public required string OutputRoot { get; init; }

    public required string LockFile { get; init; }

    public bool Check { get; init; }
}

internal sealed class RpcGeneratedFile
{
    public required string RelativePath { get; init; }

    public required string Content { get; init; }
}

internal sealed class RpcGeneratorManifest
{
    public int Version { get; set; } = 1;

    public List<string> Files { get; set; } = [];
}

internal sealed class RpcSchemaLock
{
    public int Version { get; set; } = 1;

    public List<RpcLockedMethod> Methods { get; set; } = [];
}

internal sealed class RpcLockedMethod
{
    public uint ServiceId { get; set; }

    public uint MethodId { get; set; }

    public string Service { get; set; } = string.Empty;

    public string Method { get; set; } = string.Empty;

    public string Signature { get; set; } = string.Empty;

    public bool Reserved { get; set; }
}
