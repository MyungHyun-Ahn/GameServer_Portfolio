using System.Text;
using YamlDotNet.Serialization;
using YamlDotNet.Serialization.NamingConventions;

internal static class Program
{
    public static int Main(string[] args)
    {
        try
        {
            string solutionRoot = FindSolutionRoot();
            string schemaRoot = Path.Combine(solutionRoot, "Packet");
            string outputRoot = Path.Combine(solutionRoot, "Generated", "Packets");

            ParseArguments(args, ref schemaRoot, ref outputRoot);

            if (!Directory.Exists(schemaRoot))
            {
                throw new InvalidOperationException($"Schema root not found: {schemaRoot}");
            }

            var deserializer = new DeserializerBuilder()
                .WithNamingConvention(HyphenatedNamingConvention.Instance)
                .IgnoreUnmatchedProperties()
                .Build();

            string[] schemaFiles = Directory.GetFiles(schemaRoot, "*.yaml", SearchOption.AllDirectories);
            if (schemaFiles.Length == 0)
            {
                throw new InvalidOperationException($"No schema files found in: {schemaRoot}");
            }

            var documents = new List<PacketSchemaDocument>();
            foreach (string schemaFile in schemaFiles)
            {
                string yamlText = File.ReadAllText(schemaFile);
                PacketSchemaDocument document = deserializer.Deserialize<PacketSchemaDocument>(yamlText)
                    ?? throw new InvalidOperationException($"Failed to deserialize schema: {schemaFile}");

                PacketSchemaValidator.Validate(document, schemaFile);
                documents.Add(document);
            }

            PacketSchemaSetValidator.Validate(documents);

            foreach (PacketSchemaDocument document in documents)
            {
                string contentOutputDirectory = Path.Combine(outputRoot, document.Content);
                Directory.CreateDirectory(contentOutputDirectory);

                string packetsHeaderPath = Path.Combine(contentOutputDirectory, $"{document.Content}Packets.h");
                string handlerHeaderPath = Path.Combine(contentOutputDirectory, $"{document.Content}PacketHandler.h");

                File.WriteAllText(packetsHeaderPath, CppPacketGenerator.GeneratePacketsHeader(document), new UTF8Encoding(false));
                File.WriteAllText(handlerHeaderPath, CppPacketGenerator.GenerateHandlerHeader(document), new UTF8Encoding(false));

                Console.WriteLine($"Generated: {packetsHeaderPath}");
                Console.WriteLine($"Generated: {handlerHeaderPath}");
            }

            string routerHeaderPath = Path.Combine(outputRoot, "PacketRouter.h");
            File.WriteAllText(routerHeaderPath, CppPacketGenerator.GenerateRouterHeader(documents), new UTF8Encoding(false));
            Console.WriteLine($"Generated: {routerHeaderPath}");

            return 0;
        }
        catch (Exception exception)
        {
            Console.Error.WriteLine($"PacketGenerator failed: {exception.Message}");
            return 1;
        }
    }

    private static void ParseArguments(string[] args, ref string schemaRoot, ref string outputRoot)
    {
        for (int index = 0; index < args.Length; ++index)
        {
            string argument = args[index];
            if (argument == "--schema-root" && index + 1 < args.Length)
            {
                schemaRoot = Path.GetFullPath(args[++index]);
            }
            else if (argument == "--output-root" && index + 1 < args.Length)
            {
                outputRoot = Path.GetFullPath(args[++index]);
            }
            else
            {
                throw new InvalidOperationException($"Unknown argument: {argument}");
            }
        }
    }

    private static string FindSolutionRoot()
    {
        DirectoryInfo? directory = new DirectoryInfo(AppContext.BaseDirectory);
        while (directory != null)
        {
            if (File.Exists(Path.Combine(directory.FullName, "Portfolio.sln")))
            {
                return directory.FullName;
            }

            directory = directory.Parent;
        }

        return Directory.GetCurrentDirectory();
    }
}

internal static class PacketSchemaSetValidator
{
    public static void Validate(IReadOnlyList<PacketSchemaDocument> documents)
    {
        var usedContentNames = new HashSet<string>(StringComparer.Ordinal);
        var usedOpcodes = new HashSet<int>();

        foreach (PacketSchemaDocument document in documents)
        {
            if (!usedContentNames.Add(document.Content))
            {
                throw new InvalidOperationException($"Duplicate content name in schema set: {document.Content}");
            }

            foreach (PacketSchemaMessage message in document.Messages)
            {
                ValidateEndpointOpcode(message.Rq, document.Content, message.Name, "Rq", usedOpcodes);
                ValidateEndpointOpcode(message.Rp, document.Content, message.Name, "Rp", usedOpcodes);
                ValidateEndpointOpcode(message.Noti, document.Content, message.Name, "Noti", usedOpcodes);
                ValidateEndpointOpcode(message.Broadcast, document.Content, message.Name, "Broadcast", usedOpcodes);
            }
        }
    }

    private static void ValidateEndpointOpcode(PacketSchemaEndpoint? endpoint, string contentName, string messageName, string kind, HashSet<int> usedOpcodes)
    {
        if (endpoint == null)
        {
            return;
        }

        if (!usedOpcodes.Add(endpoint.Opcode))
        {
            throw new InvalidOperationException($"Duplicate opcode {endpoint.Opcode} across schema set: {contentName}.{messageName}.{kind}");
        }
    }
}

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
    public static void Validate(PacketSchemaDocument document, string schemaPath)
    {
        if (string.IsNullOrWhiteSpace(document.Content))
        {
            throw new InvalidOperationException($"Schema content name is empty: {schemaPath}");
        }

        if (document.Messages.Count == 0)
        {
            throw new InvalidOperationException($"Schema has no messages: {schemaPath}");
        }

        var usedOpcodes = new HashSet<int>();
        foreach (PacketSchemaMessage message in document.Messages)
        {
            if (string.IsNullOrWhiteSpace(message.Name))
            {
                throw new InvalidOperationException($"Message name is empty: {schemaPath}");
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

            ValidateEndpoint(message.Rq, "Rq", message.Name, usedOpcodes, schemaPath);
            ValidateEndpoint(message.Rp, "Rp", message.Name, usedOpcodes, schemaPath);
            ValidateEndpoint(message.Noti, "Noti", message.Name, usedOpcodes, schemaPath);
            ValidateEndpoint(message.Broadcast, "Broadcast", message.Name, usedOpcodes, schemaPath);
        }
    }

    private static void ValidateEndpoint(PacketSchemaEndpoint? endpoint, string kind, string messageName, HashSet<int> usedOpcodes, string schemaPath)
    {
        if (endpoint == null)
        {
            return;
        }

        if (endpoint.Opcode <= 0)
        {
            throw new InvalidOperationException($"{messageName}.{kind} opcode must be positive: {schemaPath}");
        }

        if (!usedOpcodes.Add(endpoint.Opcode))
        {
            throw new InvalidOperationException($"Duplicate opcode {endpoint.Opcode} in schema: {schemaPath}");
        }

        foreach (PacketSchemaField field in endpoint.Fields)
        {
            if (string.IsNullOrWhiteSpace(field.Name))
            {
                throw new InvalidOperationException($"{messageName}.{kind} has empty field name: {schemaPath}");
            }

            if (string.IsNullOrWhiteSpace(field.Type))
            {
                throw new InvalidOperationException($"{messageName}.{kind}.{field.Name} has empty field type: {schemaPath}");
            }

            _ = PacketTypeMapping.RenderCppType(field.Type);
            _ = PacketTypeMapping.RenderCSharpType(field.Type);
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

    public static string RenderCppType(string schemaType)
    {
        SchemaTypeNode typeNode = SchemaTypeParser.Parse(schemaType);
        return RenderCppType(typeNode);
    }

    public static string RenderCSharpType(string schemaType)
    {
        SchemaTypeNode typeNode = SchemaTypeParser.Parse(schemaType);
        return RenderCSharpType(typeNode);
    }

    private static string RenderCppType(SchemaTypeNode typeNode)
    {
        if (CppScalarTypes.TryGetValue(typeNode.Name, out string? scalarType))
        {
            return scalarType;
        }

        return typeNode.Name switch
        {
            "vector" => $"std::vector<{RenderCppType(typeNode.TypeArguments[0])}>",
            "array" => $"std::array<{RenderCppType(typeNode.TypeArguments[0])}, {typeNode.LiteralArguments[0]}>",
            "map" => $"std::map<{RenderCppType(typeNode.TypeArguments[0])}, {RenderCppType(typeNode.TypeArguments[1])}>",
            "unordered_map" => $"std::unordered_map<{RenderCppType(typeNode.TypeArguments[0])}, {RenderCppType(typeNode.TypeArguments[1])}>",
            _ => throw new InvalidOperationException($"Unsupported C++ schema type: {typeNode.OriginalText}")
        };
    }

    private static string RenderCSharpType(SchemaTypeNode typeNode)
    {
        if (CSharpScalarTypes.TryGetValue(typeNode.Name, out string? scalarType))
        {
            return scalarType;
        }

        return typeNode.Name switch
        {
            "vector" => $"List<{RenderCSharpType(typeNode.TypeArguments[0])}>",
            "array" => $"{RenderCSharpType(typeNode.TypeArguments[0])}[]",
            "map" => $"Dictionary<{RenderCSharpType(typeNode.TypeArguments[0])}, {RenderCSharpType(typeNode.TypeArguments[1])}>",
            "unordered_map" => $"Dictionary<{RenderCSharpType(typeNode.TypeArguments[0])}, {RenderCSharpType(typeNode.TypeArguments[1])}>",
            _ => throw new InvalidOperationException($"Unsupported C# schema type: {typeNode.OriginalText}")
        };
    }
}

internal sealed class SchemaTypeNode
{
    public required string Name { get; init; }

    public required string OriginalText { get; init; }

    public List<SchemaTypeNode> TypeArguments { get; } = [];

    public List<string> LiteralArguments { get; } = [];
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
            }
            else
            {
                node.TypeArguments.Add(ParseType(text, ref index));
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
        while (index < text.Length && (char.IsLetterOrDigit(text[index]) || text[index] == '_' ))
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

internal static class CppPacketGenerator
{
    public static string GeneratePacketsHeader(PacketSchemaDocument document)
    {
        var builder = new StringBuilder();
        builder.AppendLine("#pragma once");
        builder.AppendLine();
        builder.AppendLine($"namespace Generated::{document.Content}");
        builder.AppendLine("{");

        foreach (PacketSchemaMessage message in document.Messages)
        {
            AppendPacketClass(builder, message.Name, "Rq", message.Rq);
            AppendPacketClass(builder, message.Name, "Rp", message.Rp);
            AppendPacketClass(builder, message.Name, "Noti", message.Noti);
            AppendPacketClass(builder, message.Name, "Broadcast", message.Broadcast);
        }

        builder.AppendLine("}");
        return builder.ToString();
    }

    public static string GenerateHandlerHeader(PacketSchemaDocument document)
    {
        var builder = new StringBuilder();
        builder.AppendLine("#pragma once");
        builder.AppendLine();
        builder.AppendLine($"namespace Generated::{document.Content}");
        builder.AppendLine("{");
        builder.AppendLine($"\tclass I{document.Content}PacketHandler");
        builder.AppendLine("\t{");
        builder.AppendLine("\tpublic:");
        builder.AppendLine($"\t\tvirtual ~I{document.Content}PacketHandler() = default;");
        builder.AppendLine();

        foreach (PacketSchemaMessage message in document.Messages)
        {
            AppendHandlerDeclaration(builder, message.Name, "Rq", message.Rq);
            AppendHandlerDeclaration(builder, message.Name, "Rp", message.Rp);
            AppendHandlerDeclaration(builder, message.Name, "Noti", message.Noti);
            AppendHandlerDeclaration(builder, message.Name, "Broadcast", message.Broadcast);
        }

        builder.AppendLine("\t};");
        builder.AppendLine();
        builder.AppendLine($"\tclass I{document.Content}PacketDispatcher");
        builder.AppendLine("\t{");
        builder.AppendLine("\tpublic:");
        builder.AppendLine($"\t\tvirtual ~I{document.Content}PacketDispatcher() = default;");
        builder.AppendLine("\t\tvirtual bool DispatchPacket(NetworkLib::IServer& server, std::uint64_t sessionId, const NetworkLib::Packet::View::FPacketView& packetView) = 0;");
        builder.AppendLine("\t};");
        builder.AppendLine();
        builder.AppendLine($"\tclass F{document.Content}PacketHandlerBase : public I{document.Content}PacketHandler, public I{document.Content}PacketDispatcher");
        builder.AppendLine("\t{");
        builder.AppendLine("\tpublic:");
        builder.AppendLine("\t\tbool DispatchPacket(NetworkLib::IServer& server, std::uint64_t sessionId, const NetworkLib::Packet::View::FPacketView& packetView) override");
        builder.AppendLine("\t\t{");
        builder.AppendLine("\t\t\tswitch (packetView.opcode)");
        builder.AppendLine("\t\t\t{");

        foreach (PacketSchemaMessage message in document.Messages)
        {
            AppendDispatchCase(builder, message.Name, "Rq", message.Rq);
            AppendDispatchCase(builder, message.Name, "Rp", message.Rp);
            AppendDispatchCase(builder, message.Name, "Noti", message.Noti);
            AppendDispatchCase(builder, message.Name, "Broadcast", message.Broadcast);
        }

        builder.AppendLine("\t\t\tdefault:");
        builder.AppendLine("\t\t\t\treturn OnUnhandledPacket(server, sessionId, packetView);");
        builder.AppendLine("\t\t\t}");
        builder.AppendLine("\t\t}");
        builder.AppendLine();

        foreach (PacketSchemaMessage message in document.Messages)
        {
            AppendNoOpHandler(builder, message.Name, "Rq", message.Rq);
            AppendNoOpHandler(builder, message.Name, "Rp", message.Rp);
            AppendNoOpHandler(builder, message.Name, "Noti", message.Noti);
            AppendNoOpHandler(builder, message.Name, "Broadcast", message.Broadcast);
        }

        builder.AppendLine("\tprotected:");
        builder.AppendLine("\t\tvirtual bool OnUnhandledPacket(NetworkLib::IServer&, std::uint64_t, const NetworkLib::Packet::View::FPacketView&)");
        builder.AppendLine("\t\t{");
        builder.AppendLine("\t\t\treturn false;");
        builder.AppendLine("\t\t}");
        builder.AppendLine("\t};");
        builder.AppendLine();
        builder.AppendLine("\ttemplate <typename TPacket>");
        builder.AppendLine("\tinline bool SendGeneratedPacket(NetworkLib::IServer& server, std::uint64_t sessionId, const TPacket& packet)");
        builder.AppendLine("\t{");
        builder.AppendLine("\t\treturn NetworkLib::Packet::Serialization::SendContentPacket(server, sessionId, packet);");
        builder.AppendLine("\t}");
        builder.AppendLine("}");
        return builder.ToString();
    }

    public static string GenerateRouterHeader(IReadOnlyList<PacketSchemaDocument> documents)
    {
        var builder = new StringBuilder();
        builder.AppendLine("#pragma once");
        builder.AppendLine();
        builder.AppendLine("namespace Generated");
        builder.AppendLine("{");
        builder.AppendLine("\tclass FPacketRouter");
        builder.AppendLine("\t{");
        builder.AppendLine("\tpublic:");

        foreach (PacketSchemaDocument document in documents)
        {
            builder.AppendLine($"\t\tvoid Set{document.Content}Handler({document.Content}::I{document.Content}PacketDispatcher* handler) noexcept");
            builder.AppendLine("\t\t{");
            builder.AppendLine($"\t\t\tm_{ToMemberName(document.Content)}Handler = handler;");
            builder.AppendLine("\t\t}");
            builder.AppendLine();
        }

        builder.AppendLine("\t\tbool DispatchPacket(NetworkLib::IServer& server, std::uint64_t sessionId, const NetworkLib::Packet::View::FPacketView& packetView)");
        builder.AppendLine("\t\t{");
        builder.AppendLine("\t\t\tswitch (packetView.opcode)");
        builder.AppendLine("\t\t\t{");

        foreach (PacketSchemaDocument document in documents)
        {
            AppendRouterDispatchCases(builder, document);
        }

        builder.AppendLine("\t\t\tdefault:");
        builder.AppendLine("\t\t\t\treturn false;");
        builder.AppendLine("\t\t\t}");
        builder.AppendLine("\t\t}");
        builder.AppendLine();
        builder.AppendLine("\tprivate:");

        foreach (PacketSchemaDocument document in documents)
        {
            builder.AppendLine($"\t\t{document.Content}::I{document.Content}PacketDispatcher* m_{ToMemberName(document.Content)}Handler = nullptr;");
        }

        builder.AppendLine("\t};");
        builder.AppendLine("}");
        return builder.ToString();
    }

    private static void AppendPacketClass(StringBuilder builder, string messageName, string kindSuffix, PacketSchemaEndpoint? endpoint)
    {
        if (endpoint == null)
        {
            return;
        }

        string className = BuildClassName(messageName, kindSuffix);
        bool containsBorrowedViews = EndpointContainsBorrowedViews(endpoint);
        builder.AppendLine($"\tclass {className} final : public NetworkLib::Packet::Serialization::IContentPacket");
        builder.AppendLine("\t{");
        builder.AppendLine("\tpublic:");
        builder.AppendLine($"\t\tstatic constexpr std::uint16_t kOpcode = {endpoint.Opcode};");
        builder.AppendLine();

        foreach (PacketSchemaField field in endpoint.Fields)
        {
            if (FieldContainsBorrowedView(field))
            {
                builder.AppendLine($"\t\tvoid Set{ToAccessorSuffix(field.Name)}Value({PacketTypeMapping.RenderCppType(field.Type)} value) noexcept");
                builder.AppendLine("\t\t{");
                builder.AppendLine($"\t\t\tm_{field.Name} = value;");
                builder.AppendLine("\t\t}");
                builder.AppendLine();
                builder.AppendLine($"\t\t{PacketTypeMapping.RenderCppType(field.Type)} Get{ToAccessorSuffix(field.Name)}Value() const noexcept");
                builder.AppendLine("\t\t{");
                builder.AppendLine("\t\t\tNetworkLib::Packet::View::ValidateBorrowedViewAccess(m_borrowedViewScope);");
                builder.AppendLine($"\t\t\treturn m_{field.Name};");
                builder.AppendLine("\t\t}");
                builder.AppendLine();
            }
            else
            {
                builder.AppendLine($"\t\t{PacketTypeMapping.RenderCppType(field.Type)} {field.Name}{{}};");
            }
        }

        if (endpoint.Fields.Count > 0)
        {
            builder.AppendLine();
        }

        if (containsBorrowedViews)
        {
            builder.AppendLine("\t\tvoid BindBorrowedViewScope(const std::shared_ptr<NetworkLib::Packet::View::FBorrowedViewScopeState>& scope) noexcept override");
            builder.AppendLine("\t\t{");
            builder.AppendLine("\t\t\tm_borrowedViewScope = scope;");
            builder.AppendLine("\t\t}");
            builder.AppendLine();
        }

        builder.AppendLine("\tpublic:");
        builder.AppendLine("\t\tstd::uint16_t GetOpcode() const noexcept override");
        builder.AppendLine("\t\t{");
        builder.AppendLine("\t\t\treturn kOpcode;");
        builder.AppendLine("\t\t}");
        builder.AppendLine();
        builder.AppendLine("\t\tbool ContainsBorrowedViews() const noexcept override");
        builder.AppendLine("\t\t{");
        builder.AppendLine($"\t\t\treturn {(containsBorrowedViews ? "true" : "false")};");
        builder.AppendLine("\t\t}");
        builder.AppendLine();
        builder.AppendLine("\t\tstd::size_t GetEstimatedBodySize() const noexcept override");
        builder.AppendLine("\t\t{");
        if (endpoint.Fields.Count == 0)
        {
            builder.AppendLine("\t\t\treturn 0;");
        }
        else
        {
            builder.Append("\t\t\treturn ");
            for (int index = 0; index < endpoint.Fields.Count; ++index)
            {
                PacketSchemaField field = endpoint.Fields[index];
                if (index > 0)
                {
                    builder.AppendLine();
                    builder.Append("\t\t\t\t+ ");
                }

                builder.Append($"NetworkLib::Packet::Serialization::GetSerializedSize({RenderFieldAccess(field)})");
            }

            builder.AppendLine(";");
        }
        builder.AppendLine("\t\t}");
        builder.AppendLine();
        builder.AppendLine("\t\tvoid Serialize(NetworkLib::Packet::Serialization::FPacketWriter& writer) const override");
        builder.AppendLine("\t\t{");
        foreach (PacketSchemaField field in endpoint.Fields)
        {
            builder.AppendLine($"\t\t\twriter.Write({RenderFieldAccess(field)});");
        }
        builder.AppendLine("\t\t}");
        builder.AppendLine();
        builder.AppendLine("\t\tbool Deserialize(NetworkLib::Packet::Serialization::FPacketReader& reader) override");
        builder.AppendLine("\t\t{");
        if (endpoint.Fields.Count == 0)
        {
            builder.AppendLine("\t\t\treturn true;");
        }
        else
        {
            builder.Append("\t\t\treturn ");
            for (int index = 0; index < endpoint.Fields.Count; ++index)
            {
                PacketSchemaField field = endpoint.Fields[index];
                if (index > 0)
                {
                    builder.AppendLine();
                    builder.Append("\t\t\t\t&& ");
                }

                builder.Append($"reader.Read({RenderFieldAccess(field)})");
            }

            builder.AppendLine(";");
        }
        builder.AppendLine("\t\t}");
        if (containsBorrowedViews)
        {
            builder.AppendLine();
            builder.AppendLine("\tprivate:");
            builder.AppendLine("\t\tstd::shared_ptr<NetworkLib::Packet::View::FBorrowedViewScopeState> m_borrowedViewScope;");
            foreach (PacketSchemaField field in endpoint.Fields)
            {
                if (FieldContainsBorrowedView(field))
                {
                    builder.AppendLine($"\t\t{PacketTypeMapping.RenderCppType(field.Type)} m_{field.Name}{{}};");
                }
            }
        }
        builder.AppendLine("\t};");
        builder.AppendLine();
    }

    private static void AppendHandlerDeclaration(StringBuilder builder, string messageName, string kindSuffix, PacketSchemaEndpoint? endpoint)
    {
        if (endpoint == null)
        {
            return;
        }

        string className = BuildClassName(messageName, kindSuffix);
        string handlerMethodName = BuildHandlerMethodName(messageName, kindSuffix);
        builder.AppendLine($"\t\tvirtual bool {handlerMethodName}(NetworkLib::IServer& server, std::uint64_t sessionId, const {className}& packet) = 0;");
    }

    private static void AppendNoOpHandler(StringBuilder builder, string messageName, string kindSuffix, PacketSchemaEndpoint? endpoint)
    {
        if (endpoint == null)
        {
            return;
        }

        string className = BuildClassName(messageName, kindSuffix);
        string handlerMethodName = BuildHandlerMethodName(messageName, kindSuffix);
        builder.AppendLine($"\t\tbool {handlerMethodName}(NetworkLib::IServer&, std::uint64_t, const {className}&) override");
        builder.AppendLine("\t\t{");
        builder.AppendLine("\t\t\treturn false;");
        builder.AppendLine("\t\t}");
        builder.AppendLine();
    }

    private static void AppendDispatchCase(StringBuilder builder, string messageName, string kindSuffix, PacketSchemaEndpoint? endpoint)
    {
        if (endpoint == null)
        {
            return;
        }

        string className = BuildClassName(messageName, kindSuffix);
        string handlerMethodName = BuildHandlerMethodName(messageName, kindSuffix);
        builder.AppendLine($"\t\t\tcase {className}::kOpcode:");
        builder.AppendLine("\t\t\t\t{");
        builder.AppendLine($"\t\t\t\t\t{className} packet;");
        if (EndpointContainsBorrowedViews(endpoint))
        {
            builder.AppendLine("\t\t\t\t\tNetworkLib::Packet::View::FBorrowedViewScope borrowedViewScope;");
            builder.AppendLine("\t\t\t\t\tpacket.BindBorrowedViewScope(borrowedViewScope.GetState());");
        }
        builder.AppendLine("\t\t\t\t\tif (!NetworkLib::Packet::Serialization::DeserializeContentPacket(packetView, packet))");
        builder.AppendLine("\t\t\t\t\t{");
        builder.AppendLine("\t\t\t\t\t\treturn false;");
        builder.AppendLine("\t\t\t\t\t}");
        builder.AppendLine();
        builder.AppendLine($"\t\t\t\t\treturn {handlerMethodName}(server, sessionId, packet);");
        builder.AppendLine("\t\t\t\t}");
    }

    private static void AppendRouterDispatchCases(StringBuilder builder, PacketSchemaDocument document)
    {
        string memberName = $"m_{ToMemberName(document.Content)}Handler";
        foreach (PacketSchemaMessage message in document.Messages)
        {
            AppendRouterDispatchCase(builder, BuildQualifiedClassName(document.Content, message.Name, "Rq"), message.Rq, memberName);
            AppendRouterDispatchCase(builder, BuildQualifiedClassName(document.Content, message.Name, "Rp"), message.Rp, memberName);
            AppendRouterDispatchCase(builder, BuildQualifiedClassName(document.Content, message.Name, "Noti"), message.Noti, memberName);
            AppendRouterDispatchCase(builder, BuildQualifiedClassName(document.Content, message.Name, "Broadcast"), message.Broadcast, memberName);
        }
    }

    private static void AppendRouterDispatchCase(StringBuilder builder, string qualifiedClassName, PacketSchemaEndpoint? endpoint, string memberName)
    {
        if (endpoint == null)
        {
            return;
        }

        builder.AppendLine($"\t\t\tcase {qualifiedClassName}::kOpcode:");
        builder.AppendLine($"\t\t\t\treturn {memberName} != nullptr ? {memberName}->DispatchPacket(server, sessionId, packetView) : false;");
    }

    private static string ToMemberName(string contentName)
    {
        if (string.IsNullOrEmpty(contentName))
        {
            return "content";
        }

        return char.ToLowerInvariant(contentName[0]) + contentName[1..];
    }

    private static string BuildClassName(string messageName, string kindSuffix)
    {
        return kindSuffix switch
        {
            "Broadcast" => $"F{messageName}",
            _ => $"F{messageName}{kindSuffix}"
        };
    }

    private static string BuildQualifiedClassName(string contentName, string messageName, string kindSuffix)
    {
        return $"{contentName}::{BuildClassName(messageName, kindSuffix)}";
    }

    private static string BuildHandlerMethodName(string messageName, string kindSuffix)
    {
        return kindSuffix switch
        {
            "Broadcast" => $"Handle{messageName}",
            _ => $"Handle{messageName}{kindSuffix}"
        };
    }

    private static bool EndpointContainsBorrowedViews(PacketSchemaEndpoint endpoint)
    {
        foreach (PacketSchemaField field in endpoint.Fields)
        {
            if (FieldContainsBorrowedView(field))
            {
                return true;
            }
        }

        return false;
    }

    private static bool FieldContainsBorrowedView(PacketSchemaField field)
    {
        return SchemaTypeContainsBorrowedView(SchemaTypeParser.Parse(field.Type));
    }

    private static bool SchemaTypeContainsBorrowedView(SchemaTypeNode typeNode)
    {
        if (typeNode.Name is "string_view" or "bytes_view")
        {
            return true;
        }

        foreach (SchemaTypeNode childType in typeNode.TypeArguments)
        {
            if (SchemaTypeContainsBorrowedView(childType))
            {
                return true;
            }
        }

        return false;
    }

    private static string RenderFieldAccess(PacketSchemaField field)
    {
        return FieldContainsBorrowedView(field) ? $"m_{field.Name}" : field.Name;
    }

    private static string ToAccessorSuffix(string fieldName)
    {
        if (string.IsNullOrEmpty(fieldName))
        {
            return "Field";
        }

        return char.ToUpperInvariant(fieldName[0]) + fieldName[1..];
    }
}
