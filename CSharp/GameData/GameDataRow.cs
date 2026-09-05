namespace GameData;

public abstract class GameDataRow<TKey>
    where TKey : notnull
{
    public abstract TKey DataId { get; }
}
