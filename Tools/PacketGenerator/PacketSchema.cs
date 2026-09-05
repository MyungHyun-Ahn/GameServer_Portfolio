internal sealed class PacketSchemaDocument
{
    public string Content { get; set; } = string.Empty;

    public List<PacketSchemaMessage> Messages { get; set; } = [];
}

internal sealed class PacketSchemaMessage
{
    public string Name { get; set; } = string.Empty;

    public PacketSchemaEndpoint? Rq { get; set; }

    public PacketSchemaEndpoint? Rp { get; set; }

    public PacketSchemaEndpoint? Noti { get; set; }

    public PacketSchemaEndpoint? Broadcast { get; set; }
}

internal sealed class PacketSchemaEndpoint
{
    public int Opcode { get; set; }

    public List<PacketSchemaField> Fields { get; set; } = [];
}

internal sealed class PacketSchemaField
{
    public string Name { get; set; } = string.Empty;

    public string Type { get; set; } = string.Empty;
}

internal static class PacketSchemaValidator
{
    private static readonly HashSet<string> ReservedLanguageIdentifiers = new(StringComparer.Ordinal)
    {
        "abstract", "alignas", "alignof", "and", "and_eq", "as", "asm", "auto", "base", "bitand", "bitor",
        "bool", "break", "byte", "case", "catch", "char", "char8_t", "char16_t", "char32_t", "checked", "class",
        "compl", "concept", "const", "const_cast", "consteval", "constexpr", "constinit", "continue", "co_await",
        "co_return", "co_yield", "decimal", "decltype", "default", "delegate", "delete", "do", "double",
        "dynamic_cast", "else", "enum", "event", "explicit", "export", "extern", "false", "finally", "fixed",
        "float", "for", "foreach", "friend", "goto", "if", "implicit", "in", "inline", "int", "interface",
        "internal", "is", "lock", "long", "mutable", "namespace", "new", "noexcept", "not", "not_eq", "nullptr",
        "null", "object", "operator", "or", "or_eq", "out", "override", "params", "private", "protected", "public",
        "readonly", "ref", "register", "reinterpret_cast", "requires", "return", "sbyte", "sealed", "short", "signed",
        "sizeof", "stackalloc", "static", "static_assert", "static_cast", "string", "struct", "switch", "template",
        "this", "thread_local", "throw", "true", "try", "typedef", "typeid", "typename", "uint", "ulong", "unchecked",
        "union", "unsafe", "unsigned", "ushort", "using", "virtual", "void", "volatile", "wchar_t", "while", "xor",
        "xor_eq"
    };

    private static readonly HashSet<string> GeneratedCSharpMemberNames = new(StringComparer.Ordinal)
    {
        "Opcode",
        "OpcodeValue",
        "PacketKind",
        "SerializeBody",
        "TryDeserializeBody",
        "CreateFixedArray"
    };

    private static readonly HashSet<string> GeneratedCppMemberNames = new(StringComparer.Ordinal)
    {
        "kOpcode",
        "kPacketKind",
        "GetOpcode",
        "ContainsBorrowedViews",
        "GetEstimatedBodySize",
        "Serialize",
        "Deserialize",
        "BindBorrowedViewScope",
        "m_borrowedViewScope"
    };

    public static void Validate(PacketSchemaDocument document, string schemaPath)
    {
        ValidateIdentifier(document.Content, "content", schemaPath);

        if (document.Messages.Count == 0)
        {
            throw new InvalidOperationException($"Schema has no messages: {schemaPath}");
        }

        var usedMessageNames = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var usedGeneratedTypeNames = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var usedOpcodes = new HashSet<int>();
        foreach (PacketSchemaMessage message in document.Messages)
        {
            ValidateIdentifier(message.Name, "message", schemaPath);
            if (!usedMessageNames.Add(message.Name))
            {
                throw new InvalidOperationException($"Duplicate message name '{message.Name}': {schemaPath}");
            }

            bool hasRq = message.Rq != null;
            bool hasRp = message.Rp != null;
            bool hasNoti = message.Noti != null;
            bool hasBroadcast = message.Broadcast != null;
            if (hasRq != hasRp)
            {
                throw new InvalidOperationException($"Message '{message.Name}' must define both Rq and Rp together: {schemaPath}");
            }

            if (!hasRq && !hasNoti && !hasBroadcast)
            {
                throw new InvalidOperationException($"Message '{message.Name}' must define either Rq/Rp pair, Noti, or Broadcast: {schemaPath}");
            }

            if (hasNoti && hasBroadcast)
            {
                throw new InvalidOperationException($"Message '{message.Name}' cannot define both Noti and Broadcast: {schemaPath}");
            }

            ValidateEndpoint(message.Rq, EPacketEndpointKind.Request, message.Name, usedOpcodes, usedGeneratedTypeNames, schemaPath);
            ValidateEndpoint(message.Rp, EPacketEndpointKind.Response, message.Name, usedOpcodes, usedGeneratedTypeNames, schemaPath);
            ValidateEndpoint(message.Noti, EPacketEndpointKind.Notification, message.Name, usedOpcodes, usedGeneratedTypeNames, schemaPath);
            ValidateEndpoint(message.Broadcast, EPacketEndpointKind.Broadcast, message.Name, usedOpcodes, usedGeneratedTypeNames, schemaPath);
        }
    }

    private static void ValidateEndpoint(
        PacketSchemaEndpoint? endpoint,
        EPacketEndpointKind endpointKind,
        string messageName,
        HashSet<int> usedOpcodes,
        HashSet<string> usedGeneratedTypeNames,
        string schemaPath)
    {
        if (endpoint == null)
        {
            return;
        }

        string endpointName = PacketNaming.GetEndpointSuffix(endpointKind);
        if (endpoint.Opcode is <= 0 or > ushort.MaxValue)
        {
            throw new InvalidOperationException(
                $"{messageName}.{endpointName} opcode must be between 1 and {ushort.MaxValue}: {schemaPath}");
        }

        if (!usedOpcodes.Add(endpoint.Opcode))
        {
            throw new InvalidOperationException($"Duplicate opcode {endpoint.Opcode} in schema: {schemaPath}");
        }

        string generatedTypeName = PacketNaming.BuildCppClassName(messageName, endpointKind);
        if (!usedGeneratedTypeNames.Add(generatedTypeName))
        {
            throw new InvalidOperationException($"Duplicate generated packet type '{generatedTypeName}': {schemaPath}");
        }

        string generatedCSharpTypeName = PacketNaming.BuildCSharpClassName(messageName, endpointKind);

        var usedFieldNames = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var usedCSharpPropertyNames = new HashSet<string>(StringComparer.Ordinal);
        var usedCppMemberNames = new HashSet<string>(GeneratedCppMemberNames, StringComparer.Ordinal)
        {
            generatedTypeName
        };
        ulong minimumBodySize = 0;
        foreach (PacketSchemaField field in endpoint.Fields)
        {
            ValidateIdentifier(field.Name, $"{messageName}.{endpointName} field", schemaPath);
            if (!usedFieldNames.Add(field.Name))
            {
                throw new InvalidOperationException($"Duplicate field '{messageName}.{endpointName}.{field.Name}': {schemaPath}");
            }

            if (string.IsNullOrWhiteSpace(field.Type))
            {
                throw new InvalidOperationException($"{messageName}.{endpointName}.{field.Name} has empty field type: {schemaPath}");
            }

            SchemaTypeNode type = SchemaTypeParser.Parse(field.Type);
            PacketTypeRules.Validate(type, $"{messageName}.{endpointName}.{field.Name}", schemaPath);
            PacketTypeRules.ValidateCSharpMvp(type, $"{messageName}.{endpointName}.{field.Name}", schemaPath);

            bool containsBorrowedView = PacketTypeRules.ContainsBorrowedView(type);
            string cppStorageName = containsBorrowedView ? $"m_{field.Name}" : field.Name;
            ValidateGeneratedCppMemberName(cppStorageName, field.Name, messageName, endpointName, usedCppMemberNames, schemaPath);
            if (containsBorrowedView)
            {
                string accessorSuffix = PacketNaming.ToCppAccessorSuffix(field.Name);
                ValidateGeneratedCppMemberName(
                    $"Set{accessorSuffix}Value", field.Name, messageName, endpointName, usedCppMemberNames, schemaPath);
                ValidateGeneratedCppMemberName(
                    $"Get{accessorSuffix}Value", field.Name, messageName, endpointName, usedCppMemberNames, schemaPath);
            }

            try
            {
                minimumBodySize = checked(minimumBodySize + PacketTypeRules.GetMinimumSerializedSize(type));
            }
            catch (OverflowException exception)
            {
                throw new InvalidOperationException(
                    $"Minimum serialized size overflow in '{messageName}.{endpointName}.{field.Name}': {schemaPath}", exception);
            }

            string propertyName = PacketNaming.ToCSharpIdentifier(field.Name);
            if (string.IsNullOrEmpty(propertyName))
            {
                throw new InvalidOperationException(
                    $"Field '{messageName}.{endpointName}.{field.Name}' cannot be converted to a C# property name: {schemaPath}");
            }

            if (!usedCSharpPropertyNames.Add(propertyName) ||
                GeneratedCSharpMemberNames.Contains(propertyName) ||
                string.Equals(propertyName, generatedCSharpTypeName, StringComparison.Ordinal))
            {
                throw new InvalidOperationException(
                    $"Field '{messageName}.{endpointName}.{field.Name}' conflicts with generated C# member '{propertyName}': {schemaPath}");
            }
        }

        if (minimumBodySize > PacketWireContract.MaxContentBodySize)
        {
            throw new InvalidOperationException(
                $"Packet '{messageName}.{endpointName}' has a minimum body size of {minimumBodySize} bytes; " +
                $"the maximum is {PacketWireContract.MaxContentBodySize}: {schemaPath}");
        }
    }

    private static void ValidateIdentifier(string value, string description, string schemaPath)
    {
        if (string.IsNullOrWhiteSpace(value))
        {
            throw new InvalidOperationException($"Schema {description} name is empty: {schemaPath}");
        }

        if (!(char.IsLetter(value[0]) || value[0] == '_'))
        {
            throw new InvalidOperationException($"Schema {description} name is not a valid identifier '{value}': {schemaPath}");
        }

        for (int index = 1; index < value.Length; ++index)
        {
            if (!(char.IsLetterOrDigit(value[index]) || value[index] == '_'))
            {
                throw new InvalidOperationException($"Schema {description} name is not a valid identifier '{value}': {schemaPath}");
            }
        }

        if (ReservedLanguageIdentifiers.Contains(value))
        {
            throw new InvalidOperationException(
                $"Schema {description} name uses a reserved C++/C# identifier '{value}': {schemaPath}");
        }
    }

    private static void ValidateGeneratedCppMemberName(
        string generatedMemberName,
        string fieldName,
        string messageName,
        string endpointName,
        HashSet<string> usedCppMemberNames,
        string schemaPath)
    {
        if (usedCppMemberNames.Add(generatedMemberName))
        {
            return;
        }

        throw new InvalidOperationException(
            $"Field '{messageName}.{endpointName}.{fieldName}' conflicts with generated C++ member " +
            $"'{generatedMemberName}': {schemaPath}");
    }
}
