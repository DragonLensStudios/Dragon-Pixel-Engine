using System.Security.Cryptography;
using System.Text;
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

    public static ParsedSceneSnapshot Parse(
        string path,
        long requestedRevision,
        ProjectComponentRuntime? projectRuntime = null)
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
        var snapshotFormatVersion = ReadInt(root, "snapshotFormatVersion", 4);
        if (snapshotFormatVersion is not (4 or 5))
        {
            throw new InvalidDataException($"Runtime snapshot version {snapshotFormatVersion} was unsupported.");
        }
        var runtimeTilemaps = ReadRuntimeTilemaps(
            root, snapshotFormatVersion, diagnostics, projectRuntime);
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
                                diagnostics.Add($"Tilemap asset {tilemapAssetId} was not present in flattened snapshot v{snapshotFormatVersion} data.");
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
        string TextureAssetId,
        int SourceX,
        int SourceY,
        int SourceWidth,
        int SourceHeight,
        IReadOnlyList<RenderTileAnimationFrame> AnimationFrames,
        float MinimumAnimationSpeed,
        float MaximumAnimationSpeed,
        float AnimationStartTime,
        int AnimationStartFrame,
        bool AnimationLoopOnce,
        bool AnimationPaused,
        string CustomTypeId,
        string CustomPayloadJson);

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
        string AssetId,
        IReadOnlyDictionary<string, RenderTileTexture> Textures,
        float CellWidth,
        float CellHeight,
        IReadOnlyDictionary<string, RuntimeTileDefinition> Tiles);

    private static IReadOnlyDictionary<string, RenderTilemap> ReadRuntimeTilemaps(
        JsonElement root,
        int snapshotFormatVersion,
        List<string> diagnostics,
        ProjectComponentRuntime? projectRuntime)
    {
        var sets = new Dictionary<string, RuntimeTileSet>(StringComparer.Ordinal);
        if (root.TryGetProperty("tileSets", out var tileSets) && tileSets.ValueKind == JsonValueKind.Array)
        {
            foreach (var value in tileSets.EnumerateArray())
            {
                var id = CanonicalId(RequiredString(value, "assetId"), "TileSet asset id");
                var pixelsPerUnit = ReadFloat(value, "pixelsPerUnit", 32);
                var textureAssetId = CanonicalId(RequiredString(value, "textureAssetId"), "TileSet texture asset id");
                var cellSize = value.TryGetProperty("cellSize", out var cellSizeValue)
                    && cellSizeValue.ValueKind == JsonValueKind.Object
                        ? cellSizeValue
                        : value;
                var cellWidth = ReadFloat(cellSize, snapshotFormatVersion >= 5 ? "x" : "cellWidth", 32)
                    / Math.Max(0.001f, pixelsPerUnit);
                var cellHeight = ReadFloat(cellSize, snapshotFormatVersion >= 5 ? "y" : "cellHeight", 32)
                    / Math.Max(0.001f, pixelsPerUnit);
                var textures = new Dictionary<string, RenderTileTexture>(StringComparer.Ordinal);
                if (value.TryGetProperty("texturePngBase64", out var encodedTexture)
                    && encodedTexture.ValueKind == JsonValueKind.String
                    && !string.IsNullOrWhiteSpace(encodedTexture.GetString()))
                {
                    try
                    {
                        var bytes = Convert.FromBase64String(encodedTexture.GetString()!);
                        textures[textureAssetId] = new RenderTileTexture(textureAssetId, bytes);
                    }
                    catch (FormatException) { diagnostics.Add($"TileSet {id} contains invalid immutable PNG data."); }
                }
                if (snapshotFormatVersion >= 5
                    && value.TryGetProperty("texturePngBase64ByAssetId", out var textureValues)
                    && textureValues.ValueKind == JsonValueKind.Object)
                {
                    foreach (var encoded in textureValues.EnumerateObject())
                    {
                        try
                        {
                            var textureId = CanonicalId(encoded.Name, "TileSet texture asset id");
                            if (encoded.Value.ValueKind == JsonValueKind.String
                                && !string.IsNullOrWhiteSpace(encoded.Value.GetString()))
                            {
                                textures[textureId] = new RenderTileTexture(
                                    textureId, Convert.FromBase64String(encoded.Value.GetString()!));
                            }
                        }
                        catch (Exception exception) when (exception is InvalidDataException or FormatException)
                        {
                            diagnostics.Add($"TileSet {id} rejected a texture binding: {exception.Message}");
                        }
                    }
                }
                var definitions = new Dictionary<string, RuntimeTileDefinition>(StringComparer.Ordinal);
                if (value.TryGetProperty("tiles", out var tiles) && tiles.ValueKind == JsonValueKind.Array)
                {
                    foreach (var tile in tiles.EnumerateArray())
                    {
                        var tileId = CanonicalId(RequiredString(tile, "tileId"), "tile id");
                        var hash = SHA256.HashData(System.Text.Encoding.UTF8.GetBytes(tileId));
                        var source = tile.TryGetProperty("source", out var sourceValue)
                            && sourceValue.ValueKind == JsonValueKind.Object ? sourceValue : tile;
                        var tileTextureId = OptionalString(tile, "textureAssetId") is { } candidateTexture
                            ? CanonicalId(candidateTexture, "tile texture asset id") : textureAssetId;
                        var animationFrames = new List<RenderTileAnimationFrame>();
                        var minimumSpeed = 1.0f;
                        var maximumSpeed = 1.0f;
                        var startTime = 0.0f;
                        var startFrame = 0;
                        var loopOnce = false;
                        var paused = false;
                        var customTypeId = string.Empty;
                        var customPayloadJson = "{}";
                        if (snapshotFormatVersion >= 5
                            && tile.TryGetProperty("animation", out var animation)
                            && animation.ValueKind == JsonValueKind.Object)
                        {
                            minimumSpeed = Math.Max(0, ReadFloat(animation, "minimumSpeed", 1));
                            maximumSpeed = Math.Max(minimumSpeed, ReadFloat(animation, "maximumSpeed", minimumSpeed));
                            startTime = ReadFloat(animation, "startTime", 0);
                            startFrame = Math.Max(0, ReadInt(animation, "startFrame", 0));
                            loopOnce = OptionalBool(animation, "loopOnce", false);
                            paused = OptionalBool(animation, "paused", false);
                            if (animation.TryGetProperty("frames", out var frames)
                                && frames.ValueKind == JsonValueKind.Array)
                            {
                                foreach (var frame in frames.EnumerateArray())
                                {
                                    if (!frame.TryGetProperty("sprite", out var sprite)
                                        || sprite.ValueKind != JsonValueKind.Object) continue;
                                    var frameTextureId = OptionalString(sprite, "textureAssetId") is { } frameTexture
                                        ? CanonicalId(frameTexture, "animation texture asset id") : tileTextureId;
                                    var frameSource = sprite.TryGetProperty("source", out var frameSourceValue)
                                        && frameSourceValue.ValueKind == JsonValueKind.Object ? frameSourceValue : sprite;
                                    animationFrames.Add(new RenderTileAnimationFrame(
                                        frameTextureId,
                                        ReadInt(frameSource, "x", 0),
                                        ReadInt(frameSource, "y", 0),
                                        Math.Max(1, ReadInt(frameSource, "width", 1)),
                                        Math.Max(1, ReadInt(frameSource, "height", 1)),
                                        Math.Max(0.000001f, ReadFloat(frame, "durationSeconds", 1.0f / 12.0f))));
                                }
                            }
                        }
                        if (snapshotFormatVersion >= 5
                            && ReadString(tile, "kind", "basic") == "custom"
                            && tile.TryGetProperty("custom", out var custom)
                            && custom.ValueKind == JsonValueKind.Object)
                        {
                            customTypeId = ReadString(custom, "typeId", string.Empty);
                            if (custom.TryGetProperty("payload", out var payload))
                                customPayloadJson = payload.GetRawText();
                        }
                        definitions[tileId] = new RuntimeTileDefinition(
                            new RenderColor(
                                0.25f + (hash[0] / 255.0f * 0.7f),
                                0.25f + (hash[1] / 255.0f * 0.7f),
                                0.25f + (hash[2] / 255.0f * 0.7f),
                                1),
                            tileTextureId,
                            ReadInt(source, snapshotFormatVersion >= 5 ? "x" : "sourceX", 0),
                            ReadInt(source, snapshotFormatVersion >= 5 ? "y" : "sourceY", 0),
                            Math.Max(1, ReadInt(source, snapshotFormatVersion >= 5 ? "width" : "sourceWidth", 1)),
                            Math.Max(1, ReadInt(source, snapshotFormatVersion >= 5 ? "height" : "sourceHeight", 1)),
                            animationFrames, minimumSpeed, maximumSpeed, startTime, startFrame, loopOnce, paused,
                            customTypeId, customPayloadJson);
                    }
                }
                sets.Add(id, new RuntimeTileSet(id, textures, cellWidth, cellHeight, definitions));
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
            var mapSets = new List<RuntimeTileSet>();
            if (value.TryGetProperty("tileSetDependencies", out var dependencies)
                && dependencies.ValueKind == JsonValueKind.Array)
            {
                foreach (var dependency in dependencies.EnumerateArray())
                {
                    if (dependency.ValueKind == JsonValueKind.String
                        && sets.TryGetValue(dependency.GetString()!, out var dependencySet))
                    {
                        mapSets.Add(dependencySet);
                    }
                }
            }
            if (mapSets.Count == 0)
            {
                diagnostics.Add($"Tilemap {id} has no resolved flattened TileSet dependency.");
                continue;
            }
            var primarySet = mapSets[0];
            var grid = value.TryGetProperty("grid", out var gridValue)
                && gridValue.ValueKind == JsonValueKind.Object ? gridValue : value;
            var gridCellSize = grid.TryGetProperty("cellSize", out var gridCellSizeValue)
                && gridCellSizeValue.ValueKind == JsonValueKind.Object ? gridCellSizeValue : grid;
            var gridGap = grid.TryGetProperty("cellGap", out var gridGapValue)
                && gridGapValue.ValueKind == JsonValueKind.Object ? gridGapValue : grid;
            var tileAnchor = grid.TryGetProperty("tileAnchor", out var tileAnchorValue)
                && tileAnchorValue.ValueKind == JsonValueKind.Object ? tileAnchorValue : grid;
            var layout = ReadString(grid, "layout", "rectangular") switch
            {
                "hex-point-top" => RenderGridLayout.HexPointTop,
                "hex-flat-top" => RenderGridLayout.HexFlatTop,
                "isometric" => RenderGridLayout.Isometric,
                "isometric-z-as-y" => RenderGridLayout.IsometricZAsY,
                _ => RenderGridLayout.Rectangular,
            };
            var cellWidth = snapshotFormatVersion >= 5 ? ReadFloat(gridCellSize, "x", 1) : primarySet.CellWidth;
            var cellHeight = snapshotFormatVersion >= 5 ? ReadFloat(gridCellSize, "y", 1) : primarySet.CellHeight;
            var textures = new Dictionary<string, RenderTileTexture>(StringComparer.Ordinal);
            foreach (var mapSet in mapSets)
            {
                foreach (var texture in mapSet.Textures) textures[texture.Key] = texture.Value;
            }
            var layers = new List<RenderTileLayer>();
            if (value.TryGetProperty("layers", out var layerValues) && layerValues.ValueKind == JsonValueKind.Array)
            {
                foreach (var layer in layerValues.EnumerateArray())
                {
                    var layerId = OptionalString(layer, "layerId") ?? string.Empty;
                    var cells = new List<RenderTileCell>();
                    if (layer.TryGetProperty("cells", out var cellValues) && cellValues.ValueKind == JsonValueKind.Array)
                    {
                        foreach (var cell in cellValues.EnumerateArray())
                        {
                            var tileId = CanonicalId(RequiredString(cell, "tileId"), "tile cell id");
                            var tileSetId = OptionalString(cell, "tileSetId") is { } qualifiedSet
                                ? CanonicalId(qualifiedSet, "tile cell TileSet id") : primarySet.AssetId;
                            var resolvedTileSetId = OptionalString(cell, "resolvedTileSetId") is { } resolvedSet
                                ? CanonicalId(resolvedSet, "resolved tile cell TileSet id") : tileSetId;
                            var resolvedTileId = OptionalString(cell, "resolvedTileId") is { } resolvedTile
                                ? CanonicalId(resolvedTile, "resolved tile cell id") : tileId;
                            var set = mapSets.FirstOrDefault(candidate => candidate.AssetId == resolvedTileSetId)
                                ?? primarySet;
                            if (!set.Tiles.TryGetValue(resolvedTileId, out var tile))
                            {
                                diagnostics.Add($"Tilemap {id} retained missing tile id {resolvedTileId}.");
                                tile = new RuntimeTileDefinition(new RenderColor(1, 0, 1, 1), string.Empty,
                                    0, 0, 1, 1, Array.Empty<RenderTileAnimationFrame>(), 1, 1, 0, 0, false, false,
                                    string.Empty, "{}");
                            }
                            var cellX = ReadInt(cell, "x", 0);
                            var cellY = ReadInt(cell, "y", 0);
                            var sourceX = tile.SourceX;
                            var sourceY = tile.SourceY;
                            var sourceWidth = tile.SourceWidth;
                            var sourceHeight = tile.SourceHeight;
                            var renderTextureAssetId = tile.TextureAssetId;
                            if (cell.TryGetProperty("resolvedSprite", out var resolvedSprite)
                                && resolvedSprite.ValueKind == JsonValueKind.Object)
                            {
                                if (OptionalString(resolvedSprite, "textureAssetId") is { } spriteTexture)
                                    renderTextureAssetId = CanonicalId(spriteTexture, "resolved sprite texture asset id");
                                var resolvedSource = resolvedSprite.TryGetProperty("source", out var resolvedSourceValue)
                                    && resolvedSourceValue.ValueKind == JsonValueKind.Object
                                        ? resolvedSourceValue : resolvedSprite;
                                sourceX = ReadInt(resolvedSource, "x", sourceX);
                                sourceY = ReadInt(resolvedSource, "y", sourceY);
                                sourceWidth = Math.Max(1, ReadInt(resolvedSource, "width", sourceWidth));
                                sourceHeight = Math.Max(1, ReadInt(resolvedSource, "height", sourceHeight));
                            }
                            var offset = cell.TryGetProperty("offset", out var offsetValue)
                                && offsetValue.ValueKind == JsonValueKind.Object ? offsetValue : cell;
                            var scale = cell.TryGetProperty("scale", out var scaleValue)
                                && scaleValue.ValueKind == JsonValueKind.Object ? scaleValue : cell;
                            var flipX = OptionalBool(cell, "flipX", false);
                            var flipY = OptionalBool(cell, "flipY", false);
                            var rotationDegrees = ReadFloat(cell, "rotationDegrees", 0);
                            var offsetX = ReadFloat(offset, "x", 0);
                            var offsetY = ReadFloat(offset, "y", 0);
                            var scaleX = ReadFloat(scale, "x", 1);
                            var scaleY = ReadFloat(scale, "y", 1);
                            var elevation = ReadInt(cell, "elevation", 0);
                            var placeholder = OptionalBool(cell, "placeholder", false);
                            var color = ReadColor(cell, "tint", placeholder
                                ? new RenderColor(1, 0, 1, 1)
                                : textures.ContainsKey(renderTextureAssetId) ? RenderColor.White : tile.Color);
                            if (!string.IsNullOrWhiteSpace(tile.CustomTypeId))
                            {
                                if (projectRuntime is null)
                                {
                                    diagnostics.Add(
                                        $"Custom tile {tileId} requires unavailable extension {tile.CustomTypeId}; opaque data was preserved.");
                                }
                                else
                                {
                                    var context = new TileExtensionContext(
                                        cellX, cellY, elevation, checked((uint)layout),
                                        StableTileExtensionSeed(id, layerId, cellX, cellY, tile.CustomTypeId),
                                        0.0, id, layerId, tileSetId, tileId,
                                        tile.CustomPayloadJson, "{}");
                                    var evaluated = projectRuntime.EvaluateTiles(tile.CustomTypeId, [context])[0];
                                    if (evaluated.Succeeded)
                                    {
                                        using var extensionDocument = JsonDocument.Parse(evaluated.ResultJson);
                                        var extension = extensionDocument.RootElement;
                                        color = ReadColor(extension, "tint", color);
                                        flipX = OptionalBool(extension, "flipX", flipX);
                                        flipY = OptionalBool(extension, "flipY", flipY);
                                        rotationDegrees = ReadFloat(extension, "rotationDegrees", rotationDegrees);
                                        elevation = ReadInt(extension, "elevation", elevation);
                                        if (extension.TryGetProperty("offset", out var extensionOffset)
                                            && extensionOffset.ValueKind == JsonValueKind.Object)
                                        {
                                            offsetX = ReadFloat(extensionOffset, "x", offsetX);
                                            offsetY = ReadFloat(extensionOffset, "y", offsetY);
                                        }
                                        if (extension.TryGetProperty("scale", out var extensionScale)
                                            && extensionScale.ValueKind == JsonValueKind.Object)
                                        {
                                            scaleX = ReadFloat(extensionScale, "x", scaleX);
                                            scaleY = ReadFloat(extensionScale, "y", scaleY);
                                        }
                                        if (OptionalString(extension, "tileSetId") is { } extensionSetId
                                            && OptionalString(extension, "tileId") is { } extensionTileId)
                                        {
                                            var outputSetId = CanonicalId(extensionSetId,
                                                "tile-extension output TileSet id");
                                            var outputTileId = CanonicalId(extensionTileId,
                                                "tile-extension output tile id");
                                            var outputSet = mapSets.FirstOrDefault(candidate => candidate.AssetId == outputSetId);
                                            if (outputSet is not null
                                                && outputSet.Tiles.TryGetValue(outputTileId, out var outputTile))
                                            {
                                                tile = outputTile;
                                                renderTextureAssetId = tile.TextureAssetId;
                                                sourceX = tile.SourceX;
                                                sourceY = tile.SourceY;
                                                sourceWidth = tile.SourceWidth;
                                                sourceHeight = tile.SourceHeight;
                                            }
                                            else
                                            {
                                                diagnostics.Add(
                                                    $"Tile extension {tile.CustomTypeId} proposed an unresolved tile output; the source tile was retained.");
                                            }
                                        }
                                        if (extension.TryGetProperty("sprite", out var extensionSprite)
                                            && extensionSprite.ValueKind == JsonValueKind.Object)
                                        {
                                            if (OptionalString(extensionSprite, "textureAssetId") is { } extensionTexture)
                                                renderTextureAssetId = CanonicalId(extensionTexture,
                                                    "tile-extension sprite texture id");
                                            var extensionSource = extensionSprite.TryGetProperty("source", out var valueSource)
                                                && valueSource.ValueKind == JsonValueKind.Object
                                                    ? valueSource : extensionSprite;
                                            sourceX = ReadInt(extensionSource, "x", sourceX);
                                            sourceY = ReadInt(extensionSource, "y", sourceY);
                                            sourceWidth = Math.Max(1, ReadInt(extensionSource, "width", sourceWidth));
                                            sourceHeight = Math.Max(1, ReadInt(extensionSource, "height", sourceHeight));
                                        }
                                        placeholder = false;
                                    }
                                    else
                                    {
                                        diagnostics.Add(
                                            $"Custom tile {tileId} extension {tile.CustomTypeId} was disabled for this cell: "
                                            + $"{evaluated.ErrorCode} {evaluated.ErrorMessage}");
                                    }
                                }
                            }
                            cells.Add(new RenderTileCell(
                                cellX,
                                cellY,
                                tileSetId,
                                tileId,
                                color,
                                renderTextureAssetId,
                                sourceX,
                                sourceY,
                                sourceWidth,
                                sourceHeight,
                                flipX,
                                flipY,
                                Math.Clamp(ReadInt(cell, "rotationQuarterTurns", 0), 0, 3),
                                rotationDegrees,
                                offsetX,
                                offsetY,
                                scaleX,
                                scaleY,
                                elevation,
                                OptionalBool(cell, "lockColor", false),
                                OptionalBool(cell, "lockTransform", false),
                                tile.AnimationFrames,
                                tile.MinimumAnimationSpeed,
                                tile.MaximumAnimationSpeed,
                                tile.AnimationStartTime,
                                tile.AnimationStartFrame,
                                tile.AnimationLoopOnce,
                                tile.AnimationPaused));
                        }
                    }
                    var renderer = layer.TryGetProperty("renderer", out var rendererValue)
                        && rendererValue.ValueKind == JsonValueKind.Object ? rendererValue : layer;
                    var cullingPadding = renderer.TryGetProperty("cullingPadding", out var cullingValue)
                        && cullingValue.ValueKind == JsonValueKind.Object ? cullingValue : renderer;
                    layers.Add(new RenderTileLayer(
                        OptionalString(layer, "name") ?? "Layer",
                        OptionalBool(layer, "visible", true),
                        ReadInt(layer, "order", 0),
                        ReadColor(renderer, "tint", RenderColor.White),
                        OptionalString(renderer, "materialAssetId"),
                        ReadInt(renderer, "sortOrder", 0),
                        ReadString(renderer, "mode", "chunk") == "individual",
                        Math.Max(0, ReadFloat(renderer, "animationRate", 1)),
                        ReadFloat(cullingPadding, "x", 0),
                        ReadFloat(cullingPadding, "y", 0),
                        cells));
                }
            }
            result.Add(id, new RenderTilemap(id, textures, layout, cellWidth, cellHeight,
                snapshotFormatVersion >= 5 ? ReadFloat(gridGap, "x", 0) : 0,
                snapshotFormatVersion >= 5 ? ReadFloat(gridGap, "y", 0) : 0,
                snapshotFormatVersion >= 5 ? ReadFloat(tileAnchor, "x", 0.5f) : 0.5f,
                snapshotFormatVersion >= 5 ? ReadFloat(tileAnchor, "y", 0.5f) : 0.5f,
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

    private static ulong StableTileExtensionSeed(
        string mapId,
        string layerId,
        int x,
        int y,
        string typeId)
    {
        var bytes = SHA256.HashData(Encoding.UTF8.GetBytes(
            $"{mapId}\n{layerId}\n{x}\n{y}\n{typeId}"));
        return System.Buffers.Binary.BinaryPrimitives.ReadUInt64LittleEndian(bytes);
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
