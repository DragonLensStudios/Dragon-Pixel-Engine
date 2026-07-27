namespace DragonPixel.Runtime;

public readonly record struct RenderVector3(float X, float Y, float Z)
{
    public static RenderVector3 Zero => new(0, 0, 0);
    public static RenderVector3 One => new(1, 1, 1);
}

public readonly record struct RenderQuaternion(float X, float Y, float Z, float W)
{
    public static RenderQuaternion Identity => new(0, 0, 0, 1);
}

public readonly record struct RenderColor(float R, float G, float B, float A)
{
    public static RenderColor White => new(1, 1, 1, 1);
}

public sealed record RenderTransform(
    RenderVector3 Position,
    RenderQuaternion Rotation,
    RenderVector3 Scale);

public sealed record RenderSprite(string AssetId, RenderColor Color, int Layer);

public sealed record RenderAsset(
    string AssetId,
    string AssetType,
    string MediaType,
    string ContentHash,
    byte[] ImmutableBytes);

public sealed record RenderMesh(string AssetId, RenderColor BaseColor);

public sealed record RenderTileCell(
    int X,
    int Y,
    string TileId,
    RenderColor Color,
    int SourceX,
    int SourceY,
    int SourceWidth,
    int SourceHeight,
    bool FlipX,
    bool FlipY,
    int RotationQuarterTurns);

public sealed record RenderTileLayer(string Name, bool Visible, int Order, IReadOnlyList<RenderTileCell> Cells);

public sealed record RenderTilemap(
    string AssetId,
    string TextureAssetId,
    byte[] TexturePng,
    float CellWidth,
    float CellHeight,
    RenderColor Tint,
    int BaseLayer,
    IReadOnlyList<RenderTileLayer> Layers);

public sealed record RenderCamera(
    bool Primary,
    bool Orthographic,
    float FieldOfViewDegrees,
    float OrthographicSize,
    float NearPlane,
    float FarPlane);

public enum RenderLightKind
{
    Ambient,
    Directional,
    Point,
}

public sealed record RenderLight(RenderLightKind Kind, RenderColor Color, float Intensity, float Range);

public enum RenderColliderKind
{
    Box2D,
    Circle2D,
    Box3D,
    Sphere3D,
}

public sealed record RenderCollider(
    RenderColliderKind Kind,
    RenderVector3 Size,
    RenderVector3 Offset,
    bool Sensor);

public sealed record RenderInputMotion2D(
    string HorizontalAction,
    string VerticalAction,
    float Speed);

public sealed record RenderEntity(
    string Id,
    string Name,
    string? ParentId,
    bool Enabled,
    RenderTransform Transform,
    RenderSprite? Sprite,
    RenderMesh? Mesh,
    RenderTilemap? Tilemap,
    RenderCamera? Camera,
    RenderLight? Light,
    IReadOnlyList<RenderCollider> Colliders,
    RenderInputMotion2D? InputMotion,
    float RotatorDegreesPerSecond);

public sealed record RenderScene(
    string SceneId,
    string Name,
    long SnapshotRevision,
    IReadOnlyList<RenderEntity> Entities,
    IReadOnlyDictionary<string, RenderAsset> Assets,
    IReadOnlyList<string> Diagnostics)
{
    public static RenderScene Empty { get; } = new(
        string.Empty,
        "Empty",
        0,
        Array.Empty<RenderEntity>(),
        new Dictionary<string, RenderAsset>(StringComparer.Ordinal),
        Array.Empty<string>());
}

public sealed record EditorCameraState(
    bool Orthographic,
    RenderVector3 Position,
    RenderVector3 Target,
    float FieldOfViewDegrees,
    float OrthographicSize)
{
    public static EditorCameraState Default { get; } = new(
        false,
        new RenderVector3(0, 2, 7),
        RenderVector3.Zero,
        60,
        10);
}

public sealed record FrameworkRenderRequest(
    RenderScene Scene,
    int Width,
    int Height,
    TimeSpan Elapsed,
    bool UseSceneCamera,
    EditorCameraState EditorCamera,
    IReadOnlyList<string> Selection,
    long SnapshotRevision,
    long CameraRevision,
    long CommandRevision,
    long InputRevision,
    IReadOnlyDictionary<string, float> InputActions,
    long FrameRevision);

public sealed record FrameworkFrame(
    byte[] Bgra8Pixels,
    int Width,
    int Height,
    int ContentFlags,
    string Backend,
    string Device,
    long FrameRevision,
    long SnapshotRevision,
    long CameraRevision,
    long CommandRevision,
    long InputRevision);

public sealed record FrameworkPickResult(
    string? EntityId,
    long FrameRevision,
    long SnapshotRevision,
    long CameraRevision,
    long CommandRevision);

public interface IFrameworkSceneAdapter : IDisposable
{
    string Name { get; }
    string Version { get; }
    bool Experimental { get; }
    string Backend { get; }
    string Device { get; }

    FrameworkFrame RenderFrame(FrameworkRenderRequest request);
    FrameworkPickResult Pick(int x, int y, long minimumFrameRevision);
    void ServicePendingOperations();
}
