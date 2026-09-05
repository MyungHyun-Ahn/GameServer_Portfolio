using System.Text;

internal static class CppRpcGenerator
{
    public static List<RpcGeneratedFile> Generate(IReadOnlyList<RpcSchemaDocument> documents)
    {
        var result = documents
            .OrderBy(document => document.Service.Id)
            .ThenBy(document => document.Output, StringComparer.Ordinal)
            .Select(document => new RpcGeneratedFile
            {
                RelativePath = NormalizePath(document.Output),
                Content = GenerateServiceHeader(document),
            })
            .ToList();

        result.Add(new RpcGeneratedFile
        {
            RelativePath = "RpcMethodCatalog.h",
            Content = GenerateCatalogHeader(documents),
        });
        return result.OrderBy(file => file.RelativePath, StringComparer.Ordinal).ToList();
    }

    private static string GenerateServiceHeader(RpcSchemaDocument document)
    {
        var builder = new StringBuilder();
        var types = RpcSchemaValidator.CreateTypeRegistry(document);
        builder.AppendLine("#pragma once");
        builder.AppendLine();
        builder.AppendLine("// Generated from RPC YAML. Keep deterministic layout; do not format by hand.");
        builder.AppendLine("// clang-format off");
        builder.AppendLine($"namespace {document.Namespace}");
        builder.AppendLine("{");

        foreach (RpcEnumSchema enumSchema in document.Enums)
        {
            builder.AppendLine($"\tenum class {enumSchema.Name} : {types.RenderCpp(enumSchema.Underlying)};");
        }

        foreach (RpcStructSchema structSchema in document.Structs)
        {
            builder.AppendLine($"\tstruct {structSchema.Name};");
        }

        if (document.Enums.Count > 0 || document.Structs.Count > 0)
        {
            builder.AppendLine();
        }

        foreach (RpcAliasSchema alias in OrderAliases(document))
        {
            builder.AppendLine($"\tusing {alias.Name} = {types.RenderCpp(alias.Type)};");
        }

        if (document.Aliases.Count > 0)
        {
            builder.AppendLine();
        }

        builder.AppendLine(
            $"\tinline constexpr RpcLib::Protocol::FRpcServiceId k{document.Service.Name}ServiceId = {document.Service.Id};");

        foreach (RpcEnumSchema enumSchema in document.Enums)
        {
            builder.AppendLine();
            builder.AppendLine($"\tenum class {enumSchema.Name} : {types.RenderCpp(enumSchema.Underlying)}");
            builder.AppendLine("\t{");
            foreach (RpcEnumValueSchema value in enumSchema.Values)
            {
                builder.AppendLine($"\t\t{value.Name} = {value.Value},");
            }

            builder.AppendLine("\t};");
        }

        foreach (RpcStructSchema structSchema in OrderStructs(document))
        {
            AppendStruct(builder, structSchema, types);
        }

        foreach (RpcMethodSchema method in document.Methods.OrderBy(item => item.Id).ThenBy(item => item.Name, StringComparer.Ordinal))
        {
            if (method.Request != null)
            {
                AppendRpcDescriptor(builder, document, method, types);
            }

            if (method.Noti != null)
            {
                AppendNotificationDescriptor(builder, document, method, types);
            }
        }

        builder.AppendLine("}");
        builder.AppendLine("// clang-format on");
        return NormalizeNewlines(builder.ToString());
    }

    private static IReadOnlyList<RpcAliasSchema> OrderAliases(RpcSchemaDocument document)
    {
        var aliasesByName = document.Aliases.ToDictionary(alias => alias.Name, StringComparer.Ordinal);
        var declarationOrder = document.Aliases
            .Select((alias, index) => (alias.Name, index))
            .ToDictionary(item => item.Name, item => item.index, StringComparer.Ordinal);
        var states = new Dictionary<string, EDeclarationVisitState>(StringComparer.Ordinal);
        var result = new List<RpcAliasSchema>(document.Aliases.Count);

        void Visit(RpcAliasSchema alias)
        {
            EDeclarationVisitState state = states.GetValueOrDefault(alias.Name);
            if (state == EDeclarationVisitState.Visited)
            {
                return;
            }

            if (state == EDeclarationVisitState.Visiting)
            {
                throw new InvalidOperationException($"Cyclic RPC alias dependency detected at '{alias.Name}': {document.SourcePath}");
            }

            states[alias.Name] = EDeclarationVisitState.Visiting;
            IEnumerable<RpcAliasSchema> dependencies = EnumerateReferencedTypeNames(alias.Type)
                .Where(aliasesByName.ContainsKey)
                .Distinct(StringComparer.Ordinal)
                .OrderBy(name => declarationOrder[name])
                .Select(name => aliasesByName[name]);
            foreach (RpcAliasSchema dependency in dependencies)
            {
                Visit(dependency);
            }

            states[alias.Name] = EDeclarationVisitState.Visited;
            result.Add(alias);
        }

        foreach (RpcAliasSchema alias in document.Aliases)
        {
            Visit(alias);
        }

        return result;
    }

    private static IReadOnlyList<RpcStructSchema> OrderStructs(RpcSchemaDocument document)
    {
        var aliasesByName = document.Aliases.ToDictionary(alias => alias.Name, StringComparer.Ordinal);
        var structsByName = document.Structs.ToDictionary(structSchema => structSchema.Name, StringComparer.Ordinal);
        var declarationOrder = document.Structs
            .Select((structSchema, index) => (structSchema.Name, index))
            .ToDictionary(item => item.Name, item => item.index, StringComparer.Ordinal);
        var states = new Dictionary<string, EDeclarationVisitState>(StringComparer.Ordinal);
        var result = new List<RpcStructSchema>(document.Structs.Count);

        void Visit(RpcStructSchema structSchema)
        {
            EDeclarationVisitState state = states.GetValueOrDefault(structSchema.Name);
            if (state == EDeclarationVisitState.Visited)
            {
                return;
            }

            if (state == EDeclarationVisitState.Visiting)
            {
                throw new InvalidOperationException(
                    $"Cyclic RPC struct value dependency detected at '{structSchema.Name}': {document.SourcePath}");
            }

            states[structSchema.Name] = EDeclarationVisitState.Visiting;
            var dependencyNames = new HashSet<string>(StringComparer.Ordinal);
            foreach (RpcFieldSchema field in structSchema.Fields)
            {
                CollectStructDependencies(
                    field.Type,
                    aliasesByName,
                    structsByName,
                    dependencyNames,
                    new HashSet<string>(StringComparer.Ordinal));
            }

            foreach (string dependencyName in dependencyNames.OrderBy(name => declarationOrder[name]))
            {
                Visit(structsByName[dependencyName]);
            }

            states[structSchema.Name] = EDeclarationVisitState.Visited;
            result.Add(structSchema);
        }

        foreach (RpcStructSchema structSchema in document.Structs)
        {
            Visit(structSchema);
        }

        return result;
    }

    private static void CollectStructDependencies(
        string type,
        IReadOnlyDictionary<string, RpcAliasSchema> aliasesByName,
        IReadOnlyDictionary<string, RpcStructSchema> structsByName,
        ISet<string> output,
        ISet<string> expandingAliases)
    {
        CollectStructDependencies(RpcTypeParser.Parse(type), aliasesByName, structsByName, output, expandingAliases);
    }

    private static void CollectStructDependencies(
        RpcTypeNode type,
        IReadOnlyDictionary<string, RpcAliasSchema> aliasesByName,
        IReadOnlyDictionary<string, RpcStructSchema> structsByName,
        ISet<string> output,
        ISet<string> expandingAliases)
    {
        if (structsByName.ContainsKey(type.Name))
        {
            output.Add(type.Name);
        }
        else if (aliasesByName.TryGetValue(type.Name, out RpcAliasSchema? alias) && expandingAliases.Add(alias.Name))
        {
            CollectStructDependencies(alias.Type, aliasesByName, structsByName, output, expandingAliases);
            expandingAliases.Remove(alias.Name);
        }

        foreach (RpcTypeNode typeArgument in type.TypeArguments)
        {
            CollectStructDependencies(typeArgument, aliasesByName, structsByName, output, expandingAliases);
        }
    }

    private static IEnumerable<string> EnumerateReferencedTypeNames(string type)
    {
        return EnumerateReferencedTypeNames(RpcTypeParser.Parse(type));
    }

    private static IEnumerable<string> EnumerateReferencedTypeNames(RpcTypeNode type)
    {
        yield return type.Name;
        foreach (RpcTypeNode typeArgument in type.TypeArguments)
        {
            foreach (string nestedName in EnumerateReferencedTypeNames(typeArgument))
            {
                yield return nestedName;
            }
        }
    }

    private static void AppendStruct(StringBuilder builder, RpcStructSchema structSchema, RpcTypeRegistry types)
    {
        builder.AppendLine();
        builder.AppendLine($"\tstruct {structSchema.Name} final");
        builder.AppendLine("\t{");
        foreach (RpcFieldSchema field in structSchema.Fields)
        {
            builder.AppendLine($"\t\t{types.RenderCpp(field.Type)} {field.Name}{{}};");
        }

        builder.AppendLine();
        builder.AppendLine("\t\tvoid Serialize(NetworkLib::Packet::Serialization::FPacketWriter& writer) const");
        builder.AppendLine("\t\t{");
        foreach (RpcFieldSchema field in structSchema.Fields)
        {
            builder.AppendLine($"\t\t\tif (!RpcLib::Protocol::WriteRpcValue(writer, {field.Name}))");
            builder.AppendLine("\t\t\t{");
            builder.AppendLine("\t\t\t\tthrow std::runtime_error(\"RPC struct serialization failed.\");");
            builder.AppendLine("\t\t\t}");
        }

        builder.AppendLine("\t\t}");
        builder.AppendLine();
        builder.AppendLine("\t\tbool Deserialize(NetworkLib::Packet::Serialization::FPacketReader& reader)");
        builder.AppendLine("\t\t{");
        if (structSchema.Fields.Count == 0)
        {
            builder.AppendLine("\t\t\t(void)reader;");
            builder.AppendLine("\t\t\treturn true;");
        }
        else
        {
            builder.Append("\t\t\treturn ");
            for (int index = 0; index < structSchema.Fields.Count; ++index)
            {
                RpcFieldSchema field = structSchema.Fields[index];
                if (index > 0)
                {
                    builder.Append(" &&\n\t\t\t\t");
                }

                builder.Append($"RpcLib::Protocol::ReadRpcValue(reader, {field.Name})");
            }

            builder.AppendLine(";");
        }

        builder.AppendLine("\t\t}");
        builder.AppendLine("\t};");
    }

    private static void AppendRpcDescriptor(
        StringBuilder builder,
        RpcSchemaDocument document,
        RpcMethodSchema method,
        RpcTypeRegistry types)
    {
        builder.AppendLine();
        builder.AppendLine($"\tstruct F{method.Name}Rpc final");
        builder.AppendLine("\t{");
        AppendDescriptorMetadata(builder, document, method, method.Request!);
        builder.AppendLine();
        builder.AppendLine($"\t\tusing FRequestArguments = {RenderTuple(method.Request!.Fields, types)};");
        builder.AppendLine($"\t\tusing FResponseArguments = {RenderTuple(method.Response!.Fields, types)};");
        builder.AppendLine("\t};");
    }

    private static void AppendNotificationDescriptor(
        StringBuilder builder,
        RpcSchemaDocument document,
        RpcMethodSchema method,
        RpcTypeRegistry types)
    {
        builder.AppendLine();
        builder.AppendLine($"\tstruct F{method.Name}Noti final");
        builder.AppendLine("\t{");
        AppendDescriptorMetadata(builder, document, method, method.Noti!);
        builder.AppendLine();
        builder.AppendLine($"\t\tusing FArguments = {RenderTuple(method.Noti!.Fields, types)};");
        builder.AppendLine("\t};");
    }

    private static void AppendDescriptorMetadata(
        StringBuilder builder,
        RpcSchemaDocument document,
        RpcMethodSchema method,
        RpcEndpointSchema routedEndpoint)
    {
        builder.AppendLine($"\t\tstatic constexpr RpcLib::Protocol::FRpcServiceId kServiceId = k{document.Service.Name}ServiceId;");
        builder.AppendLine($"\t\tstatic constexpr RpcLib::Protocol::FRpcMethodId kMethodId = {method.Id};");
        builder.AppendLine($"\t\tstatic constexpr const char* kName = \"{Escape(document.Service.Name)}.{Escape(method.Name)}\";");
        bool hasRoutingKey = method.RoutingKey != null;
        int routingKeyIndex = hasRoutingKey
            ? routedEndpoint.Fields.FindIndex(field => string.Equals(field.Name, method.RoutingKey, StringComparison.Ordinal))
            : 0;
        builder.AppendLine($"\t\tstatic constexpr bool kHasRoutingKey = {hasRoutingKey.ToString().ToLowerInvariant()};");
        builder.AppendLine($"\t\tstatic constexpr std::size_t kRoutingKeyArgumentIndex = {routingKeyIndex};");
    }

    private static string RenderTuple(IReadOnlyList<RpcFieldSchema> fields, RpcTypeRegistry types)
    {
        return fields.Count == 0
            ? "std::tuple<>"
            : $"std::tuple<{string.Join(", ", fields.Select(field => types.RenderCpp(field.Type)))}>";
    }

    private static string GenerateCatalogHeader(IReadOnlyList<RpcSchemaDocument> documents)
    {
        var builder = new StringBuilder();
        builder.AppendLine("#pragma once");
        builder.AppendLine();
        builder.AppendLine("// Generated from RPC YAML. Keep deterministic layout; do not format by hand.");
        builder.AppendLine("// clang-format off");
        builder.AppendLine("namespace Generated::Rpc");
        builder.AppendLine("{");
        builder.AppendLine("\tstruct FRpcMethodCatalogEntry final");
        builder.AppendLine("\t{");
        builder.AppendLine("\t\tstd::uint32_t serviceId = 0;");
        builder.AppendLine("\t\tstd::uint32_t methodId = 0;");
        builder.AppendLine("\t\tconst char* name = nullptr;");
        builder.AppendLine("\t\tconst char* routingKey = nullptr;");
        builder.AppendLine("\t\tbool hasRequestResponse = false;");
        builder.AppendLine("\t\tbool hasNotification = false;");
        builder.AppendLine("\t};");
        builder.AppendLine();
        builder.AppendLine("\tinline constexpr FRpcMethodCatalogEntry kRpcMethodCatalog[] =");
        builder.AppendLine("\t{");
        foreach ((RpcSchemaDocument document, RpcMethodSchema method) in documents
                     .SelectMany(document => document.Methods.Select(method => (document, method)))
                     .OrderBy(item => item.document.Service.Id)
                     .ThenBy(item => item.method.Id)
                     .ThenBy(item => item.method.Name, StringComparer.Ordinal))
        {
            string routingKey = method.RoutingKey == null ? "nullptr" : $"\"{Escape(method.RoutingKey)}\"";
            builder.AppendLine(
                $"\t\t{{{document.Service.Id}, {method.Id}, \"{Escape(document.Service.Name)}.{Escape(method.Name)}\", {routingKey}, " +
                $"{(method.Request != null).ToString().ToLowerInvariant()}, {(method.Noti != null).ToString().ToLowerInvariant()}}},");
        }

        builder.AppendLine("\t};");
        builder.AppendLine("}");
        builder.AppendLine("// clang-format on");
        return NormalizeNewlines(builder.ToString());
    }

    private static string NormalizePath(string path)
    {
        return path.Replace('\\', '/');
    }

    private static string NormalizeNewlines(string value)
    {
        return value.Replace("\r\n", "\n", StringComparison.Ordinal).Replace('\r', '\n');
    }

    private static string Escape(string value)
    {
        return value.Replace("\\", "\\\\", StringComparison.Ordinal).Replace("\"", "\\\"", StringComparison.Ordinal);
    }

    private enum EDeclarationVisitState : byte
    {
        Unvisited = 0,
        Visiting = 1,
        Visited = 2,
    }
}

internal static class RpcFieldListExtensions
{
    public static int FindIndex(this IReadOnlyList<RpcFieldSchema> fields, Func<RpcFieldSchema, bool> predicate)
    {
        for (int index = 0; index < fields.Count; ++index)
        {
            if (predicate(fields[index]))
            {
                return index;
            }
        }

        return -1;
    }
}
