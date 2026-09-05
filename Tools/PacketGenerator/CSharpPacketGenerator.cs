using System.Text;

internal static class CSharpPacketGenerator
{
    public static string GeneratePackets(PacketProtocolDocument document)
    {
        var builder = new StringBuilder();
        AppendGeneratedFileHeader(builder);
        builder.AppendLine("using System;");
        builder.AppendLine("using System.Collections.Generic;");
        builder.AppendLine("using ClientNetwork.Packet;");
        builder.AppendLine();
        builder.AppendLine($"namespace Generated.Packets.{document.Content};");
        builder.AppendLine();

        foreach (PacketDefinition packet in document.Packets)
        {
            AppendPacketClass(builder, packet);
        }

        return builder.ToString();
    }

    public static string GenerateHandler(PacketProtocolDocument document)
    {
        var builder = new StringBuilder();
        AppendGeneratedFileHeader(builder);
        builder.AppendLine("using System;");
        builder.AppendLine();
        builder.AppendLine($"namespace Generated.Packets.{document.Content};");
        builder.AppendLine();
        builder.AppendLine($"public interface I{document.Content}PacketHandler");
        builder.AppendLine("{");
        foreach (PacketDefinition packet in document.Packets)
        {
            builder.AppendLine($"    bool {packet.HandlerMethodName}({packet.CSharpClassName} packet);");
        }
        builder.AppendLine("}");
        builder.AppendLine();
        builder.AppendLine($"public interface I{document.Content}PacketDispatcher");
        builder.AppendLine("{");
        builder.AppendLine("    bool DispatchPacket(ushort opcode, ReadOnlySpan<byte> body);");
        builder.AppendLine("}");
        builder.AppendLine();
        builder.AppendLine($"public abstract class {document.Content}PacketHandlerBase : I{document.Content}PacketHandler, I{document.Content}PacketDispatcher");
        builder.AppendLine("{");
        builder.AppendLine("    public bool DispatchPacket(ushort opcode, ReadOnlySpan<byte> body)");
        builder.AppendLine("    {");
        builder.AppendLine("        switch (opcode)");
        builder.AppendLine("        {");
        foreach (PacketDefinition packet in document.Packets)
        {
            AppendDispatchCase(builder, packet);
        }
        builder.AppendLine("            default:");
        builder.AppendLine("                return OnUnhandledPacket(opcode, body);");
        builder.AppendLine("        }");
        builder.AppendLine("    }");
        builder.AppendLine();

        foreach (PacketDefinition packet in document.Packets)
        {
            builder.AppendLine($"    public virtual bool {packet.HandlerMethodName}({packet.CSharpClassName} packet)");
            builder.AppendLine("    {");
            builder.AppendLine("        return false;");
            builder.AppendLine("    }");
            builder.AppendLine();
        }

        builder.AppendLine("    protected virtual bool OnUnhandledPacket(ushort opcode, ReadOnlySpan<byte> body)");
        builder.AppendLine("    {");
        builder.AppendLine("        return false;");
        builder.AppendLine("    }");
        builder.AppendLine("}");
        return builder.ToString();
    }

    public static string GenerateRouter(IReadOnlyList<PacketProtocolDocument> documents)
    {
        var builder = new StringBuilder();
        AppendGeneratedFileHeader(builder);
        builder.AppendLine("using System;");
        builder.AppendLine();
        builder.AppendLine("namespace Generated.Packets;");
        builder.AppendLine();
        builder.AppendLine("public sealed class PacketRouter");
        builder.AppendLine("{");

        foreach (PacketProtocolDocument document in documents)
        {
            string memberName = $"_{PacketNaming.ToMemberName(document.Content)}Handler";
            string dispatcherName = $"global::Generated.Packets.{document.Content}.I{document.Content}PacketDispatcher";
            builder.AppendLine($"    public void Set{document.Content}Handler({dispatcherName}? handler)");
            builder.AppendLine("    {");
            builder.AppendLine($"        {memberName} = handler;");
            builder.AppendLine("    }");
            builder.AppendLine();
        }

        builder.AppendLine("    public bool DispatchPacket(ushort opcode, ReadOnlySpan<byte> body)");
        builder.AppendLine("    {");
        builder.AppendLine("        switch (opcode)");
        builder.AppendLine("        {");
        foreach (PacketProtocolDocument document in documents)
        {
            string memberName = $"_{PacketNaming.ToMemberName(document.Content)}Handler";
            foreach (PacketDefinition packet in document.Packets)
            {
                string packetName = $"global::Generated.Packets.{document.Content}.{packet.CSharpClassName}";
                builder.AppendLine($"            case {packetName}.OpcodeValue:");
                builder.AppendLine($"                return {memberName}?.DispatchPacket(opcode, body) ?? false;");
            }
        }
        builder.AppendLine("            default:");
        builder.AppendLine("                return false;");
        builder.AppendLine("        }");
        builder.AppendLine("    }");
        builder.AppendLine();

        foreach (PacketProtocolDocument document in documents)
        {
            string memberName = $"_{PacketNaming.ToMemberName(document.Content)}Handler";
            string dispatcherName = $"global::Generated.Packets.{document.Content}.I{document.Content}PacketDispatcher";
            builder.AppendLine($"    private {dispatcherName}? {memberName};");
        }

        builder.AppendLine("}");
        return builder.ToString();
    }

    private static void AppendGeneratedFileHeader(StringBuilder builder)
    {
        builder.AppendLine("// <auto-generated />");
        builder.AppendLine("#nullable enable");
        builder.AppendLine();
    }

    private static void AppendPacketClass(StringBuilder builder, PacketDefinition packet)
    {
        builder.AppendLine($"public sealed class {packet.CSharpClassName} : IContentPacket");
        builder.AppendLine("{");
        builder.AppendLine($"    public const ushort OpcodeValue = {packet.Opcode};");
        builder.AppendLine();
        builder.AppendLine("    public ushort Opcode => OpcodeValue;");
        builder.AppendLine();
        builder.AppendLine($"    public EContentPacketKind PacketKind => EContentPacketKind.{PacketNaming.GetPacketKindName(packet.Kind)};");

        foreach (PacketFieldDefinition field in packet.Fields)
        {
            builder.AppendLine();
            builder.AppendLine(
                $"    public {PacketTypeMapping.RenderCSharpType(field.Type)} {field.CSharpName} {{ get; set; }}{PacketTypeMapping.RenderCSharpInitializer(field.Type)}");
        }

        builder.AppendLine();
        builder.AppendLine("    public void SerializeBody(FPacketWriter writer)");
        builder.AppendLine("    {");
        int temporaryIndex = 0;
        foreach (PacketFieldDefinition field in packet.Fields)
        {
            AppendWriteValue(builder, field.Type, field.CSharpName, "        ", ref temporaryIndex);
        }
        builder.AppendLine("    }");
        builder.AppendLine();
        builder.AppendLine($"    public static bool TryDeserializeBody(ReadOnlySpan<byte> body, out {packet.CSharpClassName}? packet)");
        builder.AppendLine("    {");
        builder.AppendLine("        if (body.Length > FPacketProtocol.MaxContentBodySize)");
        builder.AppendLine("        {");
        builder.AppendLine("            packet = null;");
        builder.AppendLine("            return false;");
        builder.AppendLine("        }");
        builder.AppendLine();
        builder.AppendLine("        var reader = new FPacketReader(body);");

        temporaryIndex = 0;
        var readValues = new List<string>(packet.Fields.Count);
        foreach (PacketFieldDefinition field in packet.Fields)
        {
            readValues.Add(AppendReadValue(builder, field.Type, "        ", ref temporaryIndex));
        }

        builder.AppendLine("        if (!reader.IsAtEnd)");
        builder.AppendLine("        {");
        builder.AppendLine("            packet = null;");
        builder.AppendLine("            return false;");
        builder.AppendLine("        }");
        builder.AppendLine();
        builder.AppendLine($"        packet = new {packet.CSharpClassName}");
        builder.AppendLine("        {");
        for (int index = 0; index < packet.Fields.Count; ++index)
        {
            builder.AppendLine($"            {packet.Fields[index].CSharpName} = {readValues[index]},");
        }
        builder.AppendLine("        };");
        builder.AppendLine("        return true;");
        builder.AppendLine("    }");

        if (packet.Fields.Any(field => PacketTypeMapping.RequiresFixedArrayFactory(field.Type)))
        {
            builder.AppendLine();
            builder.AppendLine("    private static T[] CreateFixedArray<T>(int length, Func<T> elementFactory)");
            builder.AppendLine("    {");
            builder.AppendLine("        var values = new T[length];");
            builder.AppendLine("        for (int index = 0; index < values.Length; ++index)");
            builder.AppendLine("        {");
            builder.AppendLine("            values[index] = elementFactory();");
            builder.AppendLine("        }");
            builder.AppendLine();
            builder.AppendLine("        return values;");
            builder.AppendLine("    }");
        }

        builder.AppendLine("}");
        builder.AppendLine();
    }

    private static void AppendDispatchCase(StringBuilder builder, PacketDefinition packet)
    {
        builder.AppendLine($"            case {packet.CSharpClassName}.OpcodeValue:");
        builder.AppendLine("            {");
        builder.AppendLine($"                if (!{packet.CSharpClassName}.TryDeserializeBody(body, out {packet.CSharpClassName}? packet) || packet is null)");
        builder.AppendLine("                {");
        builder.AppendLine("                    return false;");
        builder.AppendLine("                }");
        builder.AppendLine();
        builder.AppendLine($"                return {packet.HandlerMethodName}(packet);");
        builder.AppendLine("            }");
    }

    private static void AppendWriteValue(
        StringBuilder builder,
        SchemaTypeNode type,
        string expression,
        string indent,
        ref int temporaryIndex)
    {
        if (TryGetWriterMethod(type.Name, out string? writerMethod))
        {
            builder.AppendLine($"{indent}writer.{writerMethod}({expression});");
            return;
        }

        if (type.Name == "vector")
        {
            int index = temporaryIndex++;
            string itemName = $"item{index}";
            builder.AppendLine($"{indent}if ({expression} is null)");
            builder.AppendLine($"{indent}{{");
            builder.AppendLine($"{indent}    throw new InvalidOperationException(\"Packet collection cannot be null.\");");
            builder.AppendLine($"{indent}}}");
            builder.AppendLine();
            builder.AppendLine($"{indent}writer.WriteUInt32(checked((uint){expression}.Count));");
            builder.AppendLine($"{indent}foreach ({PacketTypeMapping.RenderCSharpType(type.TypeArguments[0])} {itemName} in {expression})");
            builder.AppendLine($"{indent}{{");
            AppendWriteValue(builder, type.TypeArguments[0], itemName, indent + "    ", ref temporaryIndex);
            builder.AppendLine($"{indent}}}");
            return;
        }

        if (type.Name == "array")
        {
            int index = temporaryIndex++;
            string itemName = $"item{index}";
            string fixedLength = type.LiteralArguments[0];
            builder.AppendLine($"{indent}if ({expression} is null || {expression}.Length != {fixedLength})");
            builder.AppendLine($"{indent}{{");
            builder.AppendLine($"{indent}    throw new InvalidOperationException(\"Packet fixed array length must be {fixedLength}.\");");
            builder.AppendLine($"{indent}}}");
            builder.AppendLine();
            builder.AppendLine($"{indent}foreach ({PacketTypeMapping.RenderCSharpType(type.TypeArguments[0])} {itemName} in {expression})");
            builder.AppendLine($"{indent}{{");
            AppendWriteValue(builder, type.TypeArguments[0], itemName, indent + "    ", ref temporaryIndex);
            builder.AppendLine($"{indent}}}");
            return;
        }

        throw new InvalidOperationException($"Unsupported C# write type: {type.OriginalText}");
    }

    private static string AppendReadValue(
        StringBuilder builder,
        SchemaTypeNode type,
        string indent,
        ref int temporaryIndex)
    {
        int currentIndex = temporaryIndex++;
        string valueName = $"value{currentIndex}";

        if (TryGetReaderMethod(type.Name, out string? readerMethod))
        {
            builder.AppendLine(
                $"{indent}if (!reader.{readerMethod}(out {PacketTypeMapping.RenderCSharpType(type)} {valueName}))");
            AppendReadFailure(builder, indent);
            builder.AppendLine();
            return valueName;
        }

        if (type.Name == "vector")
        {
            string countName = $"count{currentIndex}";
            ulong minimumElementSize = PacketTypeRules.GetMinimumSerializedSize(type.TypeArguments[0]);
            builder.AppendLine(
                $"{indent}if (!reader.TryReadCount(out uint {countName}) || {countName} > int.MaxValue || (ulong){countName} * {minimumElementSize}UL > (ulong)reader.Remaining)");
            AppendReadFailure(builder, indent);
            builder.AppendLine();
            builder.AppendLine($"{indent}var {valueName} = new List<{PacketTypeMapping.RenderCSharpType(type.TypeArguments[0])}>((int){countName});");
            builder.AppendLine($"{indent}for (uint index{currentIndex} = 0; index{currentIndex} < {countName}; ++index{currentIndex})");
            builder.AppendLine($"{indent}{{");
            string itemName = AppendReadValue(builder, type.TypeArguments[0], indent + "    ", ref temporaryIndex);
            builder.AppendLine($"{indent}    {valueName}.Add({itemName});");
            builder.AppendLine($"{indent}}}");
            builder.AppendLine();
            return valueName;
        }

        if (type.Name == "array")
        {
            string fixedLength = type.LiteralArguments[0];
            ulong minimumElementSize = PacketTypeRules.GetMinimumSerializedSize(type.TypeArguments[0]);
            builder.AppendLine($"{indent}if ({fixedLength}UL * {minimumElementSize}UL > (ulong)reader.Remaining)");
            AppendReadFailure(builder, indent);
            builder.AppendLine();
            builder.AppendLine(
                $"{indent}var {valueName} = {PacketTypeMapping.RenderCSharpArrayAllocation(type)};");
            builder.AppendLine($"{indent}for (int index{currentIndex} = 0; index{currentIndex} < {fixedLength}; ++index{currentIndex})");
            builder.AppendLine($"{indent}{{");
            string itemName = AppendReadValue(builder, type.TypeArguments[0], indent + "    ", ref temporaryIndex);
            builder.AppendLine($"{indent}    {valueName}[index{currentIndex}] = {itemName};");
            builder.AppendLine($"{indent}}}");
            builder.AppendLine();
            return valueName;
        }

        throw new InvalidOperationException($"Unsupported C# read type: {type.OriginalText}");
    }

    private static void AppendReadFailure(StringBuilder builder, string indent)
    {
        builder.AppendLine($"{indent}{{");
        builder.AppendLine($"{indent}    packet = null;");
        builder.AppendLine($"{indent}    return false;");
        builder.AppendLine($"{indent}}}");
    }

    private static bool TryGetWriterMethod(string schemaTypeName, out string? methodName)
    {
        methodName = schemaTypeName switch
        {
            "bool" => "WriteBool",
            "int8" => "WriteInt8",
            "uint8" => "WriteUInt8",
            "int16" => "WriteInt16",
            "uint16" => "WriteUInt16",
            "int32" => "WriteInt32",
            "uint32" => "WriteUInt32",
            "int64" => "WriteInt64",
            "uint64" => "WriteUInt64",
            "float" => "WriteFloat",
            "double" => "WriteDouble",
            "string" or "string_view" => "WriteString",
            "bytes" or "bytes_view" => "WriteBytes",
            _ => null
        };
        return methodName != null;
    }

    private static bool TryGetReaderMethod(string schemaTypeName, out string? methodName)
    {
        methodName = schemaTypeName switch
        {
            "bool" => "TryReadBool",
            "int8" => "TryReadInt8",
            "uint8" => "TryReadUInt8",
            "int16" => "TryReadInt16",
            "uint16" => "TryReadUInt16",
            "int32" => "TryReadInt32",
            "uint32" => "TryReadUInt32",
            "int64" => "TryReadInt64",
            "uint64" => "TryReadUInt64",
            "float" => "TryReadFloat",
            "double" => "TryReadDouble",
            "string" or "string_view" => "TryReadString",
            "bytes" or "bytes_view" => "TryReadBytes",
            _ => null
        };
        return methodName != null;
    }

}
