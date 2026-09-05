namespace WorldClientCore.Simulation;

public sealed record WorldSimulationOptions
{
    public float WorldWidth { get; init; } = 1024.0f;
    public float WorldHeight { get; init; } = 1024.0f;
    public float MoveSpeed { get; init; } = 96.0f;

    public void Validate()
    {
        if (!float.IsFinite(WorldWidth) || WorldWidth <= 0.0f)
        {
            throw new ArgumentOutOfRangeException(nameof(WorldWidth));
        }
        if (!float.IsFinite(WorldHeight) || WorldHeight <= 0.0f)
        {
            throw new ArgumentOutOfRangeException(nameof(WorldHeight));
        }
        if (!float.IsFinite(MoveSpeed) || MoveSpeed <= 0.0f)
        {
            throw new ArgumentOutOfRangeException(nameof(MoveSpeed));
        }
    }
}
