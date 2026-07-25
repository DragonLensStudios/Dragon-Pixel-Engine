using System.Text.Json;
using System.Text.Json.Nodes;

namespace DragonPixel.PocC;

internal sealed class SceneDocument
{
    private static readonly JsonSerializerOptions SerializerOptions = new()
    {
        WriteIndented = true,
    };

    private SceneDocument(JsonObject root)
    {
        Root = root;
    }

    public JsonObject Root { get; }

    public static SceneDocument Parse(string json)
    {
        var root = JsonNode.Parse(json)?.AsObject()
            ?? throw new InvalidDataException("Scene root must be a JSON object.");
        return new SceneDocument(root);
    }

    public string Save()
    {
        var canonical = Canonicalize(Root);
        return canonical.ToJsonString(SerializerOptions).Replace("\r\n", "\n", StringComparison.Ordinal) + "\n";
    }

    public IEnumerable<JsonObject> Components()
    {
        foreach (var entityNode in Root["entities"]?.AsArray() ?? [])
        {
            foreach (var componentNode in entityNode?["components"]?.AsArray() ?? [])
            {
                yield return componentNode!.AsObject();
            }
        }
    }

    public JsonObject Component(string typeId) => Components().Single(component =>
        string.Equals(component["typeId"]!.GetValue<string>(), typeId, StringComparison.Ordinal));

    public MigrationReport MigrateTransformV1ToV2()
    {
        var migrated = 0;
        foreach (var component in Components())
        {
            if (component["typeId"]?.GetValue<string>() != PocComponentIds.NativeTransform
                || component["schemaVersion"]?.GetValue<int>() != 1)
            {
                continue;
            }

            var properties = component["properties"]!.AsObject();
            var oldPropertyId = "dpe.poc.transform.translation";
            var newPropertyId = "dpe.poc.transform.position";
            if (properties.Remove(oldPropertyId, out var value))
            {
                properties[newPropertyId] = value;
            }

            component["schemaVersion"] = 2;
            migrated++;
        }

        return new MigrationReport(
            PocComponentIds.NativeTransform,
            1,
            2,
            migrated,
            "dpe.poc.transform.translation",
            "dpe.poc.transform.position");
    }

    private static JsonNode Canonicalize(JsonNode node)
    {
        if (node is JsonObject jsonObject)
        {
            var result = new JsonObject();
            foreach (var property in jsonObject.OrderBy(static pair => pair.Key, StringComparer.Ordinal))
            {
                result[property.Key] = property.Value is null ? null : Canonicalize(property.Value);
            }

            return result;
        }

        if (node is JsonArray jsonArray)
        {
            var result = new JsonArray();
            foreach (var item in jsonArray)
            {
                result.Add(item is null ? null : Canonicalize(item));
            }

            return result;
        }

        return node.DeepClone();
    }
}

internal sealed record MigrationReport(
    string TypeId,
    int FromVersion,
    int ToVersion,
    int MigratedRecords,
    string OldPropertyId,
    string NewPropertyId);
