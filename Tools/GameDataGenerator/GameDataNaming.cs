using System.Globalization;
using System.Text;

internal static class FGameDataNaming
{
    private static readonly HashSet<string> CppKeywords = new(StringComparer.Ordinal)
    {
        "alignas", "alignof", "and", "and_eq", "asm", "auto", "bitand", "bitor", "bool", "break", "case", "catch",
        "char", "char8_t", "char16_t", "char32_t", "class", "compl", "concept", "const", "consteval", "constexpr",
        "constinit", "const_cast", "continue", "co_await", "co_return", "co_yield", "decltype", "default", "delete", "do",
        "double", "dynamic_cast", "else", "enum", "explicit", "export", "extern", "false", "float", "for", "friend", "goto",
        "if", "inline", "int", "long", "mutable", "namespace", "new", "noexcept", "not", "not_eq", "nullptr", "operator",
        "or", "or_eq", "private", "protected", "public", "register", "reinterpret_cast", "requires", "return", "short",
        "signed", "sizeof", "static", "static_assert", "static_cast", "struct", "switch", "template", "this", "thread_local",
        "throw", "true", "try", "typedef", "typeid", "typename", "union", "unsigned", "using", "virtual", "void", "volatile",
        "wchar_t", "while", "xor", "xor_eq",
    };

    private static readonly HashSet<string> CSharpKeywords = new(StringComparer.Ordinal)
    {
        "abstract", "as", "base", "bool", "break", "byte", "case", "catch", "char", "checked", "class", "const", "continue",
        "decimal", "default", "delegate", "do", "double", "else", "enum", "event", "explicit", "extern", "false", "finally",
        "fixed", "float", "for", "foreach", "goto", "if", "implicit", "in", "int", "interface", "internal", "is", "lock",
        "long", "namespace", "new", "null", "object", "operator", "out", "override", "params", "private", "protected", "public",
        "readonly", "ref", "return", "sbyte", "sealed", "short", "sizeof", "stackalloc", "static", "string", "struct", "switch",
        "this", "throw", "true", "try", "typeof", "uint", "ulong", "unchecked", "unsafe", "ushort", "using", "virtual", "void",
        "volatile", "while", "add", "alias", "and", "ascending", "async", "await", "by", "descending", "dynamic", "equals",
        "file", "from", "get", "global", "group", "init", "into", "join", "let", "managed", "nameof", "nint", "not", "notnull",
        "nuint", "on", "or", "orderby", "partial", "record", "remove", "required", "scoped", "select", "set", "unmanaged",
        "value", "var", "when", "where", "with", "yield",
    };

    public static bool IsIdentifier(string text)
    {
        return IsLexicalIdentifier(text) &&
            !CppKeywords.Contains(text) &&
            !CSharpKeywords.Contains(text);
    }

    public static bool IsCppIdentifier(string text) => IsLexicalIdentifier(text) && !CppKeywords.Contains(text);

    public static bool IsCSharpIdentifier(string text) => IsLexicalIdentifier(text) && !CSharpKeywords.Contains(text);

    private static bool IsLexicalIdentifier(string text)
    {
        if (string.IsNullOrWhiteSpace(text) ||
            !(char.IsLetter(text[0]) || text[0] == '_') ||
            !text.Any(char.IsLetterOrDigit))
        {
            return false;
        }

        return text.Skip(1).All(character => char.IsLetterOrDigit(character) || character == '_');
    }

    public static string ToPascalCase(string text)
    {
        var builder = new StringBuilder(text.Length);
        bool uppercaseNext = true;
        foreach (char character in text)
        {
            if (!char.IsLetterOrDigit(character))
            {
                uppercaseNext = true;
                continue;
            }

            builder.Append(uppercaseNext ? char.ToUpperInvariant(character) : character);
            uppercaseNext = false;
        }
        return builder.ToString();
    }

    public static string ToCppMemberName(string tableName, string fieldName)
    {
        if (string.Equals(tableName, "Item", StringComparison.OrdinalIgnoreCase) && fieldName == "Int")
        {
            return "intelligence";
        }

        string pascal = ToPascalCase(fieldName);
        return pascal.Length == 0 ? pascal : char.ToLowerInvariant(pascal[0]) + pascal[1..];
    }
}

internal static class FGameDataText
{
    public static string FormatScalar(object value, FGameDataType type)
    {
        return type.Kind switch
        {
            EGameDataScalarKind.Bool => (bool)value ? "true" : "false",
            EGameDataScalarKind.Float => ((float)value).ToString("R", CultureInfo.InvariantCulture),
            EGameDataScalarKind.Double => ((double)value).ToString("R", CultureInfo.InvariantCulture),
            EGameDataScalarKind.Int32 => ((int)value).ToString(CultureInfo.InvariantCulture),
            EGameDataScalarKind.UInt32 => ((uint)value).ToString(CultureInfo.InvariantCulture),
            EGameDataScalarKind.Int64 => ((long)value).ToString(CultureInfo.InvariantCulture),
            EGameDataScalarKind.UInt64 => ((ulong)value).ToString(CultureInfo.InvariantCulture),
            _ => (string)value,
        };
    }

    public static string QuoteYaml(string value)
    {
        return '"' + value
            .Replace("\\", "\\\\", StringComparison.Ordinal)
            .Replace("\"", "\\\"", StringComparison.Ordinal)
            .Replace("\r", "\\r", StringComparison.Ordinal)
            .Replace("\n", "\\n", StringComparison.Ordinal)
            .Replace("\t", "\\t", StringComparison.Ordinal) + '"';
    }
}
