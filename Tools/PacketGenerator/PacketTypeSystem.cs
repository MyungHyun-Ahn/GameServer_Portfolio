internal enum ESchemaTypeArgumentKind
{
    Type,
    Literal
}

internal sealed class SchemaTypeNode
{
    public required string Name { get; init; }

    public required string OriginalText { get; init; }

    public List<SchemaTypeNode> TypeArguments { get; } = [];

    public List<string> LiteralArguments { get; } = [];

    public List<ESchemaTypeArgumentKind> ArgumentKinds { get; } = [];
}

internal static class SchemaTypeParser
{
    public static SchemaTypeNode Parse(string text)
    {
        int index = 0;
        SchemaTypeNode typeNode = ParseType(text, ref index);
        SkipWhitespace(text, ref index);
        if (index != text.Length)
        {
            throw new InvalidOperationException($"Unexpected token in schema type: {text}");
        }

        return typeNode;
    }

    private static SchemaTypeNode ParseType(string text, ref int index)
    {
        SkipWhitespace(text, ref index);
        string name = ParseIdentifier(text, ref index);
        var node = new SchemaTypeNode
        {
            Name = name,
            OriginalText = text
        };

        SkipWhitespace(text, ref index);
        if (index >= text.Length || text[index] != '<')
        {
            return node;
        }

        ++index;
        while (true)
        {
            SkipWhitespace(text, ref index);
            if (index < text.Length && char.IsDigit(text[index]))
            {
                node.LiteralArguments.Add(ParseLiteral(text, ref index));
                node.ArgumentKinds.Add(ESchemaTypeArgumentKind.Literal);
            }
            else
            {
                node.TypeArguments.Add(ParseType(text, ref index));
                node.ArgumentKinds.Add(ESchemaTypeArgumentKind.Type);
            }

            SkipWhitespace(text, ref index);
            if (index >= text.Length)
            {
                throw new InvalidOperationException($"Unclosed generic argument list: {text}");
            }

            if (text[index] == '>')
            {
                ++index;
                break;
            }

            if (text[index] != ',')
            {
                throw new InvalidOperationException($"Expected ',' or '>' in schema type: {text}");
            }

            ++index;
        }

        return node;
    }

    private static string ParseIdentifier(string text, ref int index)
    {
        int start = index;
        while (index < text.Length && (char.IsLetterOrDigit(text[index]) || text[index] == '_'))
        {
            ++index;
        }

        if (start == index)
        {
            throw new InvalidOperationException($"Identifier expected in schema type: {text}");
        }

        return text[start..index];
    }

    private static string ParseLiteral(string text, ref int index)
    {
        int start = index;
        while (index < text.Length && char.IsDigit(text[index]))
        {
            ++index;
        }

        return text[start..index];
    }

    private static void SkipWhitespace(string text, ref int index)
    {
        while (index < text.Length && char.IsWhiteSpace(text[index]))
        {
            ++index;
        }
    }
}

internal static class PacketTypeRules
{
    private static readonly HashSet<string> ScalarTypes = new(StringComparer.Ordinal)
    {
        "bool",
        "int8",
        "uint8",
        "int16",
        "uint16",
        "int32",
        "uint32",
        "int64",
        "uint64",
        "float",
        "double",
        "string",
        "string_view",
        "bytes",
        "bytes_view"
    };

    public static void Validate(SchemaTypeNode type, string fieldPath, string schemaPath)
    {
        if (ScalarTypes.Contains(type.Name))
        {
            RequireArguments(type, [], fieldPath, schemaPath);
            return;
        }

        switch (type.Name)
        {
            case "vector":
                RequireArguments(type, [ESchemaTypeArgumentKind.Type], fieldPath, schemaPath);
                Validate(type.TypeArguments[0], fieldPath, schemaPath);
                return;

            case "array":
                RequireArguments(type, [ESchemaTypeArgumentKind.Type, ESchemaTypeArgumentKind.Literal], fieldPath, schemaPath);
                Validate(type.TypeArguments[0], fieldPath, schemaPath);
                if (!int.TryParse(type.LiteralArguments[0], out int arrayLength) || arrayLength <= 0)
                {
                    throw new InvalidOperationException(
                        $"Fixed array length must be a positive Int32 in '{fieldPath}': {schemaPath}");
                }

                return;

            case "map":
            case "unordered_map":
                RequireArguments(type, [ESchemaTypeArgumentKind.Type, ESchemaTypeArgumentKind.Type], fieldPath, schemaPath);
                Validate(type.TypeArguments[0], fieldPath, schemaPath);
                Validate(type.TypeArguments[1], fieldPath, schemaPath);
                return;

            default:
                throw new InvalidOperationException(
                    $"Unsupported schema type '{type.OriginalText}' in '{fieldPath}': {schemaPath}");
        }
    }

    public static void ValidateCSharpMvp(SchemaTypeNode type, string fieldPath, string schemaPath)
    {
        if (type.Name is "map" or "unordered_map")
        {
            throw new InvalidOperationException(
                $"C# packet generation does not support '{type.Name}' yet in '{fieldPath}': {schemaPath}");
        }

        if (type.Name == "vector" && type.TypeArguments[0].Name == "bool")
        {
            throw new InvalidOperationException(
                $"C# packet generation does not support 'vector<bool>' in '{fieldPath}': {schemaPath}");
        }

        foreach (SchemaTypeNode childType in type.TypeArguments)
        {
            ValidateCSharpMvp(childType, fieldPath, schemaPath);
        }
    }

    public static bool ContainsBorrowedView(SchemaTypeNode type)
    {
        if (type.Name is "string_view" or "bytes_view")
        {
            return true;
        }

        return type.TypeArguments.Any(ContainsBorrowedView);
    }

    public static ulong GetMinimumSerializedSize(SchemaTypeNode type)
    {
        return type.Name switch
        {
            "bool" or "int8" or "uint8" => 1,
            "int16" or "uint16" => 2,
            "int32" or "uint32" or "float" => 4,
            "int64" or "uint64" or "double" => 8,
            "string" or "string_view" or "bytes" or "bytes_view" or "vector" or "map" or "unordered_map" => 4,
            "array" => checked(
                ulong.Parse(type.LiteralArguments[0]) * GetMinimumSerializedSize(type.TypeArguments[0])),
            _ => throw new InvalidOperationException($"Unsupported minimum size type: {type.OriginalText}")
        };
    }

    private static void RequireArguments(
        SchemaTypeNode type,
        IReadOnlyList<ESchemaTypeArgumentKind> expectedKinds,
        string fieldPath,
        string schemaPath)
    {
        if (!type.ArgumentKinds.SequenceEqual(expectedKinds))
        {
            string expected = expectedKinds.Count == 0
                ? "no arguments"
                : string.Join(", ", expectedKinds.Select(kind => kind.ToString().ToLowerInvariant()));
            throw new InvalidOperationException(
                $"Schema type '{type.OriginalText}' has invalid arguments in '{fieldPath}'; expected {expected}: {schemaPath}");
        }
    }
}

internal static class PacketTypeMapping
{
    private static readonly Dictionary<string, string> CppScalarTypes = new(StringComparer.Ordinal)
    {
        ["bool"] = "bool",
        ["int8"] = "std::int8_t",
        ["uint8"] = "std::uint8_t",
        ["int16"] = "std::int16_t",
        ["uint16"] = "std::uint16_t",
        ["int32"] = "std::int32_t",
        ["uint32"] = "std::uint32_t",
        ["int64"] = "std::int64_t",
        ["uint64"] = "std::uint64_t",
        ["float"] = "float",
        ["double"] = "double",
        ["string"] = "std::string",
        ["string_view"] = "std::string_view",
        ["bytes"] = "std::vector<std::uint8_t>",
        ["bytes_view"] = "std::span<const std::uint8_t>"
    };

    private static readonly Dictionary<string, string> CSharpScalarTypes = new(StringComparer.Ordinal)
    {
        ["bool"] = "bool",
        ["int8"] = "sbyte",
        ["uint8"] = "byte",
        ["int16"] = "short",
        ["uint16"] = "ushort",
        ["int32"] = "int",
        ["uint32"] = "uint",
        ["int64"] = "long",
        ["uint64"] = "ulong",
        ["float"] = "float",
        ["double"] = "double",
        ["string"] = "string",
        ["string_view"] = "string",
        ["bytes"] = "byte[]",
        ["bytes_view"] = "byte[]"
    };

    public static string RenderCppType(SchemaTypeNode type)
    {
        if (CppScalarTypes.TryGetValue(type.Name, out string? scalarType))
        {
            return scalarType;
        }

        return type.Name switch
        {
            "vector" => $"std::vector<{RenderCppType(type.TypeArguments[0])}>",
            "array" => $"std::array<{RenderCppType(type.TypeArguments[0])}, {type.LiteralArguments[0]}>",
            "map" => $"std::map<{RenderCppType(type.TypeArguments[0])}, {RenderCppType(type.TypeArguments[1])}>",
            "unordered_map" => $"std::unordered_map<{RenderCppType(type.TypeArguments[0])}, {RenderCppType(type.TypeArguments[1])}>",
            _ => throw new InvalidOperationException($"Unsupported C++ schema type: {type.OriginalText}")
        };
    }

    public static string RenderCSharpType(SchemaTypeNode type)
    {
        if (CSharpScalarTypes.TryGetValue(type.Name, out string? scalarType))
        {
            return scalarType;
        }

        return type.Name switch
        {
            "vector" => $"List<{RenderCSharpType(type.TypeArguments[0])}>",
            "array" => $"{RenderCSharpType(type.TypeArguments[0])}[]",
            _ => throw new InvalidOperationException($"Unsupported C# schema type: {type.OriginalText}")
        };
    }

    public static string RenderCSharpInitializer(SchemaTypeNode type)
    {
        return type.Name switch
        {
            "string" or "string_view" => " = string.Empty;",
            "bytes" or "bytes_view" => " = Array.Empty<byte>();",
            "vector" => " = [];",
            "array" => $" = {RenderCSharpArrayAllocation(type, initializeElements: true)};",
            _ => string.Empty
        };
    }

    public static string RenderCSharpArrayAllocation(
        SchemaTypeNode type,
        bool initializeElements = false)
    {
        if (type.Name != "array")
        {
            throw new InvalidOperationException($"Expected a fixed array type: {type.OriginalText}");
        }

        SchemaTypeNode elementType = type.TypeArguments[0];
        string arrayType = RenderCSharpType(type);
        int firstArraySuffix = arrayType.IndexOf("[]", StringComparison.Ordinal);
        if (firstArraySuffix < 0)
        {
            throw new InvalidOperationException($"Failed to render a C# fixed array type: {type.OriginalText}");
        }

        if (initializeElements && RequiresExplicitCSharpDefault(elementType))
        {
            return $"CreateFixedArray<{RenderCSharpType(elementType)}>({type.LiteralArguments[0]}, " +
                $"static () => {RenderExplicitCSharpDefault(elementType)})";
        }

        return $"new {arrayType[..firstArraySuffix]}[{type.LiteralArguments[0]}]{arrayType[(firstArraySuffix + 2)..]}";
    }

    public static bool RequiresFixedArrayFactory(SchemaTypeNode type)
    {
        return type.Name == "array" && RequiresExplicitCSharpDefault(type.TypeArguments[0]);
    }

    private static bool RequiresExplicitCSharpDefault(SchemaTypeNode type)
    {
        return type.Name is "string" or "string_view" or "bytes" or "bytes_view" or "vector" or "array";
    }

    private static string RenderExplicitCSharpDefault(SchemaTypeNode type)
    {
        return type.Name switch
        {
            "string" or "string_view" => "string.Empty",
            "bytes" or "bytes_view" => "Array.Empty<byte>()",
            "vector" => $"new {RenderCSharpType(type)}()",
            "array" => RenderCSharpArrayAllocation(type, initializeElements: true),
            _ => throw new InvalidOperationException($"Type does not require an explicit C# default: {type.OriginalText}")
        };
    }
}
