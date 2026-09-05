using System.Globalization;

internal static class FGameDataEnumCatalog
{
    public const string TableName = "Enums";

    private static readonly string[] RequiredFields = ["EnumName", "Target", "UnderlyingType", "Values"];

    public static bool IsDefinitionTable(FGameDataTable table) =>
        string.Equals(table.Name, TableName, StringComparison.OrdinalIgnoreCase);

    public static IReadOnlyDictionary<string, FGameDataEnumDefinition> Build(
        IReadOnlyList<FGameDataTable> tables,
        FDiagnosticBag diagnostics)
    {
        FGameDataTable[] enumTables = tables.Where(IsDefinitionTable).ToArray();
        if (enumTables.Length == 0)
        {
            SSourceLocation location = tables.Count == 0
                ? new SSourceLocation("GameData/Excel", "-", "-")
                : tables[0].Locate(1, 2);
            diagnostics.Add("GD0901", "A shared enum definition table named 'Enums' is required.", location);
            return new Dictionary<string, FGameDataEnumDefinition>(StringComparer.Ordinal);
        }

        foreach (FGameDataTable enumTable in enumTables)
        {
            if (!string.Equals(enumTable.Name, TableName, StringComparison.Ordinal))
            {
                diagnostics.Add("GD0901", "Enum definition table name is case-sensitive and must be exactly 'Enums'.", enumTable.Locate(1, 2));
            }
        }

        if (enumTables.Length != 1)
        {
            foreach (FGameDataTable enumTable in enumTables)
            {
                diagnostics.Add("GD0901", "Exactly one 'Enums' table must be defined.", enumTable.Locate(1, 2));
            }
            return new Dictionary<string, FGameDataEnumDefinition>(StringComparer.Ordinal);
        }

        FGameDataTable table = enumTables[0];
        Dictionary<string, FGameDataField> fields = table.Fields
            .GroupBy(field => field.Name, StringComparer.Ordinal)
            .ToDictionary(group => group.Key, group => group.First(), StringComparer.Ordinal);

        FGameDataField[] unsupportedFields = table.Fields
            .Where(field => !RequiredFields.Contains(field.Name, StringComparer.Ordinal))
            .ToArray();
        foreach (FGameDataField field in unsupportedFields)
        {
            diagnostics.Add("GD0902", $"Enums table does not support field '{field.Name}'.", field.Location);
        }
        foreach (string fieldName in RequiredFields)
        {
            if (!fields.ContainsKey(fieldName))
            {
                diagnostics.Add("GD0902", $"Enums table requires field '{fieldName}'.", table.Locate(13, 2));
            }
        }
        if (unsupportedFields.Length != 0 || RequiredFields.Any(field => !fields.ContainsKey(field)))
        {
            return new Dictionary<string, FGameDataEnumDefinition>(StringComparer.Ordinal);
        }

        if (table.Target != EGameDataTarget.Shared)
        {
            diagnostics.Add("GD0902", "Enums table #Target must be Shared.", table.Locate(2, 2));
        }

        ValidateField(table, fields["EnumName"], mustBePrimaryKey: true, diagnostics);
        ValidateField(table, fields["Target"], mustBePrimaryKey: false, diagnostics);
        ValidateField(table, fields["UnderlyingType"], mustBePrimaryKey: false, diagnostics);
        ValidateField(table, fields["Values"], mustBePrimaryKey: false, diagnostics);

        var result = new Dictionary<string, FGameDataEnumDefinition>(StringComparer.Ordinal);
        var namesIgnoringCase = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        foreach (FGameDataRow row in table.Rows)
        {
            string name = GetString(row, "EnumName").Trim();
            SSourceLocation rowLocation = table.Locate(row.SourceRowNumber, fields["EnumName"].ColumnNumber);
            if (!FGameDataNaming.IsIdentifier(name))
            {
                diagnostics.Add("GD0903", $"Enum name '{name}' must be a valid C++/C# identifier.", rowLocation);
                continue;
            }
            if (namesIgnoringCase.TryGetValue(name, out string? existingName))
            {
                diagnostics.Add("GD0903", $"Enum name '{name}' conflicts with '{existingName}'.", rowLocation);
                continue;
            }
            namesIgnoringCase.Add(name, name);

            if (!TryParseTarget(GetString(row, "Target"), out EGameDataTarget target))
            {
                diagnostics.Add(
                    "GD0904",
                    $"Enum '{name}' Target must be Shared, Server, or Client.",
                    table.Locate(row.SourceRowNumber, fields["Target"].ColumnNumber));
                continue;
            }
            if (!TryParseUnderlyingType(GetString(row, "UnderlyingType"), out EGameDataEnumUnderlyingType underlyingType))
            {
                diagnostics.Add(
                    "GD0905",
                    $"Enum '{name}' UnderlyingType must be uint8, uint16, int32, or uint32.",
                    table.Locate(row.SourceRowNumber, fields["UnderlyingType"].ColumnNumber));
                continue;
            }

            IReadOnlyList<SEnumValue> values = ParseValues(
                name,
                GetString(row, "Values"),
                underlyingType,
                table.Locate(row.SourceRowNumber, fields["Values"].ColumnNumber),
                diagnostics);
            if (values.Count == 0)
            {
                continue;
            }

            result.Add(name, new FGameDataEnumDefinition
            {
                Name = name,
                Target = target,
                UnderlyingType = underlyingType,
                Values = values,
                Location = rowLocation,
            });
        }

        return result;
    }

    public static void ValidateReferences(
        IReadOnlyList<FGameDataTable> tables,
        IReadOnlyDictionary<string, FGameDataEnumDefinition> definitions,
        FDiagnosticBag diagnostics)
    {
        foreach (FGameDataTable table in tables.Where(table => !IsDefinitionTable(table)))
        {
            foreach (FGameDataField field in table.Fields.Where(field => field.Type?.Kind == EGameDataScalarKind.Enum))
            {
                string enumName = field.Type?.EnumName ?? string.Empty;
                if (!definitions.TryGetValue(enumName, out FGameDataEnumDefinition? definition))
                {
                    diagnostics.Add("GD0908", $"Enum field '{table.Name}.{field.Name}' references undefined enum '{enumName}'.", table.Locate(6, field.ColumnNumber));
                    continue;
                }

                if (field.AllowedValues.Count != 0)
                {
                    diagnostics.Add(
                        "GD0909",
                        $"Enum field '{table.Name}.{field.Name}' must not redefine #Allowed. Define values once in the Enums table.",
                        table.Locate(11, field.ColumnNumber));
                }

                bool usedOnServer = table.IsIncludedOnServer && field.IsIncludedOnServer;
                bool usedOnClient = table.IsIncludedOnClient && field.IsIncludedOnClient;
                if ((usedOnServer && !definition.IsIncludedOnServer) ||
                    (usedOnClient && !definition.IsIncludedOnClient))
                {
                    diagnostics.Add(
                        "GD0910",
                        $"Enum '{enumName}' Target '{definition.Target}' is incompatible with field '{table.Name}.{field.Name}' scope '{field.Scope}'.",
                        table.Locate(6, field.ColumnNumber));
                }

                HashSet<string> validValues = definition.Values
                    .Select(value => value.Name)
                    .ToHashSet(StringComparer.Ordinal);
                if (field.DefaultText != null && !validValues.Contains(field.DefaultText))
                {
                    diagnostics.Add(
                        "GD0911",
                        $"Default enum value '{field.DefaultText}' is not defined by '{enumName}'.",
                        table.Locate(10, field.ColumnNumber));
                }

                foreach (FGameDataRow row in table.Rows)
                {
                    if (!row.Values.TryGetValue(field.Name, out object? value) || value == null)
                    {
                        continue;
                    }
                    string enumValue = value as string ?? string.Empty;
                    if (!validValues.Contains(enumValue))
                    {
                        diagnostics.Add(
                            "GD0911",
                            $"Value '{enumValue}' is not defined by enum '{enumName}'.",
                            table.Locate(row.SourceRowNumber, field.ColumnNumber));
                    }
                }
            }
        }
    }

    private static void ValidateField(
        FGameDataTable table,
        FGameDataField field,
        bool mustBePrimaryKey,
        FDiagnosticBag diagnostics)
    {
        if (field.Type?.Kind != EGameDataScalarKind.String ||
            field.Scope != EGameDataScope.Shared ||
            !field.Required ||
            field.IsPrimaryKey != mustBePrimaryKey)
        {
            diagnostics.Add(
                "GD0902",
                $"Enums.{field.Name} must be required Shared string{(mustBePrimaryKey ? " primary key" : string.Empty)}.",
                field.Location);
        }
    }

    private static IReadOnlyList<SEnumValue> ParseValues(
        string enumName,
        string source,
        EGameDataEnumUnderlyingType underlyingType,
        SSourceLocation location,
        FDiagnosticBag diagnostics)
    {
        var values = new List<SEnumValue>();
        var names = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var numericValues = new HashSet<long>();
        (long minimum, long maximum) = GetRange(underlyingType);

        foreach (string token in source.Split(['|', ',', ';'], StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries))
        {
            int separatorIndex = token.IndexOf('=');
            if (separatorIndex <= 0 || separatorIndex == token.Length - 1 || token.IndexOf('=', separatorIndex + 1) >= 0)
            {
                diagnostics.Add("GD0906", $"Enum '{enumName}' value '{token}' must use Name=Number.", location);
                continue;
            }

            string name = token[..separatorIndex].Trim();
            string numberText = token[(separatorIndex + 1)..].Trim();
            if (!FGameDataNaming.IsIdentifier(name))
            {
                diagnostics.Add("GD0906", $"Enum '{enumName}' value name '{name}' is not a valid identifier.", location);
                continue;
            }
            if (!names.Add(name))
            {
                diagnostics.Add("GD0906", $"Enum '{enumName}' value name '{name}' is duplicated.", location);
                continue;
            }
            if (!long.TryParse(numberText, NumberStyles.Integer, CultureInfo.InvariantCulture, out long numericValue))
            {
                diagnostics.Add("GD0906", $"Enum '{enumName}' value '{name}' has invalid number '{numberText}'.", location);
                continue;
            }
            if (numericValue < minimum || numericValue > maximum)
            {
                diagnostics.Add(
                    "GD0907",
                    $"Enum '{enumName}' value '{name}={numericValue}' exceeds underlying type '{RenderUnderlyingType(underlyingType)}'.",
                    location);
                continue;
            }
            if (!numericValues.Add(numericValue))
            {
                diagnostics.Add("GD0906", $"Enum '{enumName}' numeric value '{numericValue}' is duplicated.", location);
                continue;
            }

            values.Add(new SEnumValue(name, numericValue));
        }

        if (values.Count == 0)
        {
            diagnostics.Add("GD0906", $"Enum '{enumName}' must define at least one explicit value.", location);
        }
        return values;
    }

    private static bool TryParseTarget(string source, out EGameDataTarget target)
    {
        if (Enum.TryParse(source.Trim(), ignoreCase: false, out target) && Enum.IsDefined(target))
        {
            return true;
        }
        target = EGameDataTarget.Shared;
        return false;
    }

    private static bool TryParseUnderlyingType(string source, out EGameDataEnumUnderlyingType underlyingType)
    {
        underlyingType = source.Trim() switch
        {
            "uint8" => EGameDataEnumUnderlyingType.UInt8,
            "uint16" => EGameDataEnumUnderlyingType.UInt16,
            "int32" => EGameDataEnumUnderlyingType.Int32,
            "uint32" => EGameDataEnumUnderlyingType.UInt32,
            _ => (EGameDataEnumUnderlyingType)(-1),
        };
        return Enum.IsDefined(underlyingType);
    }

    private static (long Minimum, long Maximum) GetRange(EGameDataEnumUnderlyingType underlyingType) => underlyingType switch
    {
        EGameDataEnumUnderlyingType.UInt8 => (byte.MinValue, byte.MaxValue),
        EGameDataEnumUnderlyingType.UInt16 => (ushort.MinValue, ushort.MaxValue),
        EGameDataEnumUnderlyingType.Int32 => (int.MinValue, int.MaxValue),
        EGameDataEnumUnderlyingType.UInt32 => (uint.MinValue, uint.MaxValue),
        _ => throw new ArgumentOutOfRangeException(nameof(underlyingType)),
    };

    public static string RenderUnderlyingType(EGameDataEnumUnderlyingType underlyingType) => underlyingType switch
    {
        EGameDataEnumUnderlyingType.UInt8 => "uint8",
        EGameDataEnumUnderlyingType.UInt16 => "uint16",
        EGameDataEnumUnderlyingType.Int32 => "int32",
        EGameDataEnumUnderlyingType.UInt32 => "uint32",
        _ => throw new ArgumentOutOfRangeException(nameof(underlyingType)),
    };

    private static string GetString(FGameDataRow row, string name) =>
        row.Values.TryGetValue(name, out object? value) ? value as string ?? string.Empty : string.Empty;
}
