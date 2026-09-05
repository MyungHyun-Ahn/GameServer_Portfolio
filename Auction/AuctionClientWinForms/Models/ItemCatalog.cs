namespace AuctionClientWinForms.Models;

using Generated.GameData.Item;
using Generated.GameData.Common;

internal static class ItemCatalog
{
    private static readonly IReadOnlyDictionary<uint, ItemData> s_items = ItemDataTable.Load(
        Path.Combine(AppContext.BaseDirectory, "GameData", "Item.json"));

    public static IReadOnlyList<uint> FindIds(string searchText)
    {
        string keyword = searchText.Trim();
        if (keyword.Length == 0)
        {
            return [];
        }

        return s_items.Values
            .Where(item => item.Name.Contains(keyword, StringComparison.OrdinalIgnoreCase))
            .Select(item => item.ItemDataId)
            .ToArray();
    }

    public static string GetName(uint itemDataId) =>
        s_items.TryGetValue(itemDataId, out ItemData? item) ? item.Name : $"Item {itemDataId}";

    public static string GetCategoryName(uint itemDataId) => s_items.GetValueOrDefault(itemDataId)?.Category switch
    {
        EItemCategory.Equipment => "장비",
        EItemCategory.Consumable => "소비",
        EItemCategory.Material => "재료",
        _ => "기타"
    };
}
