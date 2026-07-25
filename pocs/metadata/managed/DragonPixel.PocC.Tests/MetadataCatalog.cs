using System.Text.Json.Nodes;

namespace DragonPixel.PocC;

internal enum InspectorComponentState
{
    Editable,
    OpaqueMissingType,
    OpaqueNewerSchema,
}

internal sealed record InspectorProperty(
    string PropertyId,
    string DisplayName,
    string ValueType,
    JsonNode? Value);

internal sealed record InspectorComponent(
    string TypeId,
    string DisplayName,
    InspectorComponentState State,
    IReadOnlyList<InspectorProperty> Properties);

internal sealed class MetadataCatalog
{
    private readonly Dictionary<string, JsonObject> _components = new(StringComparer.Ordinal);

    public MetadataCatalog(params JsonObject[] manifests)
    {
        foreach (var manifest in manifests)
        {
            foreach (var componentNode in manifest["components"]?.AsArray() ?? [])
            {
                var component = componentNode?.AsObject()
                    ?? throw new InvalidDataException("A component metadata entry was null.");
                var typeId = component["typeId"]?.GetValue<string>()
                    ?? throw new InvalidDataException("A component metadata entry had no typeId.");
                if (!_components.TryAdd(typeId, component))
                {
                    throw new InvalidDataException($"Duplicate component typeId: {typeId}");
                }
            }
        }
    }

    public InspectorComponent Inspect(JsonObject record)
    {
        var typeId = record["typeId"]?.GetValue<string>()
            ?? throw new InvalidDataException("Component record had no typeId.");
        var recordVersion = record["schemaVersion"]?.GetValue<int>()
            ?? throw new InvalidDataException("Component record had no schemaVersion.");
        if (!_components.TryGetValue(typeId, out var metadata))
        {
            return new InspectorComponent(typeId, $"Missing: {typeId}", InspectorComponentState.OpaqueMissingType, []);
        }

        var supportedVersion = metadata["schemaVersion"]!.GetValue<int>();
        var displayName = metadata["displayName"]!.GetValue<string>();
        if (recordVersion > supportedVersion)
        {
            return new InspectorComponent(typeId, displayName, InspectorComponentState.OpaqueNewerSchema, []);
        }

        var payload = record["properties"]?.AsObject()
            ?? throw new InvalidDataException("Component record had no property payload.");
        var properties = metadata["properties"]!.AsArray()
            .Select(propertyNode => propertyNode!.AsObject())
            .OrderBy(property => property["order"]!.GetValue<int>())
            .ThenBy(property => property["propertyId"]!.GetValue<string>(), StringComparer.Ordinal)
            .Select(property =>
            {
                var propertyId = property["propertyId"]!.GetValue<string>();
                return new InspectorProperty(
                    propertyId,
                    property["displayName"]!.GetValue<string>(),
                    property["valueType"]!.GetValue<string>(),
                    payload[propertyId]?.DeepClone());
            })
            .ToArray();

        return new InspectorComponent(typeId, displayName, InspectorComponentState.Editable, properties);
    }
}
