namespace DragonPixel.Contracts;

public sealed class SceneSnapshot
{
    public int FormatVersion { get; set; } = 5;
    public string EngineVersion { get; set; } = string.Empty;
    public DpeId SceneId { get; set; }
    public string Name { get; set; } = string.Empty;
    public bool Enabled { get; set; } = true;
    public long SnapshotRevision { get; set; }
    public ScenePhysicsSettings Physics { get; set; } = new();
    public IReadOnlyList<EntitySnapshot> Entities { get; set; } = Array.Empty<EntitySnapshot>();
    public IReadOnlyList<RuntimeAssetBinding> Assets { get; set; } = Array.Empty<RuntimeAssetBinding>();
    public IReadOnlyList<TileSetSnapshot> TileSets { get; set; } = Array.Empty<TileSetSnapshot>();
    public IReadOnlyList<TilemapSnapshot> Tilemaps { get; set; } = Array.Empty<TilemapSnapshot>();
}

public sealed class EntitySnapshot
{
    public DpeId Id { get; set; }
    public string Name { get; set; } = string.Empty;
    public DpeId? ParentId { get; set; }
    public bool Enabled { get; set; } = true;
    public int SiblingOrder { get; set; }
    public IReadOnlyList<ComponentSnapshot> Components { get; set; } = Array.Empty<ComponentSnapshot>();
}

public sealed class ComponentSnapshot
{
    public string TypeId { get; set; } = string.Empty;
    public string QualifiedName { get; set; } = string.Empty;
    public int SchemaVersion { get; set; }
    public ComponentOwner Owner { get; set; }
    public bool Enabled { get; set; } = true;
    public string PropertyPayloadJson { get; set; } = "{}";
    public bool Opaque { get; set; }
}

public sealed class RuntimeAssetBinding
{
    public DpeId AssetId { get; set; }
    public string AssetType { get; set; } = string.Empty;
    public string ImmutablePath { get; set; } = string.Empty;
    public string ContentHash { get; set; } = string.Empty;
    public string MediaType { get; set; } = string.Empty;
    public string EmbeddedBytesBase64 { get; set; } = string.Empty;
}

public sealed class TileSetSnapshot
{
    public DpeId AssetId { get; set; }
    public DpeId TextureAssetId { get; set; }
    public IReadOnlyList<DpeId> TextureAssetIds { get; set; } = Array.Empty<DpeId>();
    public int CellWidth { get; set; }
    public int CellHeight { get; set; }
    public float PixelsPerUnit { get; set; } = 32;
    public string TexturePngBase64 { get; set; } = string.Empty;
    public IReadOnlyList<TileDefinitionSnapshot> Tiles { get; set; } = Array.Empty<TileDefinitionSnapshot>();
}

public sealed class TileDefinitionSnapshot
{
    public DpeId TileId { get; set; }
    public string Kind { get; set; } = "basic";
    public DpeId TextureAssetId { get; set; }
    public int SourceX { get; set; }
    public int SourceY { get; set; }
    public int SourceWidth { get; set; }
    public int SourceHeight { get; set; }
    public float[]? CollisionRectangle { get; set; }
    public IReadOnlyList<float[]> CollisionOutline { get; set; } = Array.Empty<float[]>();
    public IReadOnlyList<TileAnimationFrameSnapshot> AnimationFrames { get; set; } = Array.Empty<TileAnimationFrameSnapshot>();
    public string OpaqueTypedDataJson { get; set; } = "{}";
}

public sealed class TileAnimationFrameSnapshot
{
    public DpeId TextureAssetId { get; set; }
    public int SourceX { get; set; }
    public int SourceY { get; set; }
    public int SourceWidth { get; set; }
    public int SourceHeight { get; set; }
    public float DurationSeconds { get; set; } = 1.0f / 12.0f;
}

public sealed class TilemapSnapshot
{
    public DpeId AssetId { get; set; }
    public string GridLayout { get; set; } = "rectangular";
    public float CellWidth { get; set; } = 1;
    public float CellHeight { get; set; } = 1;
    public float CellGapX { get; set; }
    public float CellGapY { get; set; }
    public IReadOnlyList<DpeId> TileSetDependencies { get; set; } = Array.Empty<DpeId>();
    public IReadOnlyList<TilemapLayerSnapshot> Layers { get; set; } = Array.Empty<TilemapLayerSnapshot>();
}

public sealed class TilemapLayerSnapshot
{
    public DpeId LayerId { get; set; }
    public string Name { get; set; } = string.Empty;
    public bool Visible { get; set; } = true;
    public int Order { get; set; }
    public int SortOrder { get; set; }
    public string RendererMode { get; set; } = "chunk";
    public float AnimationRate { get; set; } = 1;
    public IReadOnlyList<TilemapCellSnapshot> Cells { get; set; } = Array.Empty<TilemapCellSnapshot>();
}

public sealed class TilemapCellSnapshot
{
    public int X { get; set; }
    public int Y { get; set; }
    public DpeId TileId { get; set; }
    public DpeId TileSetId { get; set; }
    public bool FlipX { get; set; }
    public bool FlipY { get; set; }
    public int RotationQuarterTurns { get; set; }
    public float RotationDegrees { get; set; }
    public float OffsetX { get; set; }
    public float OffsetY { get; set; }
    public float ScaleX { get; set; } = 1;
    public float ScaleY { get; set; } = 1;
    public int Elevation { get; set; }
    public bool ColorLocked { get; set; }
    public bool TransformLocked { get; set; }
}
