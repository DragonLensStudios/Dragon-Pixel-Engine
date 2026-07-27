using System.Security.Cryptography;
using System.Text.Json;
using DragonPixel.Contracts;

namespace DragonPixel.Runtime;

internal sealed record ParsedSceneSnapshot(
    RenderScene Scene,
    string Sha256,
    ParsedPhysicsSnapshot Physics);

internal static class SceneSnapshotParser
{
    private const string TransformType = "52e52fbd-ea15-40c5-bd9a-7dd320f7cd1e";
    private const string RotatorType = "ee40709b-2bfc-4d50-b729-8612fb60d478";
    private const string CameraType = "4d054cdc-20e0-4f4f-80d9-a38923c36d46";
    private const string SpriteType = "b527395a-93a5-44f3-8d6c-7ea83a8568d1";
    private const string MeshType = "9be44558-78e9-4912-bee5-046b5ad0a410";
    private const string MaterialType = "90d93631-746a-4f52-9f95-4895e27edf51";
    private const string LightType = "1859c426-7cfe-42fc-a815-f5fc1bf1dad6";
    private const string TilemapType = "eea820b4-79e4-4dd6-86f8-04c93c3486fb";
    private const string InputMotion2DType = "64348aba-c5a4-42fc-86e6-e99f9640e36d";

    public static ParsedSceneSnapshot Parse(string path, long requestedRevision)
    {
        var bytes = File.ReadAllBytes(path);
        using var document = JsonDocument.Parse(bytes);
        var root = document.RootElement;
        var formatVersion = RequiredInt(root, "formatVersion");
        if (RequiredString(root, "format") != "dpe.scene" || formatVersion is < 1 or > 3)
        {
            throw new InvalidDataException("Snapshot format was unsupported.");
        }

        var expectedSchema = formatVersion switch
        {
            2 => "https://dragonpixel.dev/schemas/v2/scene.schema.json",
            3 => "https://dragonpixel.dev/schemas/v3/scene.schema.json",
            _ => null,
        };
        if (formatVersion >= 2
            && (!root.TryGetProperty("$schema", out var schema)
                || schema.GetString() != expectedSchema
                || !root.TryGetProperty("engineVersion", out var engineVersion)
                || string.IsNullOrWhiteSpace(engineVersion.GetString())))
        {
            throw new InvalidDataException(
                "Version 2 and 3 snapshots require a schema URI and engineVersion.");
        }

        var revision = requestedRevision > 0
            ? requestedRevision
            : OptionalLong(root, "snapshotRevision", 1);
        if (revision <= 0)
        {
            throw new InvalidDataException("snapshotRevision must be positive.");
        }

        if (!root.TryGetProperty("entities", out var entityArray)
            || entityArray.ValueKind != JsonValueKind.Array)
        {
            throw new InvalidDataException("Snapshot entities must be an array.");
        }

        var diagnostics = new List<string>();
        var runtimeAssets = ReadRuntimeAssets(root, diagnostics);
        var runtimeTilemaps = ReadRuntimeTilemaps(root, diagnostics);
        var entities = new List<RenderEntity>();
        var entityIds = new HashSet<string>(StringComparer.Ordinal);
        foreach (var entityElement in entityArray.EnumerateArray())
        {
            var id = CanonicalId(RequiredString(entityElement, "id"), "entity id");
            if (!entityIds.Add(id))
            {
                throw new InvalidDataException($"Duplicate entity id {id}.");
            }

            var parentId = OptionalString(entityElement, "parentId");
            if (parentId is not null)
            {
                parentId = CanonicalId(parentId, "parent id");
            }

            var transform = new RenderTransform(
                RenderVector3.Zero,
                RenderQuaternion.Identity,
                RenderVector3.One);
            RenderSprite? sprite = null;
            RenderMesh? mesh = null;
            RenderTilemap? tilemap = null;
            RenderCamera? camera = null;
            RenderLight? light = null;
            RenderInputMotion2D? inputMotion = null;
            var colliders = new List<RenderCollider>();
            var rotatorDegreesPerSecond = 0.0f;
            var materialColor = new RenderColor(0.95f, 0.35f, 0.15f, 1.0f);
            JsonElement? meshProperties = null;

            if (entityElement.TryGetProperty("components", out var components)
                && components.ValueKind == JsonValueKind.Array)
            {
                foreach (var component in components.EnumerateArray())
                {
                    if (!OptionalBool(component, "enabled", true))
                    {
                        continue;
                    }

                    var typeId = OptionalString(component, "typeId") ?? string.Empty;
                    var properties = ReadProperties(component);
                    switch (typeId)
                    {
                        case TransformType:
                            transform = new RenderTransform(
                                ReadVector3(properties, "dpe.transform.position", RenderVector3.Zero),
                                ReadQuaternion(properties, "dpe.transform.rotation", RenderQuaternion.Identity),
                                ReadVector3(properties, "dpe.transform.scale", RenderVector3.One));
                            break;
                        case SpriteType:
                            sprite = new RenderSprite(
                                ReadString(properties, "dpe.sprite.asset", "builtin://checker"),
                                ReadColor(properties, "dpe.sprite.color", RenderColor.White),
                                ReadInt(properties, "dpe.sprite.layer", 0));
                            break;
                        case MeshType:
                            meshProperties = properties;
                            break;
                        case MaterialType:
                            materialColor = ReadColor(
                                properties,
                                "dpe.material.base_color",
                                materialColor);
                            break;
                        case CameraType:
                            var projection = ReadString(properties, "dpe.camera.projection", "perspective");
                            camera = new RenderCamera(
                                ReadBool(properties, "dpe.camera.primary", true),
                                string.Equals(projection, "orthographic", StringComparison.OrdinalIgnoreCase),
                                ReadFloat(properties, "dpe.camera.field_of_view", 60),
                                ReadFloat(properties, "dpe.camera.orthographic_size", 10),
                                ReadFloat(properties, "dpe.camera.near", 0.1f),
                                ReadFloat(properties, "dpe.camera.far", 1000));
                            break;
                        case LightType:
                            var kind = ReadString(properties, "dpe.light.kind", "directional") switch
                            {
                                "ambient" => RenderLightKind.Ambient,
                                "point" => RenderLightKind.Point,
                                _ => RenderLightKind.Directional,
                            };
                            light = new RenderLight(
                                kind,
                                ReadColor(properties, "dpe.light.color", RenderColor.White),
                                Math.Max(0, ReadFloat(properties, "dpe.light.intensity", 1)),
                                Math.Max(0.01f, ReadFloat(properties, "dpe.light.range", 10)));
                            break;
                        case TilemapType:
                            var tilemapAssetId = ReadString(properties, "dpe.tilemap.asset", string.Empty);
                            if (!string.IsNullOrWhiteSpace(tilemapAssetId)
                                && runtimeTilemaps.TryGetValue(tilemapAssetId, out var resolvedTilemap))
                            {
                                tilemap = resolvedTilemap with
                                {
                                    Tint = ReadColor(properties, "dpe.tilemap.tint", RenderColor.White),
                                    BaseLayer = ReadInt(properties, "dpe.tilemap.layer", 0),
                                };
                            }
                            else if (!string.IsNullOrWhiteSpace(tilemapAssetId))
                            {
                                diagnostics.Add($"Tilemap asset {tilemapAssetId} was not present in flattened snapshot v4 data.");
                            }
                            break;
                        case RotatorType:
                            rotatorDegreesPerSecond = ReadFloat(
                                properties,
                                "dpe.rotator.degrees_per_second",
                                0);
                            break;
                        case InputMotion2DType:
                            inputMotion = new RenderInputMotion2D(
                                ReadString(properties, "dpe.input.horizontal_action", "move.x"),
                                ReadString(properties, "dpe.input.vertical_action", "move.y"),
                                Math.Max(0, ReadFloat(properties, "dpe.input.speed", 5)));
                            break;
                        case BuiltinComponentIds.BoxCollider2D:
                            var box2DSize = ReadVector3(
                                properties,
                                "dpe.physics2d.size",
                                new RenderVector3(1, 1, 0));
                            colliders.Add(new RenderCollider(
                                RenderColliderKind.Box2D,
                                new RenderVector3(box2DSize.X, box2DSize.Y, 0),
                                ReadVector3(
                                    properties,
                                    "dpe.physics2d.offset",
                                    RenderVector3.Zero),
                                ReadBool(properties, "dpe.physics.sensor", false)));
                            break;
                        case BuiltinComponentIds.CircleCollider2D:
                            var circleDiameter = 2 * ReadFloat(
                                properties,
                                "dpe.physics2d.radius",
                                0.5f);
                            colliders.Add(new RenderCollider(
                                RenderColliderKind.Circle2D,
                                new RenderVector3(circleDiameter, circleDiameter, 0),
                                ReadVector3(
                                    properties,
                                    "dpe.physics2d.offset",
                                    RenderVector3.Zero),
                                ReadBool(properties, "dpe.physics.sensor", false)));
                            break;
                        case BuiltinComponentIds.BoxCollider3D:
                            colliders.Add(new RenderCollider(
                                RenderColliderKind.Box3D,
                                ReadVector3(
                                    properties,
                                    "dpe.physics3d.size",
                                    RenderVector3.One),
                                ReadVector3(
                                    properties,
                                    "dpe.physics3d.offset",
                                    RenderVector3.Zero),
                                ReadBool(properties, "dpe.physics.sensor", false)));
                            break;
                        case BuiltinComponentIds.SphereCollider3D:
                            var sphereDiameter = 2 * ReadFloat(
                                properties,
                                "dpe.physics3d.radius",
                                0.5f);
                            colliders.Add(new RenderCollider(
                                RenderColliderKind.Sphere3D,
                                new RenderVector3(sphereDiameter, sphereDiameter, sphereDiameter),
                                ReadVector3(
                                    properties,
                                    "dpe.physics3d.offset",
                                    RenderVector3.Zero),
                                ReadBool(properties, "dpe.physics.sensor", false)));
                            break;
                    }
                }
            }

            if (meshProperties is JsonElement meshPropertyElement)
            {
                mesh = new RenderMesh(
                    ReadString(meshPropertyElement, "dpe.mesh.asset", "builtin://unit-cube"),
                    materialColor);
            }

            entities.Add(new RenderEntity(
                id,
                OptionalString(entityElement, "name") ?? "GameObject",
                parentId,
                OptionalBool(entityElement, "enabled", true),
                transform,
                sprite,
                mesh,
                tilemap,
                camera,
                light,
                colliders,
                inputMotion,
                rotatorDegreesPerSecond));
        }

        foreach (var entity in entities)
        {
            if (entity.ParentId is not null && !entityIds.Contains(entity.ParentId))
            {
                diagnostics.Add($"Entity {entity.Id} has missing parent {entity.ParentId}; rendered at scene root.");
            }
        }

        var enabledPrimaryCameras = entities.Count(
            static entity => entity.Enabled && entity.Camera is { Primary: true });
        if (enabledPrimaryCameras == 0)
        {
            diagnostics.Add("No enabled primary camera; the editor camera is used.");
        }
        else if (enabledPrimaryCameras > 1)
        {
            diagnostics.Add("Multiple enabled primary cameras; the first stable entity order is used.");
        }

        var scene = new RenderScene(
            OptionalString(root, "sceneId") ?? string.Empty,
            OptionalString(root, "name") ?? "Scene",
            revision,
            entities,
            runtimeAssets,
            diagnostics);
        var physics = PhysicsSnapshotParser.Parse(root, scene, formatVersion);
        if (physics.Diagnostics.Count != 0)
        {
            scene = scene with
            {
                Diagnostics = diagnostics.Concat(physics.Diagnostics).ToArray(),
            };
        }
        return new ParsedSceneSnapshot(
            scene,
            Convert.ToHexString(SHA256.HashData(bytes)).ToLowerInvariant(),
            physics);
    }

    private sealed record RuntimeTileDefinition(
        RenderColor Color,
        int SourceX,
        int SourceY,
        int SourceWidth,
        int SourceHeight);

    private static IReadOnlyDictionary<string, RenderAsset> ReadRuntimeAssets(
        JsonElement root,
        List<string> diagnostics)
    {
        var result = new Dictionary<string, RenderAsset>(StringComparer.Ordinal);
        if (!root.TryGetProperty("assets", out var assets))
        {
            return result;
        }
        if (assets.ValueKind != JsonValueKind.Array)
        {
            diagnostics.Add("Runtime assets must be an array.");
            return result;
        }
        foreach (var value in assets.EnumerateArray())
        {
            try
            {
                var id = CanonicalId(RequiredString(value, "assetId"), "Runtime asset id");
                var assetType = RequiredString(value, "assetType");
                var mediaType = RequiredString(value, "mediaType");
                var contentHash = RequiredString(value, "contentHash").ToLowerInvariant();
                var encoded = RequiredString(value, "embeddedBytesBase64");
                var bytes = Convert.FromBase64String(encoded);
                if (bytes.Length == 0 || bytes.Length > 64 * 1024 * 1024)
                {
                    diagnostics.Add($"Runtime asset {id} has an invalid immutable byte length.");
                    continue;
                }
                var actualHash = Convert.ToHexString(SHA256.HashData(bytes)).ToLowerInvariant();
                if (!actualHash.Equals(contentHash, StringComparison.Ordinal))
                {
                    diagnostics.Add($"Runtime asset {id} failed content-hash validation.");
                    continue;
                }
                if (assetType != "sprite" || mediaType is not ("image/png" or "image/jpeg"))
                {
                    diagnostics.Add($"Runtime asset {id} uses unsupported type {assetType}/{mediaType}.");
                    continue;
                }
                if (!result.TryAdd(id, new RenderAsset(
                        id, assetType, mediaType, contentHash, bytes)))
                {
                    diagnostics.Add($"Runtime asset {id} was duplicated.");
                }
            }
            catch (Exception exception) when (
                exception is InvalidDataException or FormatException)
            {
                diagnostics.Add($"Runtime asset binding was rejected: {exception.Message}");
            }
        }
        return result;
    }

    private sealed record RuntimeTileSet(
        string TextureAssetId,
        byte[] TexturePng,
        float CellWidth,
        float CellHeight,
        IReadOnlyDictionary<string, RuntimeTileDefinition> Tiles);

    private static IReadOnlyDictionary<string, RenderTilemap> ReadRuntimeTilemaps(
        JsonElement root,
        List<string> diagnostics)
    {
        var sets = new Dictionary<string, RuntimeTileSet>(StringComparer.Ordinal);
        if (root.TryGetProperty("tileSets", out var tileSets) && tileSets.ValueKind == JsonValueKind.Array)
        {
            foreach (var value in tileSets.EnumerateArray())
            {
                var id = CanonicalId(RequiredString(value, "assetId"), "TileSet asset id");
                var pixelsPerUnit = ReadFloat(value, "pixelsPerUnit", 32);
                var cellWidth = ReadFloat(value, "cellWidth", 32) / Math.Max(0.001f, pixelsPerUnit);
                var cellHeight = ReadFloat(value, "cellHeight", 32) / Math.Max(0.001f, pixelsPerUnit);
                var textureAssetId = CanonicalId(RequiredString(value, "textureAssetId"), "TileSet texture asset id");
                var texturePng = Array.Empty<byte>();
                if (value.TryGetProperty("texturePngBase64", out var encodedTexture)
                    && encodedTexture.ValueKind == JsonValueKind.String
                    && !string.IsNullOrWhiteSpace(encodedTexture.GetString()))
                {
                    try { texturePng = Convert.FromBase64String(encodedTexture.GetString()!); }
                    catch (FormatException) { diagnostics.Add($"TileSet {id} contains invalid immutable PNG data."); }
                }
                var definitions = new Dictionary<string, RuntimeTileDefinition>(StringComparer.Ordinal);
                if (value.TryGetProperty("tiles", out var tiles) && tiles.ValueKind == JsonValueKind.Array)
                {
                    foreach (var tile in tiles.EnumerateArray())
                    {
                        var tileId = CanonicalId(RequiredString(tile, "tileId"), "tile id");
                        var hash = SHA256.HashData(System.Text.Encoding.UTF8.GetBytes(tileId));
                        definitions[tileId] = new RuntimeTileDefinition(
                            new RenderColor(
                                0.25f + (hash[0] / 255.0f * 0.7f),
                                0.25f + (hash[1] / 255.0f * 0.7f),
                                0.25f + (hash[2] / 255.0f * 0.7f),
                                1),
                            ReadInt(tile, "sourceX", 0),
                            ReadInt(tile, "sourceY", 0),
                            Math.Max(1, ReadInt(tile, "sourceWidth", 1)),
                            Math.Max(1, ReadInt(tile, "sourceHeight", 1)));
                    }
                }
                sets.Add(id, new RuntimeTileSet(textureAssetId, texturePng, cellWidth, cellHeight, definitions));
            }
        }

        var result = new Dictionary<string, RenderTilemap>(StringComparer.Ordinal);
        if (!root.TryGetProperty("tilemaps", out var tilemaps) || tilemaps.ValueKind != JsonValueKind.Array)
        {
            return result;
        }
        foreach (var value in tilemaps.EnumerateArray())
        {
            var id = CanonicalId(RequiredString(value, "assetId"), "tilemap asset id");
            RuntimeTileSet? set = null;
            if (value.TryGetProperty("tileSetDependencies", out var dependencies)
                && dependencies.ValueKind == JsonValueKind.Array)
            {
                foreach (var dependency in dependencies.EnumerateArray())
                {
                    if (dependency.ValueKind == JsonValueKind.String
                        && sets.TryGetValue(dependency.GetString()!, out set))
                    {
                        break;
                    }
                }
            }
            if (set is null)
            {
                diagnostics.Add($"Tilemap {id} has no resolved flattened TileSet dependency.");
                continue;
            }
            var layers = new List<RenderTileLayer>();
            if (value.TryGetProperty("layers", out var layerValues) && layerValues.ValueKind == JsonValueKind.Array)
            {
                foreach (var layer in layerValues.EnumerateArray())
                {
                    var cells = new List<RenderTileCell>();
                    if (layer.TryGetProperty("cells", out var cellValues) && cellValues.ValueKind == JsonValueKind.Array)
                    {
                        foreach (var cell in cellValues.EnumerateArray())
                        {
                            var tileId = CanonicalId(RequiredString(cell, "tileId"), "tile cell id");
                            if (!set.Tiles.TryGetValue(tileId, out var tile))
                            {
                                diagnostics.Add($"Tilemap {id} retained missing tile id {tileId}.");
                                tile = new RuntimeTileDefinition(new RenderColor(1, 0, 1, 1), 0, 0, 1, 1);
                            }
                            cells.Add(new RenderTileCell(
                                ReadInt(cell, "x", 0),
                                ReadInt(cell, "y", 0),
                                tileId,
                                tile.Color,
                                tile.SourceX,
                                tile.SourceY,
                                tile.SourceWidth,
                                tile.SourceHeight,
                                OptionalBool(cell, "flipX", false),
                                OptionalBool(cell, "flipY", false),
                                Math.Clamp(ReadInt(cell, "rotationQuarterTurns", 0), 0, 3)));
                        }
                    }
                    layers.Add(new RenderTileLayer(
                        OptionalString(layer, "name") ?? "Layer",
                        OptionalBool(layer, "visible", true),
                        ReadInt(layer, "order", 0),
                        cells));
                }
            }
            result.Add(id, new RenderTilemap(id, set.TextureAssetId, set.TexturePng, set.CellWidth, set.CellHeight,
                RenderColor.White, 0, layers));
        }
        return result;
    }

    private static JsonElement ReadProperties(JsonElement component)
    {
        if (component.TryGetProperty("properties", out var properties)
            && properties.ValueKind == JsonValueKind.Object)
        {
            return properties;
        }

        if (component.TryGetProperty("propertyPayloadJson", out var payload)
            && payload.ValueKind == JsonValueKind.String)
        {
            using var document = JsonDocument.Parse(payload.GetString() ?? "{}");
            return document.RootElement.Clone();
        }

        using var empty = JsonDocument.Parse("{}");
        return empty.RootElement.Clone();
    }

    private static string CanonicalId(string value, string field)
    {
        if (!Guid.TryParseExact(value, "D", out var id))
        {
            throw new InvalidDataException($"Invalid {field}: {value}.");
        }
        return id.ToString("D");
    }

    private static string RequiredString(JsonElement element, string name) =>
        element.TryGetProperty(name, out var value) && value.ValueKind == JsonValueKind.String
            ? value.GetString()!
            : throw new InvalidDataException($"Snapshot requires string property {name}.");

    private static int RequiredInt(JsonElement element, string name) =>
        element.TryGetProperty(name, out var value) && value.TryGetInt32(out var result)
            ? result
            : throw new InvalidDataException($"Snapshot requires integer property {name}.");

    private static string? OptionalString(JsonElement element, string name) =>
        element.TryGetProperty(name, out var value) && value.ValueKind == JsonValueKind.String
            ? value.GetString()
            : null;

    private static bool OptionalBool(JsonElement element, string name, bool fallback) =>
        element.TryGetProperty(name, out var value)
            && value.ValueKind is JsonValueKind.True or JsonValueKind.False
                ? value.GetBoolean()
                : fallback;

    private static long OptionalLong(JsonElement element, string name, long fallback) =>
        element.TryGetProperty(name, out var value) && value.TryGetInt64(out var result)
            ? result
            : fallback;

    private static string ReadString(JsonElement properties, string name, string fallback) =>
        properties.TryGetProperty(name, out var value) && value.ValueKind == JsonValueKind.String
            ? value.GetString() ?? fallback
            : fallback;

    private static bool ReadBool(JsonElement properties, string name, bool fallback) =>
        properties.TryGetProperty(name, out var value)
            && value.ValueKind is JsonValueKind.True or JsonValueKind.False
                ? value.GetBoolean()
                : fallback;

    private static int ReadInt(JsonElement properties, string name, int fallback) =>
        properties.TryGetProperty(name, out var value) && value.TryGetInt32(out var result)
            ? result
            : fallback;

    private static float ReadFloat(JsonElement properties, string name, float fallback) =>
        properties.TryGetProperty(name, out var value) && value.TryGetSingle(out var result)
            && float.IsFinite(result)
                ? result
                : fallback;

    private static RenderVector3 ReadVector3(
        JsonElement properties,
        string name,
        RenderVector3 fallback)
    {
        if (!properties.TryGetProperty(name, out var value) || value.ValueKind != JsonValueKind.Object)
        {
            return fallback;
        }
        return new RenderVector3(
            ReadFloat(value, "x", fallback.X),
            ReadFloat(value, "y", fallback.Y),
            ReadFloat(value, "z", fallback.Z));
    }

    private static RenderQuaternion ReadQuaternion(
        JsonElement properties,
        string name,
        RenderQuaternion fallback)
    {
        if (!properties.TryGetProperty(name, out var value) || value.ValueKind != JsonValueKind.Object)
        {
            return fallback;
        }
        var quaternion = new RenderQuaternion(
            ReadFloat(value, "x", fallback.X),
            ReadFloat(value, "y", fallback.Y),
            ReadFloat(value, "z", fallback.Z),
            ReadFloat(value, "w", fallback.W));
        var lengthSquared = (quaternion.X * quaternion.X)
            + (quaternion.Y * quaternion.Y)
            + (quaternion.Z * quaternion.Z)
            + (quaternion.W * quaternion.W);
        if (lengthSquared < 0.000001f)
        {
            return fallback;
        }
        var inverseLength = 1.0f / MathF.Sqrt(lengthSquared);
        return new RenderQuaternion(
            quaternion.X * inverseLength,
            quaternion.Y * inverseLength,
            quaternion.Z * inverseLength,
            quaternion.W * inverseLength);
    }

    private static RenderColor ReadColor(JsonElement properties, string name, RenderColor fallback)
    {
        if (!properties.TryGetProperty(name, out var value) || value.ValueKind != JsonValueKind.Object)
        {
            return fallback;
        }
        return new RenderColor(
            Math.Clamp(ReadFloat(value, "r", fallback.R), 0, 1),
            Math.Clamp(ReadFloat(value, "g", fallback.G), 0, 1),
            Math.Clamp(ReadFloat(value, "b", fallback.B), 0, 1),
            Math.Clamp(ReadFloat(value, "a", fallback.A), 0, 1));
    }
}
