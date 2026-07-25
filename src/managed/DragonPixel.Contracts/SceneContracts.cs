namespace DragonPixel.Contracts;

public sealed class SceneSnapshot
{
    public int FormatVersion { get; set; } = 3;
    public string EngineVersion { get; set; } = string.Empty;
    public DpeId SceneId { get; set; }
    public string Name { get; set; } = string.Empty;
    public bool Enabled { get; set; } = true;
    public long SnapshotRevision { get; set; }
    public ScenePhysicsSettings Physics { get; set; } = new();
    public IReadOnlyList<EntitySnapshot> Entities { get; set; } = Array.Empty<EntitySnapshot>();
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
