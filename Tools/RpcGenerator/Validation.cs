using System.Text.RegularExpressions;

internal static partial class RpcSchemaValidator
{
    private static readonly HashSet<string> IntegerTypes = new(StringComparer.Ordinal)
    {
        "int8", "uint8", "int16", "uint16", "int32", "uint32", "int64", "uint64",
    };

    private static readonly HashSet<string> CppReservedIdentifiers = new(StringComparer.Ordinal)
    {
        "alignas", "alignof", "and", "and_eq", "asm", "auto", "bitand", "bitor", "bool", "break", "case", "catch",
        "char", "char8_t", "char16_t", "char32_t", "class", "compl", "concept", "const", "consteval", "constexpr",
        "constinit", "const_cast", "continue", "co_await", "co_return", "co_yield", "decltype", "default", "delete", "do",
        "double", "dynamic_cast", "else", "enum", "explicit", "export", "extern", "false", "final", "float", "for",
        "friend", "goto", "if", "import", "inline", "int", "long", "module", "mutable", "namespace", "new", "noexcept",
        "not", "not_eq", "nullptr", "operator", "or", "or_eq", "override", "private", "protected", "public", "register",
        "reinterpret_cast", "requires", "return", "short", "signed", "sizeof", "static", "static_assert", "static_cast",
        "struct", "switch", "template", "this", "thread_local", "throw", "true", "try", "typedef", "typeid", "typename",
        "union", "unsigned", "using", "virtual", "void", "volatile", "wchar_t", "while", "xor", "xor_eq",
    };

    public static void ValidateSet(IReadOnlyList<RpcSchemaDocument> documents)
    {
        var serviceIds = new HashSet<uint>();
        var serviceNames = new HashSet<string>(StringComparer.Ordinal);
        var outputs = new HashSet<string>(StringComparer.OrdinalIgnoreCase);

        foreach (RpcSchemaDocument document in documents)
        {
            ValidateDocument(document);
            if (!serviceIds.Add(document.Service.Id))
            {
                throw Error(document, $"Duplicate global service id: {document.Service.Id}");
            }

            if (!serviceNames.Add(document.Service.Name))
            {
                throw Error(document, $"Duplicate global service name: {document.Service.Name}");
            }

            string normalizedOutput = document.Output.Replace('\\', '/');
            if (!outputs.Add(normalizedOutput))
            {
                throw Error(document, $"Duplicate generated output: {document.Output}");
            }
        }
    }

    public static RpcTypeRegistry CreateTypeRegistry(RpcSchemaDocument document)
    {
        return new RpcTypeRegistry(document);
    }

    private static void ValidateDocument(RpcSchemaDocument document)
    {
        if (document.SchemaVersion != 1)
        {
            throw Error(document, $"Unsupported schema-version {document.SchemaVersion}; expected 1");
        }

        ValidateNamespace(document.Namespace, document, "namespace");
        ValidateOutputPath(document.Output, document);
        ValidateIdentifier(document.Service.Name, document, "service.name");
        if (document.Service.Id == 0)
        {
            throw Error(document, "service.id must be greater than zero");
        }

        if (document.Methods.Count == 0)
        {
            throw Error(document, "methods must contain at least one method");
        }

        var declaredNames = new HashSet<string>(StringComparer.Ordinal);
        foreach (RpcAliasSchema alias in document.Aliases)
        {
            ValidateIdentifier(alias.Name, document, "alias.name");
            EnsureUniqueDeclaredName(alias.Name, "alias", declaredNames, document);
            RequireText(alias.Type, document, $"alias '{alias.Name}' type");
        }

        foreach (RpcEnumSchema enumSchema in document.Enums)
        {
            ValidateIdentifier(enumSchema.Name, document, "enum.name");
            EnsureUniqueDeclaredName(enumSchema.Name, "enum", declaredNames, document);
            if (!IntegerTypes.Contains(enumSchema.Underlying))
            {
                throw Error(document, $"Enum '{enumSchema.Name}' has unsupported underlying type '{enumSchema.Underlying}'");
            }

            if (enumSchema.Values.Count == 0)
            {
                throw Error(document, $"Enum '{enumSchema.Name}' must contain at least one value");
            }

            var valueNames = new HashSet<string>(StringComparer.Ordinal);
            var values = new HashSet<long>();
            foreach (RpcEnumValueSchema value in enumSchema.Values)
            {
                ValidateIdentifier(value.Name, document, $"enum '{enumSchema.Name}' value");
                if (!valueNames.Add(value.Name))
                {
                    throw Error(document, $"Enum '{enumSchema.Name}' contains duplicate value name '{value.Name}'");
                }

                if (!values.Add(value.Value))
                {
                    throw Error(document, $"Enum '{enumSchema.Name}' contains duplicate numeric value {value.Value}");
                }

                ValidateEnumRange(enumSchema, value, document);
            }
        }

        foreach (RpcStructSchema structSchema in document.Structs)
        {
            ValidateIdentifier(structSchema.Name, document, "struct.name");
            EnsureUniqueDeclaredName(structSchema.Name, "struct", declaredNames, document);
            ValidateFields(structSchema.Fields, document, $"struct '{structSchema.Name}'");
        }

        var registry = new RpcTypeRegistry(document);
        registry.ValidateAllDeclarations();

        var methodNames = new HashSet<string>(StringComparer.Ordinal);
        var methodIds = new HashSet<uint>();
        var reservedIds = new HashSet<uint>();
        foreach (uint reservedId in document.Service.ReservedMethodIds)
        {
            if (reservedId == 0 || !reservedIds.Add(reservedId))
            {
                throw Error(document, $"Invalid or duplicate reserved method id: {reservedId}");
            }
        }

        foreach (RpcMethodSchema method in document.Methods)
        {
            ValidateIdentifier(method.Name, document, "method.name");
            if (!methodNames.Add(method.Name))
            {
                throw Error(document, $"Duplicate method name: {method.Name}");
            }

            if (method.Id == 0 || !methodIds.Add(method.Id))
            {
                throw Error(document, $"Invalid or duplicate method id: {method.Id}");
            }

            if (reservedIds.Contains(method.Id))
            {
                throw Error(document, $"Method '{method.Name}' uses reserved id {method.Id}");
            }

            bool hasRequest = method.Request != null;
            bool hasResponse = method.Response != null;
            bool hasNoti = method.Noti != null;
            if (hasRequest && declaredNames.Contains($"F{method.Name}Rpc"))
            {
                throw Error(document, $"Method '{method.Name}' descriptor collides with declared type 'F{method.Name}Rpc'");
            }

            if (hasNoti && declaredNames.Contains($"F{method.Name}Noti"))
            {
                throw Error(document, $"Method '{method.Name}' descriptor collides with declared type 'F{method.Name}Noti'");
            }

            if (hasRequest != hasResponse)
            {
                throw Error(document, $"Method '{method.Name}' must define request and response together");
            }

            if (!hasRequest && !hasNoti)
            {
                throw Error(document, $"Method '{method.Name}' must be request/response, noti, or both");
            }

            ValidateEndpoint(method.Request, registry, document, method.Name, "request");
            ValidateEndpoint(method.Response, registry, document, method.Name, "response");
            ValidateEndpoint(method.Noti, registry, document, method.Name, "noti");
            ValidateRoutingKey(method, registry, document);
        }
    }

    private static void ValidateEndpoint(
        RpcEndpointSchema? endpoint,
        RpcTypeRegistry registry,
        RpcSchemaDocument document,
        string methodName,
        string endpointName)
    {
        if (endpoint == null)
        {
            return;
        }

        ValidateFields(endpoint.Fields, document, $"method '{methodName}' {endpointName}");
        foreach (RpcFieldSchema field in endpoint.Fields)
        {
            registry.Resolve(field.Type);
        }
    }

    private static void ValidateRoutingKey(RpcMethodSchema method, RpcTypeRegistry registry, RpcSchemaDocument document)
    {
        if (string.IsNullOrWhiteSpace(method.RoutingKey))
        {
            method.RoutingKey = null;
            return;
        }

        ValidateIdentifier(method.RoutingKey, document, $"method '{method.Name}' routing-key");
        if (method.Request != null)
        {
            ValidateRoutingField(method, method.Request, "request", registry, document);
        }

        if (method.Noti != null)
        {
            ValidateRoutingField(method, method.Noti, "noti", registry, document);
        }
    }

    private static void ValidateRoutingField(
        RpcMethodSchema method,
        RpcEndpointSchema endpoint,
        string endpointName,
        RpcTypeRegistry registry,
        RpcSchemaDocument document)
    {
        RpcFieldSchema? field = endpoint.Fields.FirstOrDefault(
            candidate => string.Equals(candidate.Name, method.RoutingKey, StringComparison.Ordinal));
        if (field == null)
        {
            throw Error(document, $"Method '{method.Name}' routing-key '{method.RoutingKey}' is missing from {endpointName}");
        }

        string scalar = registry.ResolveWireScalar(field.Type);
        if (scalar is not ("uint8" or "uint16" or "uint32" or "uint64"))
        {
            throw Error(document, $"Method '{method.Name}' routing-key must be an unsigned integer, not '{field.Type}'");
        }
    }

    private static void ValidateFields(IReadOnlyList<RpcFieldSchema> fields, RpcSchemaDocument document, string owner)
    {
        var names = new HashSet<string>(StringComparer.Ordinal);
        foreach (RpcFieldSchema field in fields)
        {
            ValidateIdentifier(field.Name, document, $"{owner} field");
            if (field.Name is "Serialize" or "Deserialize")
            {
                throw Error(document, $"{owner} field '{field.Name}' conflicts with generated RPC member functions");
            }
            RequireText(field.Type, document, $"{owner} field '{field.Name}' type");
            if (!names.Add(field.Name))
            {
                throw Error(document, $"{owner} contains duplicate field '{field.Name}'");
            }
        }
    }

    private static void ValidateEnumRange(RpcEnumSchema enumSchema, RpcEnumValueSchema value, RpcSchemaDocument document)
    {
        (long minimum, ulong maximum) = enumSchema.Underlying switch
        {
            "int8" => ((long)sbyte.MinValue, (ulong)sbyte.MaxValue),
            "uint8" => (0L, (ulong)byte.MaxValue),
            "int16" => ((long)short.MinValue, (ulong)short.MaxValue),
            "uint16" => (0L, (ulong)ushort.MaxValue),
            "int32" => ((long)int.MinValue, (ulong)int.MaxValue),
            "uint32" => (0L, (ulong)uint.MaxValue),
            "int64" => (long.MinValue, (ulong)long.MaxValue),
            "uint64" => (0L, ulong.MaxValue),
            _ => throw new InvalidOperationException("Enum underlying type was not validated."),
        };

        if (value.Value < minimum || (value.Value >= 0 && (ulong)value.Value > maximum))
        {
            throw Error(document, $"Enum '{enumSchema.Name}' value '{value.Name}' is outside {enumSchema.Underlying}");
        }
    }

    private static void ValidateOutputPath(string output, RpcSchemaDocument document)
    {
        RequireText(output, document, "output");
        if (Path.IsPathRooted(output) || !string.Equals(Path.GetExtension(output), ".h", StringComparison.OrdinalIgnoreCase))
        {
            throw Error(document, $"output must be a relative .h path: {output}");
        }

        string normalized = output.Replace('\\', '/');
        if (normalized.Split('/').Any(part => part is "" or "." or ".."))
        {
            throw Error(document, $"output contains an unsafe path segment: {output}");
        }
    }

    private static void ValidateNamespace(string value, RpcSchemaDocument document, string owner)
    {
        RequireText(value, document, owner);
        string[] components = value.Split("::", StringSplitOptions.None);
        if (components.Length == 0 || components.Any(component => !IsValidCppIdentifier(component)))
        {
            throw Error(document, $"Invalid C++ namespace: {value}");
        }
    }

    private static void ValidateIdentifier(string value, RpcSchemaDocument document, string owner)
    {
        if (!IsValidCppIdentifier(value))
        {
            throw Error(document, $"Invalid C++ identifier for {owner}: '{value}'");
        }
    }

    private static bool IsValidCppIdentifier(string value)
    {
        return !string.IsNullOrWhiteSpace(value) && CppIdentifierRegex().IsMatch(value) &&
               !CppReservedIdentifiers.Contains(value) && !value.StartsWith('_') && !value.Contains("__", StringComparison.Ordinal);
    }

    private static void RequireText(string value, RpcSchemaDocument document, string owner)
    {
        if (string.IsNullOrWhiteSpace(value))
        {
            throw Error(document, $"{owner} must not be empty");
        }
    }

    private static void EnsureUniqueDeclaredName(
        string name,
        string kind,
        HashSet<string> names,
        RpcSchemaDocument document)
    {
        if (!names.Add(name))
        {
            throw Error(document, $"Duplicate declared type name '{name}' ({kind})");
        }
    }

    private static InvalidOperationException Error(RpcSchemaDocument document, string message)
    {
        return new InvalidOperationException($"{message}: {document.SourcePath}");
    }

    [GeneratedRegex("^[A-Za-z_][A-Za-z0-9_]*$", RegexOptions.CultureInvariant)]
    private static partial Regex CppIdentifierRegex();
}

internal sealed class RpcTypeRegistry
{
    private static readonly Dictionary<string, string> CppScalars = new(StringComparer.Ordinal)
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
        ["bytes"] = "std::vector<std::uint8_t>",
    };

    private readonly RpcSchemaDocument _document;
    private readonly Dictionary<string, RpcAliasSchema> _aliases;
    private readonly Dictionary<string, RpcEnumSchema> _enums;
    private readonly Dictionary<string, RpcStructSchema> _structs;

    public RpcTypeRegistry(RpcSchemaDocument document)
    {
        _document = document;
        _aliases = document.Aliases.ToDictionary(item => item.Name, StringComparer.Ordinal);
        _enums = document.Enums.ToDictionary(item => item.Name, StringComparer.Ordinal);
        _structs = document.Structs.ToDictionary(item => item.Name, StringComparer.Ordinal);
    }

    public void ValidateAllDeclarations()
    {
        foreach (RpcAliasSchema alias in _document.Aliases)
        {
            _ = Resolve(alias.Type, [alias.Name]);
        }

        foreach (RpcStructSchema structSchema in _document.Structs)
        {
            foreach (RpcFieldSchema field in structSchema.Fields)
            {
                _ = Resolve(field.Type, [structSchema.Name]);
            }
        }
    }

    public RpcTypeNode Resolve(string text)
    {
        return Resolve(text, []);
    }

    public string RenderCpp(string text)
    {
        return RenderCpp(Resolve(text));
    }

    public string Canonicalize(string text)
    {
        return Canonicalize(Resolve(text), []);
    }

    public string ResolveWireScalar(string text)
    {
        RpcTypeNode node = Resolve(text);
        var visited = new HashSet<string>(StringComparer.Ordinal);
        while (_aliases.TryGetValue(node.Name, out RpcAliasSchema? alias) && node.TypeArguments.Count == 0)
        {
            if (!visited.Add(alias.Name))
            {
                throw Error($"Cyclic alias detected at '{alias.Name}'");
            }

            node = Resolve(alias.Type);
        }

        return node.TypeArguments.Count == 0 ? node.Name : string.Empty;
    }

    private RpcTypeNode Resolve(string text, IReadOnlyCollection<string> resolving)
    {
        RpcTypeNode node = RpcTypeParser.Parse(text);
        ValidateNode(node, resolving);
        return node;
    }

    private void ValidateNode(RpcTypeNode node, IReadOnlyCollection<string> resolving)
    {
        if (node.Name is "string_view" or "bytes_view")
        {
            throw Error($"Borrowed RPC type '{node.Name}' is forbidden; use owned string or bytes");
        }

        if (CppScalars.ContainsKey(node.Name))
        {
            if (node.TypeArguments.Count != 0)
            {
                throw Error($"Scalar type '{node.Name}' cannot have type arguments");
            }

            return;
        }

        if (node.Name == "vector")
        {
            if (node.TypeArguments.Count != 1)
            {
                throw Error($"vector requires exactly one type argument: {node.OriginalText}");
            }

            ValidateNode(node.TypeArguments[0], resolving);
            return;
        }

        if (node.TypeArguments.Count != 0)
        {
            throw Error($"Custom type cannot have type arguments: {node.OriginalText}");
        }

        if (_enums.ContainsKey(node.Name))
        {
            return;
        }

        if (_aliases.TryGetValue(node.Name, out RpcAliasSchema? alias))
        {
            EnsureNotResolving(node.Name, resolving);
            Resolve(alias.Type, resolving.Append(node.Name).ToArray());
            return;
        }

        if (_structs.TryGetValue(node.Name, out RpcStructSchema? structSchema))
        {
            EnsureNotResolving(node.Name, resolving);
            string[] next = resolving.Append(node.Name).ToArray();
            foreach (RpcFieldSchema field in structSchema.Fields)
            {
                Resolve(field.Type, next);
            }

            return;
        }

        throw Error($"Unknown RPC type: {node.Name}");
    }

    private string RenderCpp(RpcTypeNode node)
    {
        if (CppScalars.TryGetValue(node.Name, out string? scalar))
        {
            return scalar;
        }

        if (node.Name == "vector")
        {
            return $"std::vector<{RenderCpp(node.TypeArguments[0])}>";
        }

        return node.Name;
    }

    private string Canonicalize(RpcTypeNode node, IReadOnlyCollection<string> resolving)
    {
        if (CppScalars.ContainsKey(node.Name))
        {
            return node.Name;
        }

        if (node.Name == "vector")
        {
            return $"vector<{Canonicalize(node.TypeArguments[0], resolving)}>";
        }

        if (_aliases.TryGetValue(node.Name, out RpcAliasSchema? alias))
        {
            EnsureNotResolving(node.Name, resolving);
            return Canonicalize(Resolve(alias.Type), resolving.Append(node.Name).ToArray());
        }

        if (_enums.TryGetValue(node.Name, out RpcEnumSchema? enumSchema))
        {
            string values = string.Join(",", enumSchema.Values.Select(value => $"{value.Name}={value.Value}"));
            return $"enum<{enumSchema.Underlying}>[{values}]";
        }

        if (_structs.TryGetValue(node.Name, out RpcStructSchema? structSchema))
        {
            EnsureNotResolving(node.Name, resolving);
            string[] next = resolving.Append(node.Name).ToArray();
            string fields = string.Join(",", structSchema.Fields.Select(
                field => $"{field.Name}:{Canonicalize(Resolve(field.Type), next)}"));
            return $"struct{{{fields}}}";
        }

        throw Error($"Unknown RPC type: {node.Name}");
    }

    private void EnsureNotResolving(string name, IReadOnlyCollection<string> resolving)
    {
        if (resolving.Contains(name, StringComparer.Ordinal))
        {
            throw Error($"Cyclic RPC type definition detected at '{name}'");
        }
    }

    private InvalidOperationException Error(string message)
    {
        return new InvalidOperationException($"{message}: {_document.SourcePath}");
    }
}

internal sealed class RpcTypeNode
{
    public required string Name { get; init; }

    public required string OriginalText { get; init; }

    public List<RpcTypeNode> TypeArguments { get; } = [];
}

internal static class RpcTypeParser
{
    public static RpcTypeNode Parse(string text)
    {
        int index = 0;
        RpcTypeNode result = ParseNode(text, ref index);
        SkipWhitespace(text, ref index);
        if (index != text.Length)
        {
            throw new InvalidOperationException($"Unexpected token in RPC type: {text}");
        }

        return result;
    }

    private static RpcTypeNode ParseNode(string text, ref int index)
    {
        SkipWhitespace(text, ref index);
        int start = index;
        if (index >= text.Length || !(char.IsLetter(text[index]) || text[index] == '_'))
        {
            throw new InvalidOperationException($"RPC type identifier expected: {text}");
        }

        ++index;
        while (index < text.Length && (char.IsLetterOrDigit(text[index]) || text[index] == '_'))
        {
            ++index;
        }

        string name = text[start..index];
        var node = new RpcTypeNode { Name = name, OriginalText = text };
        SkipWhitespace(text, ref index);
        if (index >= text.Length || text[index] != '<')
        {
            return node;
        }

        ++index;
        while (true)
        {
            node.TypeArguments.Add(ParseNode(text, ref index));
            SkipWhitespace(text, ref index);
            if (index >= text.Length)
            {
                throw new InvalidOperationException($"Unclosed RPC type argument list: {text}");
            }

            if (text[index] == '>')
            {
                ++index;
                return node;
            }

            if (text[index] != ',')
            {
                throw new InvalidOperationException($"Expected ',' or '>' in RPC type: {text}");
            }

            ++index;
        }
    }

    private static void SkipWhitespace(string text, ref int index)
    {
        while (index < text.Length && char.IsWhiteSpace(text[index]))
        {
            ++index;
        }
    }
}
