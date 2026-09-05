using System.Globalization;
using ClosedXML.Excel;

internal static class FWorkbookParser
{
    private static readonly IReadOnlyDictionary<int, string> MetadataLabels = new Dictionary<int, string>
    {
        [4] = "#Key",
        [5] = "#Scope",
        [6] = "#Type",
        [7] = "#Required",
        [8] = "#Min",
        [9] = "#Max",
        [10] = "#Default",
        [11] = "#Allowed",
        [12] = "#Reference",
        [13] = "#Field",
    };

    public static IReadOnlyList<FGameDataTable> ParseDirectory(string inputRoot, FDiagnosticBag diagnostics)
    {
        string[] workbookPaths = Directory.GetFiles(inputRoot, "*.xlsx", SearchOption.AllDirectories)
            .Where(path => !Path.GetFileName(path).StartsWith("~$", StringComparison.Ordinal))
            .OrderBy(path => Path.GetRelativePath(inputRoot, path), StringComparer.Ordinal)
            .ToArray();

        if (workbookPaths.Length == 0)
        {
            diagnostics.Add(
                "GD0001",
                $"No .xlsx files were found under input root '{inputRoot}'.",
                new SSourceLocation(inputRoot, "-", "-"));
            return [];
        }

        var tables = new List<FGameDataTable>();
        foreach (string workbookPath in workbookPaths)
        {
            ParseWorkbook(inputRoot, workbookPath, tables, diagnostics);
        }

        return tables;
    }

    private static void ParseWorkbook(
        string inputRoot,
        string workbookPath,
        List<FGameDataTable> tables,
        FDiagnosticBag diagnostics)
    {
        try
        {
            using var workbook = new XLWorkbook(workbookPath);
            foreach (IXLWorksheet worksheet in workbook.Worksheets)
            {
                if (worksheet.RangeUsed(XLCellsUsedOptions.AllContents) == null)
                {
                    continue;
                }

                tables.Add(ParseWorksheet(inputRoot, workbookPath, worksheet, diagnostics));
            }
        }
        catch (Exception exception)
        {
            diagnostics.Add(
                "GD0002",
                $"Could not read workbook: {exception.Message}",
                new SSourceLocation(workbookPath, "-", "-"));
        }
    }

    private static FGameDataTable ParseWorksheet(
        string inputRoot,
        string workbookPath,
        IXLWorksheet worksheet,
        FDiagnosticBag diagnostics)
    {
        string sheetName = worksheet.Name;
        ValidateMetadataLabel(worksheet, 1, "#Table", workbookPath, diagnostics);
        ValidateMetadataLabel(worksheet, 2, "#Target", workbookPath, diagnostics);
        foreach ((int row, string label) in MetadataLabels)
        {
            ValidateMetadataLabel(worksheet, row, label, workbookPath, diagnostics);
        }

        foreach (IXLCell cell in worksheet.CellsUsed(XLCellsUsedOptions.AllContents))
        {
            if (cell.HasFormula)
            {
                diagnostics.Add(
                    "GD0101",
                    "Formulas are not allowed in game-data workbooks. Enter the resolved value explicitly.",
                    Locate(workbookPath, sheetName, cell.Address.RowNumber, cell.Address.ColumnNumber));
            }
        }

        string tableName = GetCellText(worksheet.Cell(1, 2)).Trim();
        if (!FGameDataNaming.IsIdentifier(tableName))
        {
            diagnostics.Add(
                "GD0102",
                $"Table name '{tableName}' must be a valid C++/C# identifier.",
                Locate(workbookPath, sheetName, 1, 2));
            tableName = string.IsNullOrWhiteSpace(tableName) ? "InvalidTable" : tableName;
        }

        string targetText = GetCellText(worksheet.Cell(2, 2)).Trim();
        EGameDataTarget target = ParseTarget(targetText, workbookPath, sheetName, diagnostics);

        int lastColumn = Math.Max(2, worksheet.LastColumnUsed(XLCellsUsedOptions.AllContents)?.ColumnNumber() ?? 2);
        int lastRow = Math.Max(13, worksheet.LastRowUsed(XLCellsUsedOptions.AllContents)?.RowNumber() ?? 13);
        var fields = new List<FGameDataField>();
        for (int column = 2; column <= lastColumn; ++column)
        {
            bool hasAnyColumnContent = Enumerable.Range(4, Math.Max(10, lastRow - 3))
                .Any(row => !IsBlank(worksheet.Cell(row, column)));
            if (!hasAnyColumnContent)
            {
                continue;
            }

            string fieldName = GetCellText(worksheet.Cell(13, column)).Trim();
            if (string.IsNullOrWhiteSpace(fieldName))
            {
                diagnostics.Add(
                    "GD0103",
                    "A populated field column must define #Field.",
                    Locate(workbookPath, sheetName, 13, column));
                continue;
            }

            fields.Add(ParseField(workbookPath, sheetName, worksheet, column, fieldName, diagnostics));
        }

        var rows = new List<FGameDataRow>();
        for (int row = 14; row <= lastRow; ++row)
        {
            if (fields.All(field => IsBlank(worksheet.Cell(row, field.ColumnNumber))))
            {
                continue;
            }

            var values = new Dictionary<string, object?>(StringComparer.Ordinal);
            foreach (FGameDataField field in fields)
            {
                IXLCell cell = worksheet.Cell(row, field.ColumnNumber);
                string text = cell.HasFormula ? string.Empty : GetCellText(cell);
                object? value = ParseValueOrDefault(field, text, row, workbookPath, sheetName, diagnostics);
                values.TryAdd(field.Name, value);
            }

            rows.Add(new FGameDataRow
            {
                SourceRowNumber = row,
                Values = values,
            });
        }

        return new FGameDataTable
        {
            Name = tableName,
            Target = target,
            SourceRelativePath = Path.GetRelativePath(inputRoot, workbookPath).Replace('\\', '/'),
            SourcePath = workbookPath,
            SheetName = sheetName,
            Fields = fields,
            Rows = rows,
        };
    }

    private static FGameDataField ParseField(
        string workbookPath,
        string sheetName,
        IXLWorksheet worksheet,
        int column,
        string fieldName,
        FDiagnosticBag diagnostics)
    {
        if (!FGameDataNaming.IsIdentifier(fieldName))
        {
            diagnostics.Add(
                "GD0110",
                $"Field name '{fieldName}' must be a valid C++/C# identifier.",
                Locate(workbookPath, sheetName, 13, column));
        }

        string keyText = GetCellText(worksheet.Cell(4, column)).Trim();
        bool isPrimaryKey = ParsePrimaryKey(keyText, workbookPath, sheetName, column, diagnostics);
        string scopeText = GetCellText(worksheet.Cell(5, column)).Trim();
        EGameDataScope scope = ParseScope(scopeText, workbookPath, sheetName, column, diagnostics);
        string typeText = GetCellText(worksheet.Cell(6, column)).Trim();
        FGameDataType? type = ParseType(typeText, workbookPath, sheetName, column, diagnostics);
        string requiredText = GetCellText(worksheet.Cell(7, column)).Trim();
        bool required = ParseBooleanMetadata(requiredText, "#Required", workbookPath, sheetName, 7, column, diagnostics);
        string? minimum = NullIfWhiteSpace(GetCellText(worksheet.Cell(8, column)));
        string? maximum = NullIfWhiteSpace(GetCellText(worksheet.Cell(9, column)));
        string? defaultValue = NullIfWhiteSpace(GetCellText(worksheet.Cell(10, column)));
        IReadOnlyList<SEnumValue> allowedValues = ParseAllowed(
            GetCellText(worksheet.Cell(11, column)),
            workbookPath,
            sheetName,
            column,
            diagnostics);
        string? reference = NullIfWhiteSpace(GetCellText(worksheet.Cell(12, column)));

        return new FGameDataField
        {
            Name = fieldName,
            ColumnNumber = column,
            Scope = scope,
            Type = type,
            Required = required,
            IsPrimaryKey = isPrimaryKey,
            MinimumText = minimum,
            MaximumText = maximum,
            DefaultText = defaultValue,
            AllowedValues = allowedValues,
            Reference = reference,
            Location = Locate(workbookPath, sheetName, 13, column),
        };
    }

    private static object? ParseValueOrDefault(
        FGameDataField field,
        string text,
        int row,
        string workbookPath,
        string sheetName,
        FDiagnosticBag diagnostics)
    {
        string? valueText = NullIfWhiteSpace(text);
        bool usedDefault = false;
        if (valueText == null)
        {
            valueText = field.DefaultText;
            usedDefault = valueText != null;
            if (valueText == null)
            {
                if (field.Required)
                {
                    diagnostics.Add(
                        "GD0201",
                        $"Required field '{field.Name}' is empty and has no default.",
                        Locate(workbookPath, sheetName, row, field.ColumnNumber));
                }

                return null;
            }
        }

        if (field.Type == null)
        {
            return null;
        }

        if (!TryParseScalar(field.Type, valueText, out object? value))
        {
            if (!usedDefault)
            {
                diagnostics.Add(
                    "GD0202",
                    $"Value '{valueText}' is not valid for type '{field.Type.SourceName}'.",
                    Locate(workbookPath, sheetName, row, field.ColumnNumber));
            }
            return null;
        }

        if (!usedDefault)
        {
            ValidateAllowedValue(field, value!, valueText, row, workbookPath, sheetName, diagnostics);
            ValidateRange(field, value!, row, workbookPath, sheetName, diagnostics);
        }
        return value;
    }

    private static void ValidateAllowedValue(
        FGameDataField field,
        object value,
        string valueText,
        int row,
        string workbookPath,
        string sheetName,
        FDiagnosticBag diagnostics)
    {
        if (field.AllowedValues.Count == 0)
        {
            return;
        }

        string canonical = field.Type?.Kind == EGameDataScalarKind.Bool
            ? ((bool)value ? "true" : "false")
            : valueText.Trim();
        if (!field.AllowedValues.Any(allowed => string.Equals(allowed.Name, canonical, StringComparison.Ordinal)))
        {
            diagnostics.Add(
                "GD0203",
                $"Value '{canonical}' is not in #Allowed ({string.Join(", ", field.AllowedValues.Select(value => value.Name))}).",
                Locate(workbookPath, sheetName, row, field.ColumnNumber));
        }
    }

    private static void ValidateRange(
        FGameDataField field,
        object value,
        int row,
        string workbookPath,
        string sheetName,
        FDiagnosticBag diagnostics)
    {
        if (field.Type == null)
        {
            return;
        }

        if (!field.Type.IsNumeric && field.Type.Kind != EGameDataScalarKind.String)
        {
            return;
        }

        double measuredValue = field.Type.Kind == EGameDataScalarKind.String
            ? ((string)value).Length
            : Convert.ToDouble(value, CultureInfo.InvariantCulture);
        string measurementName = field.Type.Kind == EGameDataScalarKind.String ? "Length" : "Value";
        if (field.MinimumText != null)
        {
            if (double.TryParse(field.MinimumText, NumberStyles.Float, CultureInfo.InvariantCulture, out double minimum) && measuredValue < minimum)
            {
                diagnostics.Add(
                    "GD0206",
                    $"{measurementName} {measuredValue.ToString("R", CultureInfo.InvariantCulture)} is below minimum {minimum.ToString("R", CultureInfo.InvariantCulture)}.",
                    Locate(workbookPath, sheetName, row, field.ColumnNumber));
            }
        }

        if (field.MaximumText != null)
        {
            if (double.TryParse(field.MaximumText, NumberStyles.Float, CultureInfo.InvariantCulture, out double maximum) && measuredValue > maximum)
            {
                diagnostics.Add(
                    "GD0208",
                    $"{measurementName} {measuredValue.ToString("R", CultureInfo.InvariantCulture)} exceeds maximum {maximum.ToString("R", CultureInfo.InvariantCulture)}.",
                    Locate(workbookPath, sheetName, row, field.ColumnNumber));
            }
        }
    }

    internal static bool TryParseScalar(FGameDataType type, string text, out object? value)
    {
        string trimmed = text.Trim();
        switch (type.Kind)
        {
            case EGameDataScalarKind.Bool:
                if (bool.TryParse(trimmed, out bool boolValue))
                {
                    value = boolValue;
                    return true;
                }
                if (trimmed == "1" || trimmed == "0")
                {
                    value = trimmed == "1";
                    return true;
                }
                break;
            case EGameDataScalarKind.Int32:
                if (int.TryParse(trimmed, NumberStyles.Integer, CultureInfo.InvariantCulture, out int int32Value))
                {
                    value = int32Value;
                    return true;
                }
                break;
            case EGameDataScalarKind.UInt32:
                if (uint.TryParse(trimmed, NumberStyles.Integer, CultureInfo.InvariantCulture, out uint uint32Value))
                {
                    value = uint32Value;
                    return true;
                }
                break;
            case EGameDataScalarKind.Int64:
                if (long.TryParse(trimmed, NumberStyles.Integer, CultureInfo.InvariantCulture, out long int64Value))
                {
                    value = int64Value;
                    return true;
                }
                break;
            case EGameDataScalarKind.UInt64:
                if (ulong.TryParse(trimmed, NumberStyles.Integer, CultureInfo.InvariantCulture, out ulong uint64Value))
                {
                    value = uint64Value;
                    return true;
                }
                break;
            case EGameDataScalarKind.Float:
                if (float.TryParse(trimmed, NumberStyles.Float, CultureInfo.InvariantCulture, out float floatValue) && float.IsFinite(floatValue))
                {
                    value = floatValue;
                    return true;
                }
                break;
            case EGameDataScalarKind.Double:
                if (double.TryParse(trimmed, NumberStyles.Float, CultureInfo.InvariantCulture, out double doubleValue) && double.IsFinite(doubleValue))
                {
                    value = doubleValue;
                    return true;
                }
                break;
            case EGameDataScalarKind.String:
            case EGameDataScalarKind.Enum:
                value = text;
                return true;
        }

        value = null;
        return false;
    }

    private static EGameDataTarget ParseTarget(
        string text,
        string workbookPath,
        string sheetName,
        FDiagnosticBag diagnostics)
    {
        if (!int.TryParse(text, NumberStyles.Integer, CultureInfo.InvariantCulture, out _) &&
            Enum.TryParse(text, true, out EGameDataTarget target) &&
            Enum.IsDefined(target))
        {
            return target;
        }

        diagnostics.Add(
            "GD0120",
            $"Unknown #Target '{text}'. Expected Shared, Server, or Client.",
            Locate(workbookPath, sheetName, 2, 2));
        return EGameDataTarget.Shared;
    }

    private static EGameDataScope ParseScope(
        string text,
        string workbookPath,
        string sheetName,
        int column,
        FDiagnosticBag diagnostics)
    {
        if (!int.TryParse(text, NumberStyles.Integer, CultureInfo.InvariantCulture, out _) &&
            Enum.TryParse(text, true, out EGameDataScope scope) &&
            Enum.IsDefined(scope))
        {
            return scope;
        }

        diagnostics.Add(
            "GD0121",
            $"Unknown #Scope '{text}'. Expected Shared, Server, Client, or Ignore.",
            Locate(workbookPath, sheetName, 5, column));
        return EGameDataScope.Ignore;
    }

    private static FGameDataType? ParseType(
        string text,
        string workbookPath,
        string sheetName,
        int column,
        FDiagnosticBag diagnostics)
    {
        EGameDataScalarKind? kind = text.ToLowerInvariant() switch
        {
            "bool" => EGameDataScalarKind.Bool,
            "int32" => EGameDataScalarKind.Int32,
            "uint32" => EGameDataScalarKind.UInt32,
            "int64" => EGameDataScalarKind.Int64,
            "uint64" => EGameDataScalarKind.UInt64,
            "float" => EGameDataScalarKind.Float,
            "double" => EGameDataScalarKind.Double,
            "string" => EGameDataScalarKind.String,
            _ => null,
        };
        if (kind.HasValue)
        {
            return new FGameDataType { SourceName = text, Kind = kind.Value };
        }

        if (text.StartsWith("enum<", StringComparison.OrdinalIgnoreCase) && text.EndsWith('>'))
        {
            string enumName = text[5..^1].Trim();
            if (FGameDataNaming.IsIdentifier(enumName))
            {
                return new FGameDataType
                {
                    SourceName = text,
                    Kind = EGameDataScalarKind.Enum,
                    EnumName = enumName,
                };
            }
        }

        diagnostics.Add(
            "GD0122",
            $"Unknown #Type '{text}'.",
            Locate(workbookPath, sheetName, 6, column));
        return null;
    }

    private static bool ParsePrimaryKey(
        string text,
        string workbookPath,
        string sheetName,
        int column,
        FDiagnosticBag diagnostics)
    {
        if (string.IsNullOrWhiteSpace(text))
        {
            return false;
        }

        if (text.Equals("Primary", StringComparison.OrdinalIgnoreCase) ||
            text.Equals("PK", StringComparison.OrdinalIgnoreCase) ||
            text.Equals("Key", StringComparison.OrdinalIgnoreCase) ||
            text.Equals("true", StringComparison.OrdinalIgnoreCase) ||
            text == "1")
        {
            return true;
        }

        diagnostics.Add(
            "GD0123",
            $"Unknown #Key value '{text}'. Use Primary or leave the cell empty.",
            Locate(workbookPath, sheetName, 4, column));
        return false;
    }

    private static bool ParseBooleanMetadata(
        string text,
        string metadataName,
        string workbookPath,
        string sheetName,
        int row,
        int column,
        FDiagnosticBag diagnostics)
    {
        if (string.IsNullOrWhiteSpace(text) || text.Equals("false", StringComparison.OrdinalIgnoreCase) || text == "0" || text.Equals("no", StringComparison.OrdinalIgnoreCase))
        {
            return false;
        }

        if (text.Equals("true", StringComparison.OrdinalIgnoreCase) || text == "1" || text.Equals("yes", StringComparison.OrdinalIgnoreCase))
        {
            return true;
        }

        diagnostics.Add(
            "GD0124",
            $"Unknown {metadataName} value '{text}'. Use true or false.",
            Locate(workbookPath, sheetName, row, column));
        return false;
    }

    private static IReadOnlyList<SEnumValue> ParseAllowed(
        string text,
        string workbookPath,
        string sheetName,
        int column,
        FDiagnosticBag diagnostics)
    {
        if (string.IsNullOrWhiteSpace(text))
        {
            return [];
        }

        var values = new List<SEnumValue>();
        var names = new HashSet<string>(StringComparer.Ordinal);
        foreach (string sourceToken in text.Split(['|', ',', ';'], StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries))
        {
            string name = sourceToken;
            long? numericValue = null;
            int equalsIndex = sourceToken.IndexOf('=');
            if (equalsIndex >= 0)
            {
                name = sourceToken[..equalsIndex].Trim();
                string numericText = sourceToken[(equalsIndex + 1)..].Trim();
                if (!long.TryParse(numericText, NumberStyles.Integer, CultureInfo.InvariantCulture, out long parsedNumericValue))
                {
                    diagnostics.Add(
                        "GD0125",
                        $"Invalid numeric enum value '{numericText}'.",
                        Locate(workbookPath, sheetName, 11, column));
                }
                else
                {
                    numericValue = parsedNumericValue;
                }
            }

            if (string.IsNullOrWhiteSpace(name) || !names.Add(name))
            {
                diagnostics.Add(
                    "GD0126",
                    $"Duplicate or empty #Allowed value '{name}'.",
                    Locate(workbookPath, sheetName, 11, column));
                continue;
            }

            values.Add(new SEnumValue(name, numericValue));
        }

        return values;
    }

    private static void ValidateMetadataLabel(
        IXLWorksheet worksheet,
        int row,
        string expected,
        string workbookPath,
        FDiagnosticBag diagnostics)
    {
        string actual = GetCellText(worksheet.Cell(row, 1)).Trim();
        if (!string.Equals(actual, expected, StringComparison.Ordinal))
        {
            diagnostics.Add(
                "GD0130",
                $"Expected metadata label '{expected}', but found '{actual}'.",
                Locate(workbookPath, worksheet.Name, row, 1));
        }
    }

    private static string GetCellText(IXLCell cell)
    {
        return cell.DataType switch
        {
            XLDataType.Blank => string.Empty,
            XLDataType.Boolean => cell.GetBoolean() ? "true" : "false",
            XLDataType.Number => cell.GetDouble().ToString("R", CultureInfo.InvariantCulture),
            XLDataType.DateTime => cell.GetDateTime().ToString("O", CultureInfo.InvariantCulture),
            XLDataType.TimeSpan => cell.GetTimeSpan().ToString("c", CultureInfo.InvariantCulture),
            _ => cell.GetString(),
        };
    }

    private static bool IsBlank(IXLCell cell) => string.IsNullOrWhiteSpace(GetCellText(cell));

    private static string? NullIfWhiteSpace(string text)
    {
        string trimmed = text.Trim();
        return trimmed.Length == 0 ? null : trimmed;
    }

    private static SSourceLocation Locate(string workbookPath, string sheetName, int row, int column) =>
        new(workbookPath, sheetName, FCellAddress.ToA1(row, column));
}
