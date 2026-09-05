using System.Text;

internal static class CppPacketGenerator
{
    public static string GeneratePacketsHeader(PacketProtocolDocument document)
    {
        var builder = new StringBuilder();
        builder.AppendLine("#pragma once");
        builder.AppendLine();
        builder.AppendLine($"namespace Generated::{document.Content}");
        builder.AppendLine("{");

        foreach (PacketDefinition packet in document.Packets)
        {
            AppendPacketClass(builder, packet);
        }

        builder.AppendLine("}");
        return builder.ToString();
    }

    public static string GenerateHandlerHeader(PacketProtocolDocument document)
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

        foreach (PacketDefinition packet in document.Packets)
        {
            AppendHandlerDeclaration(builder, packet);
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

        foreach (PacketDefinition packet in document.Packets)
        {
            AppendDispatchCase(builder, packet);
        }

        builder.AppendLine("\t\t\tdefault:");
        builder.AppendLine("\t\t\t\treturn OnUnhandledPacket(server, sessionId, packetView);");
        builder.AppendLine("\t\t\t}");
        builder.AppendLine("\t\t}");
        builder.AppendLine();

        foreach (PacketDefinition packet in document.Packets)
        {
            AppendNoOpHandler(builder, packet);
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

    public static string GenerateRouterHeader(IReadOnlyList<PacketProtocolDocument> documents)
    {
        var builder = new StringBuilder();
        builder.AppendLine("#pragma once");
        builder.AppendLine();
        builder.AppendLine("namespace Generated");
        builder.AppendLine("{");
        builder.AppendLine("\tclass FPacketRouter");
        builder.AppendLine("\t{");
        builder.AppendLine("\tpublic:");

        foreach (PacketProtocolDocument document in documents)
        {
            builder.AppendLine($"\t\tvoid Set{document.Content}Handler({document.Content}::I{document.Content}PacketDispatcher* handler) noexcept");
            builder.AppendLine("\t\t{");
            builder.AppendLine($"\t\t\tm_{PacketNaming.ToMemberName(document.Content)}Handler = handler;");
            builder.AppendLine("\t\t}");
            builder.AppendLine();
        }

        builder.AppendLine("\t\tbool DispatchPacket(NetworkLib::IServer& server, std::uint64_t sessionId, const NetworkLib::Packet::View::FPacketView& packetView)");
        builder.AppendLine("\t\t{");
        builder.AppendLine("\t\t\tswitch (packetView.opcode)");
        builder.AppendLine("\t\t\t{");

        foreach (PacketProtocolDocument document in documents)
        {
            AppendRouterDispatchCases(builder, document);
        }

        builder.AppendLine("\t\t\tdefault:");
        builder.AppendLine("\t\t\t\treturn false;");
        builder.AppendLine("\t\t\t}");
        builder.AppendLine("\t\t}");
        builder.AppendLine();
        builder.AppendLine("\tprivate:");

        foreach (PacketProtocolDocument document in documents)
        {
            builder.AppendLine($"\t\t{document.Content}::I{document.Content}PacketDispatcher* m_{PacketNaming.ToMemberName(document.Content)}Handler = nullptr;");
        }

        builder.AppendLine("\t};");
        builder.AppendLine("}");
        return builder.ToString();
    }

    private static void AppendPacketClass(StringBuilder builder, PacketDefinition packet)
    {
        bool containsBorrowedViews = EndpointContainsBorrowedViews(packet);
        string packetKind = PacketNaming.GetPacketKindName(packet.Kind);
        string packetBase = packet.Kind == EPacketEndpointKind.Response ? "IResponsePacket" : "IContentPacket";
        builder.AppendLine($"\tclass {packet.CppClassName} final : public NetworkLib::Packet::Serialization::{packetBase}");
        builder.AppendLine("\t{");
        builder.AppendLine("\tpublic:");
        builder.AppendLine($"\t\tstatic constexpr std::uint16_t kOpcode = {packet.Opcode};");
        builder.AppendLine($"\t\tstatic constexpr NetworkLib::Packet::Serialization::EContentPacketKind kPacketKind = NetworkLib::Packet::Serialization::EContentPacketKind::{packetKind};");
        builder.AppendLine();

        foreach (PacketFieldDefinition field in packet.Fields)
        {
            if (FieldContainsBorrowedView(field))
            {
                builder.AppendLine($"\t\tvoid Set{PacketNaming.ToCppAccessorSuffix(field.SchemaName)}Value({PacketTypeMapping.RenderCppType(field.Type)} value) noexcept");
                builder.AppendLine("\t\t{");
                builder.AppendLine($"\t\t\tm_{field.SchemaName} = value;");
                builder.AppendLine("\t\t}");
                builder.AppendLine();
                builder.AppendLine($"\t\t{PacketTypeMapping.RenderCppType(field.Type)} Get{PacketNaming.ToCppAccessorSuffix(field.SchemaName)}Value() const noexcept");
                builder.AppendLine("\t\t{");
                builder.AppendLine("\t\t\tNetworkLib::Packet::View::ValidateBorrowedViewAccess(m_borrowedViewScope);");
                builder.AppendLine($"\t\t\treturn m_{field.SchemaName};");
                builder.AppendLine("\t\t}");
                builder.AppendLine();
            }
            else
            {
                builder.AppendLine($"\t\t{PacketTypeMapping.RenderCppType(field.Type)} {field.SchemaName}{{}};");
            }
        }

        if (packet.Fields.Count > 0)
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
        if (packet.Fields.Count == 0)
        {
            builder.AppendLine("\t\t\treturn 0;");
        }
        else
        {
            builder.Append("\t\t\treturn ");
            for (int index = 0; index < packet.Fields.Count; ++index)
            {
                PacketFieldDefinition field = packet.Fields[index];
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
        foreach (PacketFieldDefinition field in packet.Fields)
        {
            builder.AppendLine($"\t\t\twriter.Write({RenderFieldAccess(field)});");
        }
        builder.AppendLine("\t\t}");
        builder.AppendLine();
        builder.AppendLine("\t\tbool Deserialize(NetworkLib::Packet::Serialization::FPacketReader& reader) override");
        builder.AppendLine("\t\t{");
        if (packet.Fields.Count == 0)
        {
            builder.AppendLine("\t\t\treturn true;");
        }
        else
        {
            builder.Append("\t\t\treturn ");
            for (int index = 0; index < packet.Fields.Count; ++index)
            {
                PacketFieldDefinition field = packet.Fields[index];
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
            foreach (PacketFieldDefinition field in packet.Fields)
            {
                if (FieldContainsBorrowedView(field))
                {
                    builder.AppendLine($"\t\t{PacketTypeMapping.RenderCppType(field.Type)} m_{field.SchemaName}{{}};");
                }
            }
        }
        builder.AppendLine("\t};");
        builder.AppendLine();
    }

    private static void AppendHandlerDeclaration(StringBuilder builder, PacketDefinition packet)
    {
        builder.AppendLine($"\t\tvirtual bool {packet.HandlerMethodName}(NetworkLib::IServer& server, std::uint64_t sessionId, const {packet.CppClassName}& packet) = 0;");
    }

    private static void AppendNoOpHandler(StringBuilder builder, PacketDefinition packet)
    {
        builder.AppendLine($"\t\tbool {packet.HandlerMethodName}(NetworkLib::IServer&, std::uint64_t, const {packet.CppClassName}&) override");
        builder.AppendLine("\t\t{");
        builder.AppendLine("\t\t\treturn false;");
        builder.AppendLine("\t\t}");
        builder.AppendLine();
    }

    private static void AppendDispatchCase(StringBuilder builder, PacketDefinition packet)
    {
        builder.AppendLine($"\t\t\tcase {packet.CppClassName}::kOpcode:");
        builder.AppendLine("\t\t\t\t{");
        builder.AppendLine($"\t\t\t\t\t{packet.CppClassName} packet;");
        if (EndpointContainsBorrowedViews(packet))
        {
            builder.AppendLine("\t\t\t\t\tNetworkLib::Packet::View::FBorrowedViewScope borrowedViewScope;");
            builder.AppendLine("\t\t\t\t\tpacket.BindBorrowedViewScope(borrowedViewScope.GetState());");
        }
        builder.AppendLine("\t\t\t\t\tif (!NetworkLib::Packet::Serialization::DeserializeContentPacket(packetView, packet))");
        builder.AppendLine("\t\t\t\t\t{");
        builder.AppendLine("\t\t\t\t\t\treturn false;");
        builder.AppendLine("\t\t\t\t\t}");
        builder.AppendLine();
        builder.AppendLine($"\t\t\t\t\treturn {packet.HandlerMethodName}(server, sessionId, packet);");
        builder.AppendLine("\t\t\t\t}");
    }

    private static void AppendRouterDispatchCases(StringBuilder builder, PacketProtocolDocument document)
    {
        string memberName = $"m_{PacketNaming.ToMemberName(document.Content)}Handler";
        foreach (PacketDefinition packet in document.Packets)
        {
            builder.AppendLine($"\t\t\tcase {document.Content}::{packet.CppClassName}::kOpcode:");
            builder.AppendLine($"\t\t\t\treturn {memberName} != nullptr ? {memberName}->DispatchPacket(server, sessionId, packetView) : false;");
        }
    }

    private static bool EndpointContainsBorrowedViews(PacketDefinition packet)
    {
        return packet.Fields.Any(FieldContainsBorrowedView);
    }

    private static bool FieldContainsBorrowedView(PacketFieldDefinition field)
    {
        return PacketTypeRules.ContainsBorrowedView(field.Type);
    }

    private static string RenderFieldAccess(PacketFieldDefinition field)
    {
        return FieldContainsBorrowedView(field) ? $"m_{field.SchemaName}" : field.SchemaName;
    }

}
