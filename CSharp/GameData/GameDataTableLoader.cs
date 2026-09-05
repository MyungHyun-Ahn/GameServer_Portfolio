using System.Collections.ObjectModel;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace GameData;

public static class GameDataTableLoader
{
    private static readonly JsonSerializerOptions s_jsonOptions = new()
    {
        PropertyNameCaseInsensitive = false,
        UnmappedMemberHandling = JsonUnmappedMemberHandling.Disallow,
        Converters =
        {
            new JsonStringEnumConverter(allowIntegerValues: false),
        },
    };

    public static IReadOnlyDictionary<TKey, TRow> Load<TKey, TRow>(
        string path,
        Func<TRow, string?>? validateRow = null)
        where TKey : notnull
        where TRow : GameDataRow<TKey>
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(path);

        if (!File.Exists(path))
        {
            throw new FileNotFoundException($"Game data file was not found: {path}", path);
        }

        IReadOnlyList<TRow> rows;
        try
        {
            using FileStream stream = File.OpenRead(path);
            rows = JsonSerializer.Deserialize<List<TRow>>(stream, s_jsonOptions)
                ?? throw new InvalidDataException($"Game data root must be a JSON array: {path}");
        }
        catch (JsonException exception)
        {
            throw new InvalidDataException(
                $"Game data JSON is invalid. path={path} line={exception.LineNumber} byte={exception.BytePositionInLine}",
                exception);
        }

        if (rows.Count == 0)
        {
            throw new InvalidDataException($"Game data table must contain at least one row: {path}");
        }

        Dictionary<TKey, TRow> rowsById = new(rows.Count);
        for (int rowIndex = 0; rowIndex < rows.Count; ++rowIndex)
        {
            TRow row = rows[rowIndex]
                ?? throw new InvalidDataException($"Game data row cannot be null. path={path} row={rowIndex + 1}");

            string? validationError = validateRow?.Invoke(row);
            if (validationError is not null)
            {
                throw new InvalidDataException(
                    $"Game data row is invalid. path={path} row={rowIndex + 1} dataId={row.DataId}: {validationError}");
            }

            if (!rowsById.TryAdd(row.DataId, row))
            {
                throw new InvalidDataException(
                    $"Game data contains a duplicate key. path={path} row={rowIndex + 1} dataId={row.DataId}");
            }
        }

        return new ReadOnlyDictionary<TKey, TRow>(rowsById);
    }
}
