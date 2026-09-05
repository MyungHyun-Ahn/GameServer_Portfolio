using Generated.GameData.Common;
using Generated.GameData.Monster;
using WorldClientCore.Models;
using WorldClientCore.Simulation;
using WorldClientWinForms.Models;

namespace WorldClientWinForms.Controls;

internal sealed class WorldViewControl : Control
{
    private const float SectorSize = 128.0f;
    private const int SourceFrameSize = 32;
    private const int DrawSize = 44;
    private const int NormalMonsterDrawSize = 26;
    private const int BossMonsterDrawSize = 68;
    private const int MonsterDirectionCount = 4;
    private const int MonsterAnimationFrameCount = 4;
    private const float MonsterAnimationFramesPerSecond = 8.0f;

    private readonly FWorldSimulationState m_simulation = new();
    private readonly List<WorldActorSnapshot> m_actorSnapshots = [];
    private readonly Dictionary<uint, Image> m_monsterSprites = [];
    private readonly Image m_idleSprite;
    private readonly Image m_walkSprite;
    private ulong m_selectedTargetEntityId;

    public WorldViewControl()
    {
        DoubleBuffered = true;
        BackColor = Color.FromArgb(18, 27, 35);
        TabStop = false;

        string assetDirectory = Path.Combine(AppContext.BaseDirectory, "Assets");
        m_idleSprite = Image.FromFile(Path.Combine(assetDirectory, "NinjaGreenIdle.png"));
        m_walkSprite = Image.FromFile(Path.Combine(assetDirectory, "NinjaGreenWalk.png"));
        foreach (MonsterData monster in MonsterCatalog.All)
        {
            m_monsterSprites.Add(monster.MonsterDataId, Image.FromFile(MonsterCatalog.ResolveSpritePath(monster.SpriteAssetKey)));
        }
    }

    public ulong LocalEntityId => m_simulation.LocalEntityId;
    public ulong MapInstanceId => m_simulation.MapInstanceId;
    public uint LocalCurrentHp => m_simulation.LocalCurrentHp;
    public uint LocalMaxHp => m_simulation.LocalMaxHp;
    public bool LocalIsDead => m_simulation.LocalIsDead;
    public ulong LocalLifeRevision => m_simulation.LocalLifeRevision;
    public int ActorCount => m_simulation.ActorCount;
    public ulong SelectedTargetEntityId => m_selectedTargetEntityId;

    public event Action<WorldActorSnapshot?>? TargetSelectionChanged;

    public void EnterLocal(MapEnterResult result)
    {
        ClearSelectedTarget();
        m_simulation.EnterLocal(result);
        Invalidate();
    }

    public void ClearWorld()
    {
        ClearSelectedTarget();
        m_simulation.Clear();
        Invalidate();
    }

    public void Spawn(ActorSpawnNotification notification)
    {
        m_simulation.Spawn(notification);
        Invalidate();
    }

    public void Despawn(ulong entityId)
    {
        m_simulation.Despawn(entityId);
        if (entityId == m_selectedTargetEntityId)
        {
            ClearSelectedTarget();
        }
        Invalidate();
    }

    public void ApplyMoveResult(MoveResult result)
    {
        if (m_simulation.ApplyMoveResult(result))
        {
            Invalidate();
        }
    }

    public void ApplyMoveNotification(MoveNotification notification)
    {
        if (m_simulation.ApplyMoveNotification(notification))
        {
            Invalidate();
        }
    }

    public void SetLocalInput(float directionX, float directionY, EWorldMoveState moveState, uint sequence)
    {
        m_simulation.SetLocalInput(directionX, directionY, moveState, sequence);
    }

    public void SetLocalMoveSpeed(uint moveSpeedMilli)
    {
        m_simulation.SetLocalMoveSpeed(moveSpeedMilli);
    }

    public void SetLocalHealth(uint currentHp, uint maxHp)
    {
        if (m_simulation.SetLocalHealth(currentHp, maxHp))
        {
            Invalidate();
        }
    }

    public bool ApplyAttackNotification(ActorAttackNotification notification)
    {
        bool applied = m_simulation.ApplyAttackNotification(notification);
        if (applied)
        {
            Invalidate();
        }
        return applied;
    }

    public bool ApplyDeathNotification(ActorDeathNotification notification)
    {
        bool applied = m_simulation.ApplyDeathNotification(notification);
        if (applied)
        {
            if (notification.EntityId == m_selectedTargetEntityId)
            {
                ClearSelectedTarget();
            }
            Invalidate();
        }
        return applied;
    }

    public bool ApplyRespawnNotification(ActorRespawnNotification notification)
    {
        bool applied = m_simulation.ApplyRespawnNotification(notification);
        if (applied)
        {
            Invalidate();
        }
        return applied;
    }

    public void Advance(TimeSpan elapsed)
    {
        if (m_simulation.Advance(elapsed))
        {
            Invalidate();
        }
    }

    public bool TryGetLocalSnapshot(
        out float positionX,
        out float positionY,
        out float directionX,
        out float directionY)
    {
        if (m_simulation.TryGetLocalSnapshot(out WorldActorSnapshot actor))
        {
            positionX = actor.PositionX;
            positionY = actor.PositionY;
            directionX = actor.DirectionX;
            directionY = actor.DirectionY;
            return true;
        }

        positionX = 0.0f;
        positionY = 0.0f;
        directionX = 0.0f;
        directionY = 1.0f;
        return false;
    }

    public bool TryGetSelectedLivingMonster(out WorldActorSnapshot target)
    {
        if (m_selectedTargetEntityId != 0 &&
            m_simulation.TryGetActorSnapshot(m_selectedTargetEntityId, out target) &&
            target.ActorKind == EWorldActorKind.Monster &&
            !target.IsDead)
        {
            return true;
        }

        target = default;
        return false;
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        base.OnPaint(e);
        Graphics graphics = e.Graphics;
        graphics.InterpolationMode = System.Drawing.Drawing2D.InterpolationMode.NearestNeighbor;
        graphics.PixelOffsetMode = System.Drawing.Drawing2D.PixelOffsetMode.Half;

        RectangleF viewport = CalculateViewport();
        DrawMap(graphics, viewport);
        m_simulation.CopyActorSnapshots(m_actorSnapshots);
        foreach (WorldActorSnapshot actor in m_actorSnapshots.OrderBy(static actor => actor.EntityId))
        {
            DrawActor(graphics, viewport, actor);
        }

        if (m_simulation.LocalEntityId == 0)
        {
            using Font font = new(Font.FontFamily, 14.0f, FontStyle.Bold);
            TextRenderer.DrawText(
                graphics,
                "Connect, then enter MapDataId 1",
                font,
                ClientRectangle,
                Color.FromArgb(190, 205, 215),
                TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter);
        }
    }

    protected override void OnMouseDown(MouseEventArgs e)
    {
        base.OnMouseDown(e);
        if (e.Button != MouseButtons.Left || m_simulation.LocalEntityId == 0)
        {
            return;
        }

        RectangleF viewport = CalculateViewport();
        m_simulation.CopyActorSnapshots(m_actorSnapshots);
        WorldActorSnapshot? selected = FindLivingMonsterAtPoint(m_actorSnapshots, viewport, e.Location);
        SetSelectedTarget(selected);
    }

    protected override void Dispose(bool disposing)
    {
        if (disposing)
        {
            m_idleSprite.Dispose();
            m_walkSprite.Dispose();
            foreach (Image monsterSprite in m_monsterSprites.Values)
            {
                monsterSprite.Dispose();
            }
        }
        base.Dispose(disposing);
    }

    private RectangleF CalculateViewport()
    {
        const float padding = 18.0f;
        float availableWidth = Math.Max(1.0f, ClientSize.Width - padding * 2.0f);
        float availableHeight = Math.Max(1.0f, ClientSize.Height - padding * 2.0f);
        float scale = Math.Min(availableWidth / m_simulation.WorldWidth, availableHeight / m_simulation.WorldHeight);
        float width = m_simulation.WorldWidth * scale;
        float height = m_simulation.WorldHeight * scale;
        return new RectangleF(
            (ClientSize.Width - width) * 0.5f,
            (ClientSize.Height - height) * 0.5f,
            width,
            height);
    }

    private void DrawMap(Graphics graphics, RectangleF viewport)
    {
        using SolidBrush groundBrush = new(Color.FromArgb(30, 48, 57));
        using Pen borderPen = new(Color.FromArgb(97, 129, 137), 2.0f);
        using Pen sectorPen = new(Color.FromArgb(48, 75, 82), 1.0f);
        graphics.FillRectangle(groundBrush, viewport);
        graphics.DrawRectangle(borderPen, viewport.X, viewport.Y, viewport.Width, viewport.Height);

        float xStep = viewport.Width * SectorSize / m_simulation.WorldWidth;
        float yStep = viewport.Height * SectorSize / m_simulation.WorldHeight;
        for (float x = viewport.Left + xStep; x < viewport.Right; x += xStep)
        {
            graphics.DrawLine(sectorPen, x, viewport.Top, x, viewport.Bottom);
        }
        for (float y = viewport.Top + yStep; y < viewport.Bottom; y += yStep)
        {
            graphics.DrawLine(sectorPen, viewport.Left, y, viewport.Right, y);
        }
    }

    private void DrawActor(Graphics graphics, RectangleF viewport, WorldActorSnapshot actor)
    {
        float screenX = viewport.Left + actor.PositionX / m_simulation.WorldWidth * viewport.Width;
        float screenY = viewport.Top + actor.PositionY / m_simulation.WorldHeight * viewport.Height;
        if (actor.ActorKind == EWorldActorKind.Monster)
        {
            DrawMonster(graphics, actor, screenX, screenY);
            return;
        }

        Rectangle destination = Rectangle.Round(new RectangleF(
            screenX - DrawSize * 0.5f,
            screenY - DrawSize * 0.75f,
            DrawSize,
            DrawSize));

        Image sheet = actor.IsDead || actor.MoveState == EWorldMoveState.Stop ? m_idleSprite : m_walkSprite;
        int directionColumn = DirectionColumn(actor.DirectionX, actor.DirectionY);
        int frame = actor.MoveState == EWorldMoveState.Stop
            ? 0
            : (int)(actor.AnimationTime * 9.0f) % 4;
        Rectangle source = new(directionColumn * SourceFrameSize, frame * SourceFrameSize, SourceFrameSize, SourceFrameSize);
        graphics.DrawImage(sheet, destination, source, GraphicsUnit.Pixel);

        Color ringColor = actor.IsDead ? Color.Gray : actor.IsLocal ? Color.DeepSkyBlue : Color.Orange;
        using Pen ringPen = new(ringColor, actor.IsLocal ? 3.0f : 2.0f);
        graphics.DrawEllipse(ringPen, screenX - 18.0f, screenY + 10.0f, 36.0f, 12.0f);

        string label = actor.IsLocal ? $"ME · {actor.EntityId}" : $"Actor {actor.EntityId}";
        if (actor.IsDead)
        {
            label += " · DEAD";
        }
        Size textSize = TextRenderer.MeasureText(label, Font);
        TextRenderer.DrawText(
            graphics,
            label,
            Font,
            new Point((int)(screenX - textSize.Width * 0.5f), (int)(screenY + 24.0f)),
            ringColor);

        if (actor.IsDead)
        {
            using Pen deathPen = new(Color.FromArgb(220, 225, 90, 90), 3.0f);
            graphics.DrawLine(deathPen, destination.Left, destination.Top, destination.Right, destination.Bottom);
            graphics.DrawLine(deathPen, destination.Right, destination.Top, destination.Left, destination.Bottom);
        }
    }

    private void DrawMonster(Graphics graphics, WorldActorSnapshot actor, float screenX, float screenY)
    {
        MonsterData? monster = MonsterCatalog.Find(actor.ActorDataId);
        bool isBoss = monster?.MonsterType == EMonsterType.Boss;
        int drawSize = isBoss ? BossMonsterDrawSize : NormalMonsterDrawSize;
        Rectangle destination = Rectangle.Round(new RectangleF(
            screenX - drawSize * 0.5f,
            screenY - drawSize * 0.7f,
            drawSize,
            drawSize));

        if (m_monsterSprites.TryGetValue(actor.ActorDataId, out Image? sprite))
        {
            Rectangle? source = CalculateMonsterFrameSource(
                sprite.Size,
                actor.DirectionX,
                actor.DirectionY,
                actor.MoveState,
                actor.AnimationTime,
                actor.IsDead);
            if (source.HasValue)
            {
                graphics.DrawImage(sprite, destination, source.Value, GraphicsUnit.Pixel);
            }
            else
            {
                graphics.DrawImage(sprite, destination);
            }
        }
        else
        {
            using SolidBrush fallbackBrush = new(Color.FromArgb(190, 85, 95));
            graphics.FillEllipse(fallbackBrush, destination);
        }

        bool isSelected = actor.EntityId == m_selectedTargetEntityId;
        Color ringColor = isSelected ? Color.Gold : isBoss ? Color.MediumVioletRed : Color.LightCoral;
        using Pen ringPen = new(ringColor, isSelected ? 3.5f : isBoss ? 3.0f : 2.0f);
        graphics.DrawEllipse(ringPen, screenX - drawSize * 0.42f, screenY + drawSize * 0.2f, drawSize * 0.84f, 10.0f);

        if (actor.MaxHp > 0)
        {
            DrawHealthBar(graphics, actor, destination, isSelected, isBoss);
        }

        string label = monster is null ? $"Monster {actor.ActorDataId}" : monster.Name;
        if (actor.IsDead)
        {
            label += " · DEAD";
        }
        Size textSize = TextRenderer.MeasureText(label, Font);
        TextRenderer.DrawText(
            graphics,
            label,
            Font,
            new Point((int)(screenX - textSize.Width * 0.5f), (int)(screenY + drawSize * 0.5f)),
            ringColor);
    }

    private static void DrawHealthBar(
        Graphics graphics,
        WorldActorSnapshot actor,
        Rectangle destination,
        bool isSelected,
        bool isBoss)
    {
        int barWidth = isBoss ? 68 : 42;
        int barHeight = isSelected ? 6 : 5;
        int barX = destination.Left + (destination.Width - barWidth) / 2;
        int barY = destination.Top - 10;
        Rectangle background = new(barX, barY, barWidth, barHeight);
        float ratio = Math.Clamp(actor.CurrentHp / (float)actor.MaxHp, 0.0f, 1.0f);
        Rectangle foreground = new(barX + 1, barY + 1, Math.Max(0, (int)((barWidth - 2) * ratio)), barHeight - 2);

        using SolidBrush backgroundBrush = new(Color.FromArgb(220, 35, 38, 42));
        using SolidBrush healthBrush = new(ratio > 0.5f ? Color.LimeGreen : ratio > 0.2f ? Color.Gold : Color.OrangeRed);
        using Pen borderPen = new(isSelected ? Color.Gold : Color.FromArgb(180, 210, 215, 220), 1.0f);
        graphics.FillRectangle(backgroundBrush, background);
        if (foreground.Width > 0 && foreground.Height > 0)
        {
            graphics.FillRectangle(healthBrush, foreground);
        }
        graphics.DrawRectangle(borderPen, background);
    }

    private WorldActorSnapshot? FindLivingMonsterAtPoint(
        IEnumerable<WorldActorSnapshot> actors,
        RectangleF viewport,
        Point point)
    {
        WorldActorSnapshot? best = null;
        float bestDistanceSquared = float.MaxValue;
        foreach (WorldActorSnapshot actor in actors)
        {
            if (actor.ActorKind != EWorldActorKind.Monster || actor.IsDead)
            {
                continue;
            }

            float screenX = viewport.Left + actor.PositionX / m_simulation.WorldWidth * viewport.Width;
            float screenY = viewport.Top + actor.PositionY / m_simulation.WorldHeight * viewport.Height;
            MonsterData? monster = MonsterCatalog.Find(actor.ActorDataId);
            int drawSize = monster?.MonsterType == EMonsterType.Boss ? BossMonsterDrawSize : NormalMonsterDrawSize;
            RectangleF hitBounds = new(
                screenX - drawSize * 0.65f,
                screenY - drawSize * 0.85f,
                drawSize * 1.3f,
                drawSize * 1.3f);
            if (!hitBounds.Contains(point))
            {
                continue;
            }

            float deltaX = point.X - screenX;
            float deltaY = point.Y - screenY;
            float distanceSquared = deltaX * deltaX + deltaY * deltaY;
            if (distanceSquared < bestDistanceSquared)
            {
                best = actor;
                bestDistanceSquared = distanceSquared;
            }
        }
        return best;
    }

    private void SetSelectedTarget(WorldActorSnapshot? target)
    {
        ulong nextEntityId = target?.EntityId ?? 0;
        if (m_selectedTargetEntityId == nextEntityId)
        {
            return;
        }

        m_selectedTargetEntityId = nextEntityId;
        TargetSelectionChanged?.Invoke(target);
        Invalidate();
    }

    private void ClearSelectedTarget() => SetSelectedTarget(null);

    private static int DirectionColumn(float directionX, float directionY)
    {
        if (Math.Abs(directionX) > Math.Abs(directionY))
        {
            return directionX >= 0.0f ? 3 : 2;
        }
        return directionY < 0.0f ? 1 : 0;
    }

    internal static Rectangle? CalculateMonsterFrameSource(
        Size spriteSize,
        float directionX,
        float directionY,
        EWorldMoveState moveState,
        float animationTime,
        bool isDead)
    {
        if (spriteSize.Width <= 0 ||
            spriteSize.Height <= 0 ||
            spriteSize.Width % MonsterDirectionCount != 0 ||
            spriteSize.Height % MonsterAnimationFrameCount != 0)
        {
            return null;
        }

        int frameWidth = spriteSize.Width / MonsterDirectionCount;
        int frameHeight = spriteSize.Height / MonsterAnimationFrameCount;
        if (frameWidth != frameHeight)
        {
            return null;
        }

        int directionColumn = DirectionColumn(directionX, directionY);
        int animationRow = isDead || moveState == EWorldMoveState.Stop
            ? 0
            : (int)(Math.Max(0.0f, animationTime) * MonsterAnimationFramesPerSecond) % MonsterAnimationFrameCount;
        return new Rectangle(
            directionColumn * frameWidth,
            animationRow * frameHeight,
            frameWidth,
            frameHeight);
    }

}
