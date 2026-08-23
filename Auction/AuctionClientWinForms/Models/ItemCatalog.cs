namespace AuctionClientWinForms.Models;

internal sealed record ItemCatalogEntry(uint ItemDataId, string Name, byte Category);

internal static class ItemCatalog
{
    private static readonly IReadOnlyList<ItemCatalogEntry> s_items = Load();

    public static IReadOnlyList<uint> FindIds(string searchText)
    {
        string keyword = searchText.Trim();
        if (keyword.Length == 0)
        {
            return [];
        }

        return s_items
            .Where(item => item.Name.Contains(keyword, StringComparison.OrdinalIgnoreCase))
            .Select(item => item.ItemDataId)
            .ToArray();
    }

    public static string GetName(uint itemDataId) =>
        s_items.FirstOrDefault(item => item.ItemDataId == itemDataId)?.Name ?? $"Item {itemDataId}";

    public static string GetCategoryName(uint itemDataId) =>
        s_items.FirstOrDefault(item => item.ItemDataId == itemDataId)?.Category switch
        {
            1 => "장비",
            2 => "소비",
            3 => "재료",
            _ => "기타"
        };

    private static IReadOnlyList<ItemCatalogEntry> Load()
    {
        string path = Path.Combine(AppContext.BaseDirectory, "Items.yaml");
        if (!File.Exists(path))
        {
            return [];
        }

        List<ItemCatalogEntry> items = [];
        uint itemDataId = 0;
        string name = string.Empty;
        byte category = 0;
        foreach (string sourceLine in File.ReadLines(path).Append(string.Empty))
        {
            string line = sourceLine.Trim();
            if ((line.StartsWith("Item", StringComparison.Ordinal) && line.EndsWith(':')) || line.Length == 0)
            {
                if (itemDataId != 0 && name.Length != 0 && category != 0)
                {
                    items.Add(new ItemCatalogEntry(itemDataId, name, category));
                }
                itemDataId = 0;
                name = string.Empty;
                category = 0;
                continue;
            }

            if (line.StartsWith("ItemDataId:", StringComparison.Ordinal))
            {
                uint.TryParse(line["ItemDataId:".Length..].Trim(), out itemDataId);
            }
            else if (line.StartsWith("Name:", StringComparison.Ordinal))
            {
                name = line["Name:".Length..].Trim();
            }
            else if (line.StartsWith("Category:", StringComparison.Ordinal))
            {
                category = line["Category:".Length..].Trim() switch
                {
                    "Equipment" => 1,
                    "Consumable" => 2,
                    "Material" => 3,
                    _ => 0
                };
            }
        }
        return items;
    }
}
