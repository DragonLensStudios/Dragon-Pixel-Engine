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
    public string DefaultJson { get; set; } = string.Empty;
    public double Minimum { get; set; } = double.NaN;
    public double Maximum { get; set; } = double.NaN;
    public double Step { get; set; } = double.NaN;
    public string Units { get; set; } = string.Empty;
    public string[] EnumChoices { get; set; } = Array.Empty<string>();
    public bool Nullable { get; set; }
    public string ReferenceFilter { get; set; } = string.Empty;
    public string Category { get; set; } = string.Empty;
    public string Tooltip { get; set; } = string.Empty;
    public string DrawerKey { get; set; } = string.Empty;
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
    public string DefaultJson { get; set; } = string.Empty;
    public double? Minimum { get; set; }
    public double? Maximum { get; set; }
    public double? Step { get; set; }
    public string Units { get; set; } = string.Empty;
    public IReadOnlyList<string> EnumChoices { get; set; } = Array.Empty<string>();
    public bool Nullable { get; set; }
    public string ReferenceFilter { get; set; } = string.Empty;
    public string Category { get; set; } = string.Empty;
    public string Tooltip { get; set; } = string.Empty;
    public string DrawerKey { get; set; } = string.Empty;
}
