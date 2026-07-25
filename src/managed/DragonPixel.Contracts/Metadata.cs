namespace DragonPixel.Contracts;

public enum ComponentOwner
{
    Native,
    Managed,
}

public enum PropertyValueKind
{
    Boolean,
    Integer,
    Number,
    String,
    Vector2,
    Vector3,
    Quaternion,
    Color,
    EntityReference,
    AssetReference,
}

[AttributeUsage(AttributeTargets.Class, AllowMultiple = false, Inherited = false)]
public sealed class DpeComponentAttribute : Attribute
{
    public DpeComponentAttribute(
        string typeId,
        string qualifiedName,
        string displayName,
        int schemaVersion,
        ComponentOwner owner)
    {
        TypeId = typeId;
        QualifiedName = qualifiedName;
        DisplayName = displayName;
        SchemaVersion = schemaVersion;
        Owner = owner;
    }

    public new string TypeId { get; }
    public string QualifiedName { get; }
    public string DisplayName { get; }
    public int SchemaVersion { get; }
    public ComponentOwner Owner { get; }
}

[AttributeUsage(AttributeTargets.Property, AllowMultiple = false, Inherited = false)]
public sealed class DpePropertyAttribute : Attribute
{
    public DpePropertyAttribute(string propertyId, string displayName, PropertyValueKind valueKind)
    {
        PropertyId = propertyId;
        DisplayName = displayName;
        ValueKind = valueKind;
    }

    public string PropertyId { get; }
    public string DisplayName { get; }
    public PropertyValueKind ValueKind { get; }
    public int Order { get; set; }
    public bool ReadOnly { get; set; }
}

public sealed class ComponentMetadata
{
    public string TypeId { get; set; } = string.Empty;
    public string QualifiedName { get; set; } = string.Empty;
    public string DisplayName { get; set; } = string.Empty;
    public int SchemaVersion { get; set; }
    public ComponentOwner Owner { get; set; }
    public IReadOnlyList<PropertyMetadata> Properties { get; set; } = Array.Empty<PropertyMetadata>();
}

public sealed class PropertyMetadata
{
    public string PropertyId { get; set; } = string.Empty;
    public string DisplayName { get; set; } = string.Empty;
    public PropertyValueKind ValueKind { get; set; }
    public int Order { get; set; }
    public bool ReadOnly { get; set; }
}
