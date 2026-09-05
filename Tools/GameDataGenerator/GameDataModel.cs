using System.Globalization;

internal enum EGameDataTarget
{
    Shared,
    Server,
    Client,
}

internal enum EGameDataScope
{
    Shared,
    Server,
    Client,
    Ignore,
}

internal enum EGameDataScalarKind
{
    Bool,
    Int32,
    UInt32,
    Int64,
    UInt64,
    Float,
    Double,
    String,
    Enum,
}

internal sealed record SSourceLocation(string FilePath, string SheetName, string CellAddress)
{
    public override string ToString() => $"{FilePath} [{SheetName}] {CellAddress}";
}

internal sealed record SEnumValue(string Name, long? NumericValue);

internal enum EGameDataEnumUnderlyingType
{
    UInt8,
    UInt16,
    Int32,
    UInt32,
}

internal sealed class FGameDataEnumDefinition
{
    public required string Name { get; init; }

    public required EGameDataTarget Target { get; init; }

    public required EGameDataEnumUnderlyingType UnderlyingType { get; init; }

    public required IReadOnlyList<SEnumValue> Values { get; init; }

    public required SSourceLocation Location { get; init; }

    public bool IsIncludedOnServer => Target is EGameDataTarget.Shared or EGameDataTarget.Server;

    public bool IsIncludedOnClient => Target is EGameDataTarget.Shared or EGameDataTarget.Client;
}

internal sealed class FGameDataType
{
    public required string SourceName { get; init; }

    public required EGameDataScalarKind Kind { get; init; }

    public string? EnumName { get; init; }

    public bool IsNumeric => Kind is EGameDataScalarKind.Int32
        or EGameDataScalarKind.UInt32
        or EGameDataScalarKind.Int64
        or EGameDataScalarKind.UInt64
        or EGameDataScalarKind.Float
        or EGameDataScalarKind.Double;
}

internal sealed class FGameDataField
{
    public required string Name { get; init; }

    public required int ColumnNumber { get; init; }

    public required EGameDataScope Scope { get; init; }

    public FGameDataType? Type { get; init; }

    public required bool Required { get; init; }

    public required bool IsPrimaryKey { get; init; }

    public string? MinimumText { get; init; }

    public string? MaximumText { get; init; }

    public string? DefaultText { get; init; }

    public required IReadOnlyList<SEnumValue> AllowedValues { get; init; }

    public string? Reference { get; init; }

    public required SSourceLocation Location { get; init; }

    public bool IsIncludedOnServer => Scope is EGameDataScope.Shared or EGameDataScope.Server;

    public bool IsIncludedOnClient => Scope is EGameDataScope.Shared or EGameDataScope.Client;
}

internal sealed class FGameDataRow
{
    public required int SourceRowNumber { get; init; }

    public required IReadOnlyDictionary<string, object?> Values { get; init; }
}

internal sealed class FGameDataTable
{
    public required string Name { get; init; }

    public required EGameDataTarget Target { get; init; }

    public required string SourceRelativePath { get; init; }

    public required string SourcePath { get; init; }

    public required string SheetName { get; init; }

    public required IReadOnlyList<FGameDataField> Fields { get; init; }

    public required IReadOnlyList<FGameDataRow> Rows { get; init; }

    public FGameDataField? PrimaryKey => Fields.FirstOrDefault(field => field.IsPrimaryKey);

    public bool IsIncludedOnServer => Target is EGameDataTarget.Shared or EGameDataTarget.Server;

    public bool IsIncludedOnClient => Target is EGameDataTarget.Shared or EGameDataTarget.Client;

    public SSourceLocation Locate(int row, int column) =>
        new(SourcePath, SheetName, FCellAddress.ToA1(row, column));
}

internal static class FCellAddress
{
    public static string ToA1(int row, int column)
    {
        int value = column;
        Span<char> buffer = stackalloc char[8];
        int index = buffer.Length;
        while (value > 0)
        {
            value--;
            buffer[--index] = (char)('A' + value % 26);
            value /= 26;
        }

        return string.Concat(buffer[index..], row.ToString(CultureInfo.InvariantCulture));
    }
}
