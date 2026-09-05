internal sealed record SGameDataDiagnostic(
    string Code,
    string Message,
    SSourceLocation Location)
{
    public override string ToString() => $"{Location}: error {Code}: {Message}";
}

internal sealed class FDiagnosticBag
{
    private readonly List<SGameDataDiagnostic> _diagnostics = [];

    public int Count => _diagnostics.Count;

    public bool HasErrors => _diagnostics.Count != 0;

    public void Add(string code, string message, SSourceLocation location)
    {
        _diagnostics.Add(new SGameDataDiagnostic(code, message, location));
    }

    public IReadOnlyList<SGameDataDiagnostic> GetSorted()
    {
        return _diagnostics
            .OrderBy(diagnostic => diagnostic.Location.FilePath, StringComparer.OrdinalIgnoreCase)
            .ThenBy(diagnostic => diagnostic.Location.SheetName, StringComparer.Ordinal)
            .ThenBy(diagnostic => diagnostic.Location.CellAddress, StringComparer.Ordinal)
            .ThenBy(diagnostic => diagnostic.Code, StringComparer.Ordinal)
            .ToArray();
    }
}
