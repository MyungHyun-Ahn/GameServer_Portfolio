using Generated.GameData.Monster;

namespace WorldClientWinForms.Models;

internal static class MonsterCatalog
{
    private static readonly IReadOnlyDictionary<uint, MonsterData> s_monsters = MonsterDataTable.Load(
        Path.Combine(AppContext.BaseDirectory, "GameData", "Monster.json"));

    public static IEnumerable<MonsterData> All => s_monsters.Values;

    public static MonsterData? Find(uint monsterDataId) => s_monsters.GetValueOrDefault(monsterDataId);

    public static string ResolveSpritePath(string spriteAssetKey)
    {
        string assetRoot = Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "Assets"));
        string relativePath = spriteAssetKey.Replace('/', Path.DirectorySeparatorChar);
        string fullPath = Path.GetFullPath(Path.Combine(assetRoot, relativePath));
        string rootPrefix = assetRoot.EndsWith(Path.DirectorySeparatorChar)
            ? assetRoot
            : assetRoot + Path.DirectorySeparatorChar;
        if (!fullPath.StartsWith(rootPrefix, StringComparison.OrdinalIgnoreCase))
        {
            throw new InvalidDataException($"Monster SpriteAssetKey escapes the Assets directory: {spriteAssetKey}");
        }
        if (!File.Exists(fullPath))
        {
            throw new FileNotFoundException($"Monster sprite was not found: {spriteAssetKey}", fullPath);
        }
        return fullPath;
    }
}
