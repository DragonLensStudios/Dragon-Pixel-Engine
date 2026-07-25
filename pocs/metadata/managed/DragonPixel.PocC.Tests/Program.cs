using System.Diagnostics;
using System.Text.Json;
using System.Text.Json.Nodes;
using DragonPixel.PocC.Generated;
using Json.Schema;

namespace DragonPixel.PocC;

internal static class Program
{
    private static readonly Dictionary<string, JsonSchema> SchemaCache = new(StringComparer.OrdinalIgnoreCase);

    private const string SceneId = "e1546948-334a-473c-8168-a9e9c12010b7";
    private const string EntityId = "fd58a0d3-15bf-4617-9ec1-45fac7e20ab0";
    private const string TargetEntityId = "4c6af5f2-df1b-48cb-9c39-bf91a3d9a787";

    private static int Main(string[] args)
    {
        try
        {
            if (args.Length != 2)
            {
                throw new ArgumentException("Usage: DragonPixel.PocC.Tests <repository-root> <native-manifest-executable>");
            }

            var repositoryRoot = Path.GetFullPath(args[0]);
            var nativeManifestJson = ReadNativeManifest(args[1]);
            var managedManifestJson = GeneratedComponentManifest.Json;
            var metadataSchemaPath = Path.Combine(repositoryRoot, "schemas", "v2", "component-metadata.schema.json");
            var sceneSchemaPath = Path.Combine(repositoryRoot, "schemas", "v2", "scene.schema.json");
            var currentSceneSchemaPath = Path.Combine(repositoryRoot, "schemas", "v3", "scene.schema.json");
            var currentProjectSchemaPath = Path.Combine(repositoryRoot, "schemas", "v2", "project.schema.json");
            var currentAssetSchemaPath = Path.Combine(repositoryRoot, "schemas", "v2", "asset-metadata.schema.json");

            ValidateAgainstSchema(metadataSchemaPath, nativeManifestJson, "native manifest");
            ValidateAgainstSchema(metadataSchemaPath, managedManifestJson, "managed generated manifest");

            var nativeManifest = ParseObject(nativeManifestJson);
            var managedManifest = ParseObject(managedManifestJson);
            VerifySharedMetadataShape(nativeManifest, managedManifest);

            var scene = SceneDocument.Parse(CreateSceneJson());
            ValidateAgainstSchema(sceneSchemaPath, scene.Save(), "initial scene");
            VerifyHeadlessInspector(scene, nativeManifest, managedManifest);
            VerifyRoundTripAndOpaquePreservation(scene, sceneSchemaPath);
            VerifyStableIdRename(scene, nativeManifest, managedManifest);
            VerifyExplicitMigration(scene, sceneSchemaPath);
            ValidateAgainstSchema(
                currentSceneSchemaPath,
                File.ReadAllText(Path.Combine(repositoryRoot, "samples", "Slice1Sample", "Scenes", "Main.dpescene")),
                "Slice 1 sample scene");
            ValidateAgainstSchema(
                currentProjectSchemaPath,
                File.ReadAllText(Path.Combine(repositoryRoot, "samples", "Slice1Sample", "DragonPixelProject.json")),
                "Slice 1 sample project");
            ValidateAgainstSchema(
                currentAssetSchemaPath,
                File.ReadAllText(Path.Combine(repositoryRoot, "samples", "Slice1Sample", "Assets", "dragon.sprite.dpeasset")),
                "Slice 1 sample sprite metadata");

            Console.WriteLine("POC C passed: shared schema, source-generated managed metadata, native metadata,");
            Console.WriteLine("headless inspection, deterministic JSON, opaque preservation, stable-ID rename, and migration.");
            return 0;
        }
        catch (Exception exception)
        {
            Console.Error.WriteLine(exception);
            return 1;
        }
    }

    private static string ReadNativeManifest(string executablePath)
    {
        using var process = Process.Start(new ProcessStartInfo
        {
            FileName = Path.GetFullPath(executablePath),
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            UseShellExecute = false,
            CreateNoWindow = true,
        }) ?? throw new InvalidOperationException("Could not start native manifest emitter.");

        var stdout = process.StandardOutput.ReadToEnd();
        var stderr = process.StandardError.ReadToEnd();
        process.WaitForExit();
        if (process.ExitCode != 0)
        {
            throw new InvalidOperationException($"Native manifest emitter exited {process.ExitCode}: {stderr}");
        }

        return stdout;
    }

    private static void ValidateAgainstSchema(string schemaPath, string json, string description)
    {
        if (!SchemaCache.TryGetValue(schemaPath, out var schema))
        {
            schema = JsonSchema.FromText(File.ReadAllText(schemaPath));
            SchemaCache.Add(schemaPath, schema);
        }
        using var instance = JsonDocument.Parse(json);
        var results = schema.Evaluate(instance.RootElement, new EvaluationOptions { OutputFormat = OutputFormat.List });
        Assert(results.IsValid, $"{description} did not satisfy {Path.GetFileName(schemaPath)}.");
    }

    private static JsonObject ParseObject(string json) => JsonNode.Parse(json)?.AsObject()
        ?? throw new InvalidDataException("Expected a JSON object.");

    private static void VerifySharedMetadataShape(JsonObject nativeManifest, JsonObject managedManifest)
    {
        Assert(nativeManifest["format"]!.GetValue<string>() == managedManifest["format"]!.GetValue<string>(),
            "Native and managed manifests use different formats.");
        Assert(nativeManifest["formatVersion"]!.GetValue<int>() == managedManifest["formatVersion"]!.GetValue<int>(),
            "Native and managed manifests use different format versions.");
        Assert(nativeManifest["components"]!.AsArray()[0]!["owner"]!.GetValue<string>() == "native",
            "Native registration did not declare native ownership.");
        Assert(managedManifest["components"]!.AsArray()[0]!["owner"]!.GetValue<string>() == "managed",
            "Managed source generation did not declare managed ownership.");
        Assert(Guid.TryParse(nativeManifest["components"]!.AsArray()[0]!["typeId"]!.GetValue<string>(), out _)
               && Guid.TryParse(managedManifest["components"]!.AsArray()[0]!["typeId"]!.GetValue<string>(), out _),
            "Native and managed metadata did not use UUID component type identities.");
    }

    private static void VerifyHeadlessInspector(
        SceneDocument scene,
        JsonObject nativeManifest,
        JsonObject managedManifest)
    {
        var catalog = new MetadataCatalog(nativeManifest, managedManifest);
        var transform = catalog.Inspect(scene.Component(PocComponentIds.NativeTransform));
        var rotator = catalog.Inspect(scene.Component(PocComponentIds.ManagedRotator));
        var missing = catalog.Inspect(scene.Component(PocComponentIds.MissingSparkle));
        var newer = catalog.Inspect(scene.Component(PocComponentIds.UnknownNewer));

        Assert(transform.State == InspectorComponentState.Editable && transform.Properties.Count == 3,
            "Native component was not editable through metadata.");
        Assert(rotator.State == InspectorComponentState.Editable && rotator.Properties.Count == 2,
            "Managed component was not editable through generated metadata.");
        Assert(rotator.Properties[0].Value!.GetValue<double>() == 12.345678901234567,
            "Inspector lost numeric precision.");
        Assert(rotator.Properties[1].Value!.GetValue<string>() == TargetEntityId,
            "Inspector lost an entity reference.");
        Assert(missing.State == InspectorComponentState.OpaqueMissingType,
            "Missing component was not represented as opaque.");
        Assert(newer.State == InspectorComponentState.OpaqueMissingType,
            "Unknown newer component was not represented as opaque.");

        var knownNewer = scene.Component(PocComponentIds.ManagedRotator).DeepClone().AsObject();
        knownNewer["schemaVersion"] = 99;
        Assert(catalog.Inspect(knownNewer).State == InspectorComponentState.OpaqueNewerSchema,
            "Known component with a newer schema was not represented as opaque.");
    }

    private static void VerifyRoundTripAndOpaquePreservation(SceneDocument scene, string sceneSchemaPath)
    {
        var missingBefore = scene.Component(PocComponentIds.MissingSparkle).DeepClone();
        var newerBefore = scene.Component(PocComponentIds.UnknownNewer).DeepClone();
        var firstSave = scene.Save();
        var reloaded = SceneDocument.Parse(firstSave);
        var secondSave = reloaded.Save();

        Assert(firstSave == secondSave, "Canonical scene JSON was not byte-deterministic after reload.");
        Assert(JsonNode.DeepEquals(missingBefore, reloaded.Component(PocComponentIds.MissingSparkle)),
            "Missing component payload did not round-trip unchanged.");
        Assert(JsonNode.DeepEquals(newerBefore, reloaded.Component(PocComponentIds.UnknownNewer)),
            "Version-mismatched component payload did not round-trip unchanged.");
        var precise = reloaded.Component(PocComponentIds.MissingSparkle)["properties"]!["precise"]!.GetValue<decimal>();
        Assert(precise == 0.12345678901234567890m,
            "High-precision decimal payload did not survive serialization.");
        ValidateAgainstSchema(sceneSchemaPath, secondSave, "round-tripped scene");
    }

    private static void VerifyStableIdRename(
        SceneDocument scene,
        JsonObject nativeManifest,
        JsonObject managedManifest)
    {
        var renamedManifest = managedManifest.DeepClone().AsObject();
        var component = renamedManifest["components"]!.AsArray()[0]!.AsObject();
        component["displayName"] = "Angular Motion";
        component["properties"]!.AsArray()[0]!["displayName"] = "Angular speed";

        var catalog = new MetadataCatalog(nativeManifest, renamedManifest);
        var inspected = catalog.Inspect(scene.Component(PocComponentIds.ManagedRotator));
        Assert(inspected.State == InspectorComponentState.Editable,
            "A display-name rename incorrectly required data migration.");
        Assert(inspected.DisplayName == "Angular Motion" && inspected.Properties[0].DisplayName == "Angular speed",
            "Inspector did not resolve renamed labels through stable IDs.");
        Assert(inspected.Properties[0].PropertyId == "dpe.poc.rotator.speed",
            "A display-name rename changed a stable property ID.");
    }

    private static void VerifyExplicitMigration(SceneDocument source, string sceneSchemaPath)
    {
        var scene = SceneDocument.Parse(source.Save());
        var missingBefore = scene.Component(PocComponentIds.MissingSparkle).DeepClone();
        var report = scene.MigrateTransformV1ToV2();
        var transform = scene.Component(PocComponentIds.NativeTransform);
        var properties = transform["properties"]!.AsObject();

        Assert(report.MigratedRecords == 1 && report.FromVersion == 1 && report.ToVersion == 2,
            "Migration report did not describe the explicit schema transition.");
        Assert(transform["schemaVersion"]!.GetValue<int>() == 2,
            "Migrated component schema version was not advanced.");
        Assert(properties.ContainsKey("dpe.poc.transform.position")
               && !properties.ContainsKey("dpe.poc.transform.translation"),
            "Migration did not rename the serialized stable property ID.");
        Assert(JsonNode.DeepEquals(missingBefore, scene.Component(PocComponentIds.MissingSparkle)),
            "Migration modified an opaque missing component.");
        ValidateAgainstSchema(sceneSchemaPath, scene.Save(), "migrated scene");
    }

    private static string CreateSceneJson() => $$"""
        {
          "$schema": "https://dragonpixel.dev/schemas/v2/scene.schema.json",
          "format": "dpe.scene",
          "formatVersion": 2,
          "engineVersion": "0.1.0-slice1",
          "sceneId": "{{SceneId}}",
          "entities": [
            {
              "enabled": true,
              "id": "{{EntityId}}",
              "name": "Metadata Probe",
              "parentId": null,
              "components": [
                {
                  "typeId": "{{PocComponentIds.NativeTransform}}",
                  "qualifiedName": "DragonPixel.PocC.NativeTransformComponent",
                  "schemaVersion": 1,
                  "owner": "native",
                  "enabled": true,
                  "properties": {
                    "dpe.poc.transform.translation": { "x": 1.25, "y": -2.5, "z": 3.75 },
                    "dpe.poc.transform.rotation": { "x": 0.0, "y": 0.0, "z": 0.0, "w": 1.0 },
                    "dpe.poc.transform.visible": true
                  }
                },
                {
                  "typeId": "{{PocComponentIds.ManagedRotator}}",
                  "qualifiedName": "DragonPixel.PocC.RotatorComponent",
                  "schemaVersion": 1,
                  "owner": "managed",
                  "enabled": true,
                  "properties": {
                    "dpe.poc.rotator.speed": 12.345678901234567,
                    "dpe.poc.rotator.target": "{{TargetEntityId}}"
                  }
                },
                {
                  "typeId": "{{PocComponentIds.MissingSparkle}}",
                  "qualifiedName": "Vendor.Missing.SparkleComponent",
                  "schemaVersion": 47,
                  "owner": "unknown",
                  "enabled": true,
                  "vendorExtension": { "keep": "exactly" },
                  "properties": {
                    "opaqueArray": [1, "two", null, { "nested": true }],
                    "precise": 0.12345678901234567890
                  }
                },
                {
                  "typeId": "{{PocComponentIds.UnknownNewer}}",
                  "qualifiedName": "DragonPixel.PocC.FutureRotatorComponent",
                  "schemaVersion": 8,
                  "owner": "managed",
                  "enabled": false,
                  "properties": {
                    "future": { "shape": [3, 2, 1] }
                  }
                }
              ]
            }
          ]
        }
        """;

    private static void Assert(bool condition, string message)
    {
        if (!condition)
        {
            throw new InvalidOperationException(message);
        }
    }
}
