namespace DragonPixel.PocC;

[AttributeUsage(AttributeTargets.Class, AllowMultiple = false, Inherited = false)]
internal sealed class DpeComponentAttribute(
    string typeId,
    string qualifiedName,
    string displayName,
    int schemaVersion,
    string owner) : Attribute
{
    public string StableTypeId { get; } = typeId;
    public string QualifiedName { get; } = qualifiedName;
    public string DisplayName { get; } = displayName;
    public int SchemaVersion { get; } = schemaVersion;
    public string Owner { get; } = owner;
}

internal static class PocComponentIds
{
    public const string NativeTransform = "ef688879-c3ca-4711-8ed4-2fb5e0b3dfc5";
    public const string ManagedRotator = "c3f99315-5959-479f-828c-e69ce93a39fc";
    public const string MissingSparkle = "e1a3d322-b5bc-40db-8a2a-b3041baa6402";
    public const string UnknownNewer = "4427ec10-7fb8-443b-b7ad-1b500806c2f6";
}

[AttributeUsage(AttributeTargets.Property, AllowMultiple = false, Inherited = false)]
internal sealed class DpePropertyAttribute(
    string propertyId,
    string displayName,
    string valueType) : Attribute
{
    public string PropertyId { get; } = propertyId;
    public string DisplayName { get; } = displayName;
    public string ValueType { get; } = valueType;
    public int Order { get; init; }
    public bool ReadOnly { get; init; }
}
