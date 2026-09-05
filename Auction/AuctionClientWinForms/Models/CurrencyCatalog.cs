namespace AuctionClientWinForms.Models;

using Generated.GameData.Currency;

internal static class CurrencyCatalog
{
    private static readonly IReadOnlyDictionary<uint, CurrencyData> s_currencies = CurrencyDataTable.Load(
        Path.Combine(AppContext.BaseDirectory, "GameData", "Currency.json"));

    public static string GetName(ushort currencyId) =>
        s_currencies.TryGetValue(currencyId, out CurrencyData? currency)
            ? currency.Name
            : $"Currency {currencyId}";
}
