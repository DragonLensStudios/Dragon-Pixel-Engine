using System.Reflection;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text.Json;
using System.Text.Json.Nodes;
using DragonPixel.Contracts;
using DragonPixel.Runtime;
using NumericsVector3 = System.Numerics.Vector3;

const string ManagedTypeId = "58f2e185-f112-442b-a56d-f6d113426081";
const string ManagedModuleId = "92c8a919-605e-479e-a3d3-238995946f26";
const string NativeTypeId = "ce9c24d8-278e-457d-b733-3671c85f6a45";
const string NativeModuleId = "e2744f01-d2dd-4893-a0e4-aca83c33c9cf";
const string LegacyNativeTypeId = "858ee1db-6c57-4bc2-b245-e22cc16cdbbd";
const string LegacyNativeModuleId = "b4717394-89f3-46f8-8350-fb64eb560e28";
const string UnlistedTypeId = "cbfef6b1-e265-4f8e-9f5c-53d0f4c366d7";
const string DisabledUnlistedTypeId = "6294ab6c-5689-43f9-845b-b357a0a17b18";
const string TestEntityId = "67dcfb82-f82a-4dd1-a962-21acc55db02c";
const string TileExtensionId = "example.weather-tile";

if (args.Length != 2 || !File.Exists(args[0]) || !File.Exists(args[1]))
{
    Console.Error.WriteLine("Expected the native v2 and legacy v1 project-component test plugin paths.");
    return 2;
}

var root = Path.Combine(Path.GetTempPath(), $"dpe-component-runtime-{Guid.NewGuid():N}");
Directory.CreateDirectory(root);
try
{
    var fixture = CreateFixture(root, Path.GetFullPath(args[0]));
    var snapshotPath = Path.Combine(root, "snapshot.json");
    WriteSnapshot(snapshotPath,
        SnapshotComponent(ManagedTypeId, enabled: true, new JsonObject { ["speed"] = 2.0 }),
        SnapshotComponent(NativeTypeId, enabled: true, new JsonObject { ["pulse"] = 4.0 }));
    var managedFailureSnapshotPath = Path.Combine(root, "managed-initialize-failure.json");
    WriteSnapshot(managedFailureSnapshotPath,
        SnapshotComponent(ManagedTypeId, enabled: true,
            new JsonObject { ["failInitialize"] = true }));
    var nativeFailureSnapshotPath = Path.Combine(root, "native-properties-failure.json");
    WriteSnapshot(nativeFailureSnapshotPath,
        SnapshotComponent(NativeTypeId, enabled: true,
            new JsonObject { ["failSetProperties"] = true }));
    var lifecycleFailureSnapshotPath = Path.Combine(root, "lifecycle-stage-failure.json");
    WriteSnapshot(lifecycleFailureSnapshotPath,
        SnapshotComponent(ManagedTypeId, enabled: true,
            new JsonObject { ["failLateUpdate"] = true }),
        SnapshotComponent(NativeTypeId, enabled: true,
            new JsonObject { ["failLateUpdate"] = true }));
    var missingFactorySnapshotPath = Path.Combine(root, "missing-factory.json");
    WriteSnapshot(missingFactorySnapshotPath,
        SnapshotComponent(UnlistedTypeId, enabled: true, new JsonObject()),
        SnapshotComponent(DisabledUnlistedTypeId, enabled: false, new JsonObject()),
        SnapshotComponent(BuiltinComponentIds.Transform, enabled: true, new JsonObject()));

    VerifyImmutableInputContracts();
    VerifyGameObjectControllerContract();
    VerifyRuntimeAssetSnapshot(root);

    ExpectRejected(fixture, manifest =>
        ManagedModule(manifest)["sha256"] = new string('0', 64),
        "Tampered artifact hash must be rejected before module loading.");

    var escapedArtifact = Path.Combine(root, "escaped-managed.dll");
    File.Copy(fixture.ManagedArtifact, escapedArtifact);
    ExpectRejected(fixture, manifest =>
    {
        ManagedModule(manifest)["path"] = escapedArtifact;
        ManagedModule(manifest)["sha256"] = Sha256(escapedArtifact);
    }, "Artifact paths outside .dragonpixel/Cache must be rejected.");

    ExpectRejected(fixture, manifest => manifest["platform"] = OtherPlatform(),
        "Wrong-platform runtime manifests must be rejected.");
    ExpectRejected(fixture, manifest => manifest["architecture"] = OtherArchitecture(),
        "Wrong-architecture runtime manifests must be rejected.");
    ExpectRejected(fixture, manifest => ManagedComponent(manifest)["moduleId"] = "test.managed",
        "Malformed module IDs must be rejected.");
    ExpectRejected(fixture, manifest => NativeComponent(manifest)["typeId"] = ManagedTypeId,
        "Duplicate stable type IDs must be rejected.");
    ExpectRejected(fixture, manifest => ManagedComponent(manifest)["typeId"] = ManagedTypeId.ToUpperInvariant(),
        "Case aliases/non-canonical stable IDs must be rejected.");
    ExpectRejected(fixture, manifest => ManagedComponent(manifest)["typeId"] = UnlistedTypeId,
        "A managed factory not listed by exact stable ID must be rejected after load.");
    ExpectRejected(fixture, manifest => NativeComponent(manifest)["typeId"] = UnlistedTypeId,
        "A native plugin type mismatch after a valid managed load must fail closed.");
    ExpectRejected(fixture, manifest =>
        ((JsonObject)manifest["toolIdentities"]!).Remove("cxx"),
        "The canonical C++ tool identity is required.");
    ExpectRejected(fixture, manifest =>
        ((JsonObject)manifest["toolIdentities"]!)["cxx"] = new string('A', 64),
        "The C++ tool identity must be a canonical lowercase SHA-256.");
    ExpectRejected(fixture, manifest => manifest["buildHash"] = new string('f', 64),
        "The manifest buildHash must select the artifact build directory.");

    var deletedArtifact = Path.Combine(fixture.BuildDirectory, "managed", "deleted.dll");
    ExpectRejected(fixture, manifest =>
    {
        ManagedModule(manifest)["path"] = deletedArtifact;
        ManagedModule(manifest)["sha256"] = new string('1', 64);
    }, "Deleted artifacts must be rejected.");

    TestLinkedArtifactWhenSupported(fixture);
    TestLinkedManifestWhenSupported(fixture);
    TestServiceWriterManifestPlacements(fixture);

    WriteManifest(fixture.ManifestPath, CreateValidManifest(fixture));
    var runtime = new ProjectComponentRuntime(fixture.ManifestPath);
    try
    {
        Require(runtime.FactoryCount == 2, "Both strictly validated worker-only factories must load.");
        Require(runtime.TileExtensionCount == 1,
            "The explicitly declared worker-only tile extension must load.");
        var tileContext = new TileExtensionContext(
            4, -7, 2, 0, 1234, 0.5,
            "fd2f3574-8e6f-43d1-bc96-c6650ab49a54",
            "59737391-9417-45bf-a8af-cb6e24e7aa38",
            "4fe655df-c40f-4e48-a5cc-fbe9bd356ac6",
            "a9ba355a-51e8-49e9-b581-c6174026c160",
            "{}", "{}");
        var tileResults = runtime.EvaluateTiles(TileExtensionId, [tileContext]);
        Require(tileResults.Count == 1 && tileResults[0].Succeeded
                && tileResults[0].ResultJson.Contains("\"tint\"", StringComparison.Ordinal),
            "The tile extension did not return one validated evaluation result.");
        var malformedResults = runtime.EvaluateTiles(TileExtensionId,
            [tileContext with { PayloadJson = "{\"malformed\":true}" }]);
        Require(malformedResults.Count == 1 && !malformedResults[0].Succeeded
                && malformedResults[0].ErrorCode == "DPE-TILE-EXT-MALFORMED-RESULT",
            "Malformed tile-extension evaluation output was not contained.");
        var proposal = runtime.ProposeBrush(TileExtensionId, tileContext, "{}");
        Require(proposal.Succeeded && proposal.CommandCount == 1,
            "The tile extension did not return one bounded brush command proposal.");
        var malformedProposal = runtime.ProposeBrush(
            TileExtensionId, tileContext, "{\"malformed\":true}");
        Require(!malformedProposal.Succeeded
                && malformedProposal.ErrorCode == "DPE-TILE-EXT-MALFORMED-PROPOSAL",
            "Malformed tile-extension brush proposals were not rejected.");
        VerifyCustomTileExtensionSnapshot(root, runtime);
        Require(runtime.Diagnostics.Any(value => value.Contains(fixture.BuildHash, StringComparison.Ordinal)),
            "The validated buildHash was not retained in the runtime evidence.");

        runtime.ReloadSnapshot(missingFactorySnapshotPath);
        Require(runtime.InstanceCount == 0,
            "Unknown and built-in component records must not create project runtime instances.");
        Require(runtime.Diagnostics.Count(value =>
                value.Contains(UnlistedTypeId, StringComparison.Ordinal)
                && value.Contains("no loaded project factory/plugin", StringComparison.Ordinal)) == 1,
            "Every enabled project component type without a loaded factory/plugin needs one diagnostic.");
        Require(!runtime.Diagnostics.Any(value =>
                value.Contains(DisabledUnlistedTypeId, StringComparison.Ordinal)),
            "Disabled project components must not produce missing-factory diagnostics.");
        Require(!runtime.Diagnostics.Any(value =>
                value.Contains(BuiltinComponentIds.Transform, StringComparison.Ordinal)
                && value.Contains("no loaded project factory/plugin", StringComparison.Ordinal)),
            "Engine-owned built-in components must not be diagnosed as missing project factories.");

        runtime.ReloadSnapshot(managedFailureSnapshotPath);
        Require(runtime.InstanceCount == 0,
            "A managed component whose Initialize failed must not remain active.");
        Require(runtime.Diagnostics.Any(value => value.Contains(
                "Managed runtime test component shut down after failed initialization.",
                StringComparison.Ordinal)),
            "Managed Shutdown must execute after Initialize fails.");

        runtime.ReloadSnapshot(nativeFailureSnapshotPath);
        Require(runtime.InstanceCount == 0,
            "A native component whose initial properties failed must not remain active.");
        Require(runtime.Diagnostics.Any(value => value.Contains(
                "Native runtime test component destroyed after set-properties failure.",
                StringComparison.Ordinal)),
            "The native handle must be destroyed when initial property assignment fails.");

        var renderScene = ControllerRenderScene();
        runtime.ReloadSnapshot(snapshotPath, renderScene);
        Require(runtime.InstanceCount == 2, "Both snapshot component instances must be created.");
        var inputState = new RuntimeInputSnapshot(
            7,
            focused: true,
            captured: false,
            new Dictionary<string, RuntimeInputActionState>
            {
                ["move.x"] = new(RuntimeInputActionKind.Axis1D, 0.75f, 0.0f, 1, 0),
            });
        runtime.Update(TimeSpan.FromSeconds(1), TimeSpan.FromSeconds(1.0 / 60.0),
            new Dictionary<string, float> { ["move.x"] = 0.75f },
            inputState);
        var controllerScene = runtime.ApplyTransforms(renderScene);
        Require(Math.Abs(controllerScene.Entities[0].Transform.Position.X - 2.0125f) < 0.0001f
                && Math.Abs(controllerScene.Entities[0].Transform.Position.Y - 3.0f) < 0.0001f,
            "A managed runtime Transform mutation did not reach the disposable render/pick scene.");
        runtime.ResetRuntimeTransforms();
        Require(ReferenceEquals(runtime.ApplyTransforms(renderScene), renderScene),
            "Reset must discard managed runtime Transform overrides and restore authoring-scene identity.");
        Require(runtime.Diagnostics.Any(value =>
                value.Contains("Managed runtime test component initialized", StringComparison.Ordinal)),
            "Managed component initialization did not execute across the isolated assembly boundary.");
        Require(runtime.Diagnostics.Any(value =>
                value.Contains("Managed runtime test component updated", StringComparison.Ordinal)
                && value.Contains("legacy=0.75", StringComparison.Ordinal)
                && value.Contains("full=0.75", StringComparison.Ordinal)
                && value.Contains("revision=7", StringComparison.Ordinal)),
            "Focused Game-view legacy and full-state input did not reach the managed component.");
        Require(runtime.Diagnostics.Any(value =>
                value.Contains("Native runtime test component initialized", StringComparison.Ordinal)),
            "Native component diagnostic callback did not cross the C ABI.");
        var managedStages = new[]
        {
            "Managed runtime test component initialized.",
            "Managed runtime test component enabled.",
            "Managed runtime test component fixed update.",
            "Managed runtime test component updated:",
            "Managed runtime test component late update.",
            "Managed runtime test component render submission.",
        };
        var managedStageIndexes = managedStages
            .Select(stage => runtime.Diagnostics.ToList().FindIndex(value =>
                value.Contains(stage, StringComparison.Ordinal)))
            .ToArray();
        Require(managedStageIndexes.All(index => index >= 0)
                && managedStageIndexes.SequenceEqual(managedStageIndexes.OrderBy(index => index)),
            $"Managed full lifecycle stages did not execute in the required order: {string.Join(",", managedStageIndexes)}. Diagnostics: {string.Join(" | ", runtime.Diagnostics)}");
        var nativeStages = new[]
        {
            "Native runtime test component initialized.",
            "Native runtime test component enabled.",
            "Native runtime test component fixed update.",
            "Native runtime test component variable update.",
            "Native runtime test component late update.",
            "Native runtime test component render submission.",
        };
        var nativeStageIndexes = nativeStages
            .Select(stage => runtime.Diagnostics.ToList().FindIndex(value =>
                value.Contains(stage, StringComparison.Ordinal)))
            .ToArray();
        Require(nativeStageIndexes.All(index => index >= 0)
                && nativeStageIndexes.SequenceEqual(nativeStageIndexes.OrderBy(index => index)),
            "Native v2 full lifecycle stages did not execute in the required order.");

        runtime.Update(
            TimeSpan.FromSeconds(2),
            TimeSpan.FromSeconds(1),
            new Dictionary<string, float>(),
            RuntimeInputSnapshot.Neutral);
        Require(runtime.Diagnostics.Any(value =>
                value.Contains("fixed update dropped", StringComparison.Ordinal)
                && value.Contains("after 4 catch-up steps", StringComparison.Ordinal)),
            "The bounded fixed-update catch-up/drop diagnostic did not execute.");

        var managedRenderCount = runtime.Diagnostics.Count(value =>
            value.Contains("Managed runtime test component render submission.", StringComparison.Ordinal));
        var nativeRenderCount = runtime.Diagnostics.Count(value =>
            value.Contains("Native runtime test component render submission.", StringComparison.Ordinal));
        runtime.ReloadSnapshot(lifecycleFailureSnapshotPath);
        runtime.Update(
            TimeSpan.FromSeconds(3),
            TimeSpan.FromSeconds(1.0 / 60.0),
            new Dictionary<string, float>(),
            RuntimeInputSnapshot.Neutral);
        Require(runtime.Diagnostics.Any(value =>
                value.Contains($"Managed component {ManagedTypeId} late update failed", StringComparison.Ordinal)
                && value.Contains("instance was disabled", StringComparison.Ordinal)),
            "A managed lifecycle-stage exception was not contained and diagnosed.");
        Require(runtime.Diagnostics.Any(value =>
                value.Contains("late update failed and was disabled", StringComparison.Ordinal)
                && value.Contains("injected native late-update failure", StringComparison.Ordinal)),
            "A native lifecycle-stage failure was not contained and diagnosed.");
        Require(runtime.Diagnostics.Count(value =>
                value.Contains("Managed runtime test component render submission.", StringComparison.Ordinal))
                    == managedRenderCount
                && runtime.Diagnostics.Count(value =>
                value.Contains("Native runtime test component render submission.", StringComparison.Ordinal))
                    == nativeRenderCount,
            "A failed lifecycle instance continued into render submission.");
    }
    finally
    {
        runtime.Dispose();
    }
    Require(runtime.Diagnostics.Any(value =>
            value.Contains("Managed runtime test component shut down", StringComparison.Ordinal)),
        "Managed component shutdown did not execute across the isolated assembly boundary.");
    var managedDisabled = runtime.Diagnostics.ToList().FindIndex(value =>
        value.Contains("Managed runtime test component disabled.", StringComparison.Ordinal));
    var managedDestroyed = runtime.Diagnostics.ToList().FindLastIndex(value =>
        value.Contains("Managed runtime test component shut down.", StringComparison.Ordinal));
    Require(managedDisabled >= 0 && managedDestroyed > managedDisabled,
        "Managed disable must precede shutdown/destroy.");
    var nativeDisabled = runtime.Diagnostics.ToList().FindIndex(value =>
        value.Contains("Native runtime test component disabled.", StringComparison.Ordinal));
    var nativeDestroyed = runtime.Diagnostics.ToList().FindLastIndex(value =>
        value.Contains("Native runtime test component destroyed.", StringComparison.Ordinal));
    Require(nativeDisabled >= 0 && nativeDestroyed > nativeDisabled,
        "Native v2 disable must precede destroy.");

    VerifyLegacyNativeV1(fixture, Path.GetFullPath(args[1]));

    Console.WriteLine("Strict contained/hash-bound managed/native component runtime proof passed.");
    return 0;
}
finally
{
    Directory.Delete(root, recursive: true);
}

Fixture CreateFixture(string root, string nativeSource)
{
    var buildHash = new string('1', 64);
    var cacheRoot = Path.Combine(root, ".dragonpixel", "Cache");
    var buildDirectory = Path.Combine(cacheRoot, buildHash,
        $"{CurrentPlatform()}-{CurrentArchitecture()}");
    var managedArtifact = Path.Combine(buildDirectory, "managed", "DragonPixel.ProjectComponents.dll");
    var nativeArtifact = Path.Combine(buildDirectory, "native",
        $"dpe_component_{NativeTypeId.Replace("-", string.Empty, StringComparison.Ordinal)}{Path.GetExtension(nativeSource)}");
    Directory.CreateDirectory(Path.GetDirectoryName(managedArtifact)!);
    Directory.CreateDirectory(Path.GetDirectoryName(nativeArtifact)!);
    File.Copy(Assembly.GetExecutingAssembly().Location, managedArtifact);
    File.Copy(nativeSource, nativeArtifact);
    return new Fixture(
        cacheRoot,
        buildDirectory,
        Path.Combine(cacheRoot, "active-runtime-modules.json"),
        managedArtifact,
        nativeArtifact,
        buildHash,
        Sha256(typeof(IProjectComponent).Assembly.Location));
}

void VerifyRuntimeAssetSnapshot(string root)
{
    const string assetId = "4dc81fa5-c0f7-4dfd-8610-bcaffb053bcf";
    const string entityId = "3e13c184-3570-46aa-a689-4b05d07357f1";
    var png = Convert.FromBase64String(
        "iVBORw0KGgoAAAANSUhEUgAAAAIAAAACCAYAAABytg0kAAAAAXNSR0IArs4c6QAAAARnQU1BAACxjwv8YQUAAAAJcEhZcwAADsMAAA7DAcdvqGQAAAAQSURBVBhXY2D4//8/GMMYAGWsC/VzMJBzAAAAAElFTkSuQmCC");
    var hash = Convert.ToHexString(SHA256.HashData(png)).ToLowerInvariant();
    JsonObject Snapshot(string contentHash) => new()
    {
        ["$schema"] = "https://dragonpixel.dev/schemas/v3/scene.schema.json",
        ["format"] = "dpe.scene",
        ["formatVersion"] = 3,
        ["engineVersion"] = "1.0.0-test",
        ["sceneId"] = "27ed345d-b487-41c1-918b-fbf279be8e28",
        ["name"] = "Runtime Asset Test",
        ["snapshotRevision"] = 7,
        ["physicsSettings"] = new JsonObject
        {
            ["fixedTimeStepSeconds"] = 1.0 / 60.0,
            ["maxCatchUpTicks"] = 4,
            ["box2DSolverSubsteps"] = 4,
            ["joltCollisionSteps"] = 1,
            ["gravity2D"] = new JsonObject { ["x"] = 0.0, ["y"] = -9.81 },
            ["gravity3D"] = new JsonObject { ["x"] = 0.0, ["y"] = -9.81, ["z"] = 0.0 },
        },
        ["assets"] = new JsonArray
        {
            new JsonObject
            {
                ["assetId"] = assetId,
                ["assetType"] = "sprite",
                ["mediaType"] = "image/png",
                ["contentHash"] = contentHash,
                ["embeddedBytesBase64"] = Convert.ToBase64String(png),
            },
        },
        ["entities"] = new JsonArray
        {
            new JsonObject
            {
                ["id"] = entityId,
                ["name"] = "Bound Sprite",
                ["enabled"] = true,
                ["components"] = new JsonArray
                {
                    new JsonObject
                    {
                        ["typeId"] = BuiltinComponentIds.Sprite,
                        ["enabled"] = true,
                        ["properties"] = new JsonObject
                        {
                            ["dpe.sprite.asset"] = assetId,
                        },
                    },
                },
            },
        },
    };

    var path = Path.Combine(root, "runtime-asset-snapshot.json");
    File.WriteAllText(path, Snapshot(hash).ToJsonString());
    var parsed = SceneSnapshotParser.Parse(path, 0);
    Require(parsed.Scene.Assets.TryGetValue(assetId, out var binding),
        "A valid immutable sprite binding was not accepted by the worker snapshot parser.");
    Require(binding!.ImmutableBytes.SequenceEqual(png) && binding.ContentHash == hash,
        "The worker snapshot parser changed immutable runtime asset bytes or their hash.");
    Require(parsed.Scene.Entities.Single().Sprite?.AssetId == assetId,
        "The parsed Sprite did not retain its stable runtime asset binding.");

    File.WriteAllText(path, Snapshot(new string('0', 64)).ToJsonString());
    var rejected = SceneSnapshotParser.Parse(path, 0);
    Require(!rejected.Scene.Assets.ContainsKey(assetId)
            && rejected.Scene.Diagnostics.Any(value => value.Contains(
                "failed content-hash validation", StringComparison.Ordinal)),
        "A tampered runtime asset binding was not rejected before framework decode.");
}

void VerifyCustomTileExtensionSnapshot(string root, ProjectComponentRuntime runtime)
{
    const string mapId = "fd2f3574-8e6f-43d1-bc96-c6650ab49a54";
    const string layerId = "59737391-9417-45bf-a8af-cb6e24e7aa38";
    const string setId = "4fe655df-c40f-4e48-a5cc-fbe9bd356ac6";
    const string tileId = "a9ba355a-51e8-49e9-b581-c6174026c160";
    const string textureId = "dd02cd2a-8a7e-4b27-9d26-06331093885a";
    const string entityId = "4b78270f-fabc-47bc-9697-f9c322769a98";
    var snapshot = new JsonObject
    {
        ["$schema"] = "https://dragonpixel.dev/schemas/v3/scene.schema.json",
        ["format"] = "dpe.scene",
        ["formatVersion"] = 3,
        ["engineVersion"] = "1.0.0-test",
        ["snapshotFormatVersion"] = 5,
        ["sceneId"] = "27ed345d-b487-41c1-918b-fbf279be8e28",
        ["name"] = "Custom Tile Extension",
        ["snapshotRevision"] = 9,
        ["tileSets"] = new JsonArray
        {
            new JsonObject
            {
                ["assetId"] = setId,
                ["pixelsPerUnit"] = 16.0,
                ["textureAssetId"] = textureId,
                ["cellSize"] = new JsonObject { ["x"] = 16, ["y"] = 16 },
                ["tiles"] = new JsonArray
                {
                    new JsonObject
                    {
                        ["tileId"] = tileId,
                        ["kind"] = "custom",
                        ["textureAssetId"] = textureId,
                        ["source"] = new JsonObject
                            { ["x"] = 0, ["y"] = 0, ["width"] = 16, ["height"] = 16 },
                        ["custom"] = new JsonObject
                            { ["typeId"] = TileExtensionId, ["typeVersion"] = 1,
                              ["payload"] = new JsonObject() },
                    },
                },
            },
        },
        ["tilemaps"] = new JsonArray
        {
            new JsonObject
            {
                ["assetId"] = mapId,
                ["tileSetDependencies"] = new JsonArray(setId),
                ["grid"] = new JsonObject
                {
                    ["layout"] = "rectangular",
                    ["cellSize"] = new JsonObject { ["x"] = 1.0, ["y"] = 1.0 },
                },
                ["layers"] = new JsonArray
                {
                    new JsonObject
                    {
                        ["layerId"] = layerId,
                        ["name"] = "Ground",
                        ["cells"] = new JsonArray
                        {
                            new JsonObject
                            {
                                ["x"] = 2, ["y"] = -3,
                                ["tileSetId"] = setId, ["tileId"] = tileId,
                                ["resolvedTileSetId"] = setId, ["resolvedTileId"] = tileId,
                                ["placeholder"] = true,
                            },
                        },
                    },
                },
            },
        },
        ["entities"] = new JsonArray
        {
            new JsonObject
            {
                ["id"] = entityId, ["name"] = "Tilemap", ["enabled"] = true,
                ["components"] = new JsonArray
                {
                    new JsonObject
                    {
                        ["typeId"] = BuiltinComponentIds.Tilemap2D,
                        ["enabled"] = true,
                        ["properties"] = new JsonObject { ["dpe.tilemap.asset"] = mapId },
                    },
                },
            },
        },
    };
    var path = Path.Combine(root, "custom-tile-extension-snapshot.json");
    File.WriteAllText(path, snapshot.ToJsonString());
    var parsed = SceneSnapshotParser.Parse(path, 0, runtime);
    var cell = parsed.Scene.Entities.Single().Tilemap!.Layers.Single().Cells.Single();
    Require(Math.Abs(cell.Color.R - 0.25f) < 0.0001f
            && Math.Abs(cell.Color.G - 0.5f) < 0.0001f
            && Math.Abs(cell.Color.B - 0.75f) < 0.0001f,
        "Worker custom-tile evaluation did not replace the lossless placeholder tint.");
}

JsonObject CreateValidManifest(Fixture fixture) => new()
{
    ["format"] = "dpe.runtime-modules",
    ["formatVersion"] = 2,
    ["buildHash"] = fixture.BuildHash,
    ["platform"] = CurrentPlatform(),
    ["architecture"] = CurrentArchitecture(),
    ["configuration"] = "Release",
    ["generatorIdentity"] = "dpe-component-generator-v2",
    ["contractsSha256"] = fixture.ContractsSha256,
    ["componentRoots"] = new JsonArray("Authoring Components"),
    ["toolIdentities"] = new JsonObject
    {
        ["dotnet"] = new string('2', 64),
        ["cmake"] = new string('3', 64),
        ["cxx"] = new string('4', 64),
    },
    ["managedModules"] = new JsonArray
    {
        new JsonObject
        {
            ["path"] = fixture.ManagedArtifact,
            ["sha256"] = Sha256(fixture.ManagedArtifact),
            ["components"] = new JsonArray
            {
                new JsonObject
                {
                    ["typeId"] = ManagedTypeId,
                    ["moduleId"] = ManagedModuleId,
                    ["sourcePath"] = "Authoring Components/CSharp/ManagedTestComponent.cs",
                },
            },
        },
    },
    ["nativeModules"] = new JsonArray
    {
        new JsonObject
        {
            ["path"] = fixture.NativeArtifact,
            ["sha256"] = Sha256(fixture.NativeArtifact),
            ["component"] = new JsonObject
            {
                ["typeId"] = NativeTypeId,
                ["moduleId"] = NativeModuleId,
                ["sourcePath"] = "Authoring Components/Cpp/NativeTestComponent.cpp",
            },
            ["tileExtension"] = new JsonObject
            {
                ["pluginId"] = TileExtensionId,
                ["capabilities"] = new JsonArray("evaluate-tiles", "propose-brush"),
            },
        },
    },
};

void VerifyLegacyNativeV1(Fixture fixture, string sourcePlugin)
{
    var artifact = Path.Combine(
        fixture.BuildDirectory,
        "native",
        $"dpe_component_{LegacyNativeTypeId.Replace("-", string.Empty, StringComparison.Ordinal)}{Path.GetExtension(sourcePlugin)}");
    File.Copy(sourcePlugin, artifact, overwrite: true);
    var manifest = CreateValidManifest(fixture);
    manifest["managedModules"] = new JsonArray();
    manifest["nativeModules"] = new JsonArray
    {
        new JsonObject
        {
            ["path"] = artifact,
            ["sha256"] = Sha256(artifact),
            ["component"] = new JsonObject
            {
                ["typeId"] = LegacyNativeTypeId,
                ["moduleId"] = LegacyNativeModuleId,
                ["sourcePath"] = "Authoring Components/Cpp/LegacyNativeV1.cpp",
            },
        },
    };
    WriteManifest(fixture.ManifestPath, manifest);
    var snapshot = Path.Combine(fixture.BuildDirectory, "legacy-v1-snapshot.json");
    WriteSnapshot(snapshot,
        SnapshotComponent(LegacyNativeTypeId, enabled: true, new JsonObject()));

    var runtime = new ProjectComponentRuntime(fixture.ManifestPath);
    try
    {
        Require(runtime.FactoryCount == 1,
            "The legacy native v1 fallback plugin must remain loadable.");
        runtime.ReloadSnapshot(snapshot);
        Require(runtime.InstanceCount == 1,
            "The legacy native v1 fallback component must instantiate.");
        runtime.Update(
            TimeSpan.FromSeconds(1),
            TimeSpan.FromSeconds(1.0 / 60.0),
            new Dictionary<string, float>(),
            RuntimeInputSnapshot.Neutral);
        Require(runtime.Diagnostics.Any(value =>
                value.Contains("Legacy native v1 component initialized.", StringComparison.Ordinal))
                && runtime.Diagnostics.Any(value =>
                    value.Contains("Legacy native v1 component updated.", StringComparison.Ordinal)),
            "The legacy native v1 create/update compatibility mapping did not execute.");
        Require(!runtime.Diagnostics.Any(value =>
                value.Contains("Legacy native v1 component enabled.", StringComparison.Ordinal)),
            "A legacy v1 module must not be treated as a v2 lifecycle table.");
    }
    finally
    {
        runtime.Dispose();
    }
    Require(runtime.Diagnostics.Any(value =>
            value.Contains("Legacy native v1 component destroyed.", StringComparison.Ordinal)),
        "The legacy native v1 destroy compatibility mapping did not execute.");
}

void ExpectRejected(Fixture fixture, Action<JsonObject> mutate, string message)
{
    var manifest = CreateValidManifest(fixture);
    mutate(manifest);
    WriteManifest(fixture.ManifestPath, manifest);
    using var runtime = new ProjectComponentRuntime(fixture.ManifestPath);
    Require(runtime.FactoryCount == 0
        && runtime.Diagnostics.Any(value =>
            value.Contains("authoring records remain preserved", StringComparison.Ordinal)), message);
}

void VerifyImmutableInputContracts()
{
    var originalState = new RuntimeInputActionState(
        RuntimeInputActionKind.Axis1D, 0.25f, 0.0f, 1, 0);
    var replacementState = new RuntimeInputActionState(
        RuntimeInputActionKind.Axis1D, -1.0f, 0.0f, 0, 1);
    var sourceActions = new Dictionary<string, RuntimeInputActionState>(StringComparer.Ordinal)
    {
        ["move.x"] = originalState,
    };
    var snapshot = new RuntimeInputSnapshot(11, true, true, sourceActions);
    sourceActions["move.x"] = replacementState;
    Require(ReferenceEquals(snapshot.Actions["move.x"], originalState),
        "RuntimeInputSnapshot.Actions must be copy-backed against source mutations.");
    Require(MutationIsRejected(snapshot.Actions, "move.x", replacementState),
        "RuntimeInputSnapshot.Actions must reject dictionary-interface mutations.");

    var sourceLegacyActions = new Dictionary<string, float>(StringComparer.Ordinal)
    {
        ["move.x"] = 0.25f,
    };
    var update = new ProjectComponentUpdate(1.0, 1.0 / 60.0, sourceLegacyActions, snapshot);
    sourceLegacyActions["move.x"] = -1.0f;
    Require(update.InputActions["move.x"] == 0.25f,
        "ProjectComponentUpdate.InputActions must be copy-backed against source mutations.");
    Require(MutationIsRejected(update.InputActions, "move.x", -1.0f),
        "ProjectComponentUpdate.InputActions must reject dictionary-interface mutations.");
}

bool MutationIsRejected<T>(IReadOnlyDictionary<string, T> values, string key, T replacement)
{
    if (values is not IDictionary<string, T> mutable) return true;
    try
    {
        mutable[key] = replacement;
        return false;
    }
    catch (NotSupportedException)
    {
        return true;
    }
}

void TestServiceWriterManifestPlacements(Fixture fixture)
{
    var buildManifestPath = Path.Combine(fixture.BuildDirectory, "runtime-modules.json");
    WriteManifest(buildManifestPath, CreateValidManifest(fixture));
    using (var buildRuntime = new ProjectComponentRuntime(buildManifestPath))
    {
        Require(buildRuntime.FactoryCount == 2,
            "The service-writer build-cache runtime manifest placement must be accepted.");
    }

    var misplacedManifestPath = Path.Combine(
        fixture.CacheRoot, fixture.BuildHash, "runtime-modules.json");
    WriteManifest(misplacedManifestPath, CreateValidManifest(fixture));
    using var misplacedRuntime = new ProjectComponentRuntime(misplacedManifestPath);
    Require(misplacedRuntime.FactoryCount == 0
        && misplacedRuntime.Diagnostics.Any(value =>
            value.Contains("authoring records remain preserved", StringComparison.Ordinal)),
        "A runtime manifest outside the service writer's active/build-cache placements must be rejected.");
}

void WriteSnapshot(string path, params JsonObject[] components)
{
    var componentArray = new JsonArray();
    foreach (var component in components) componentArray.Add(component);
    var rootNode = new JsonObject
    {
        ["entities"] = new JsonArray
        {
            new JsonObject
            {
                ["id"] = TestEntityId,
                ["components"] = componentArray,
            },
        },
    };
    File.WriteAllText(path, rootNode.ToJsonString());
}

RenderScene ControllerRenderScene() => new(
    "2bbc7a93-41b7-4fe2-a73f-72a6f922da39",
    "Controller runtime scene",
    17,
    new[]
    {
        new RenderEntity(
            TestEntityId,
            "Controller target",
            null,
            true,
            new RenderTransform(
                new RenderVector3(2.0f, 3.0f, 0.0f),
                RenderQuaternion.Identity,
                RenderVector3.One),
            null,
            null,
            null,
            null,
            null,
            Array.Empty<RenderCollider>(),
            null,
            0.0f),
    },
    new Dictionary<string, RenderAsset>(StringComparer.Ordinal),
    Array.Empty<string>());

void VerifyGameObjectControllerContract()
{
    var transform = new DragonPixel.Contracts.Transform(
        new NumericsVector3(4.0f, 5.0f, 6.0f),
        NumericsVector3.Zero,
        NumericsVector3.One);
    var controller = new ControllerContractProbe();
    var component = (IProjectComponent)controller;
    var lifecycle = (IProjectComponentLifecycle)controller;
    component.Initialize(new ProjectComponentContext(
        TestEntityId,
        ManagedTypeId,
        static (_, _) => { },
        transform), "{\"speed\":3}");
    Require(controller.Id == Guid.Parse(TestEntityId)
            && ReferenceEquals(controller.Transform, transform)
            && controller.PropertiesCount == 1,
        "GameObjectController did not bind its stable GUID, shared Transform, and properties.");

    var inputState = new RuntimeInputSnapshot(
        19,
        focused: true,
        captured: true,
        new Dictionary<string, RuntimeInputActionState>
        {
            ["move.x"] = new(RuntimeInputActionKind.Axis1D, 1.0f, 0.0f, 1, 0),
        });
    var update = new ProjectComponentUpdate(
        2.0,
        0.25,
        new Dictionary<string, float> { ["move.x"] = 1.0f },
        inputState);
    lifecycle.OnEnable();
    lifecycle.FixedUpdate(update);
    component.Update(update);
    lifecycle.OnDisable();

    Require(controller.EnabledCount == 1
            && controller.FixedUpdateCount == 1
            && controller.UpdateCount == 1
            && controller.DisabledCount == 1
            && !controller.IsEnabled,
        "GameObjectController lifespan dispatch did not reach the concise overrides.");
    Require(controller.Input.Focused
            && controller.Input.Captured
            && controller.LastMoveX == 1.0f
            && Math.Abs(controller.DeltaTime - 0.25f) < 0.0001f
            && Math.Abs(controller.FixedDeltaTime - 0.25f) < 0.0001f,
        "GameObjectController did not expose current input and timing state.");
    Require(controller.Transform.Position == new NumericsVector3(4.25f, 5.0f, 6.0f),
        "GameObjectController Update did not mutate its shared Transform.");

    component.Shutdown();
    Require(controller.Id == Guid.Empty && controller.Transform.Revision == 0,
        "GameObjectController shutdown did not unbind disposable runtime identity/state.");
}

JsonObject SnapshotComponent(string typeId, bool enabled, JsonObject properties) => new()
{
    ["typeId"] = typeId,
    ["enabled"] = enabled,
    ["properties"] = properties,
};

void TestLinkedArtifactWhenSupported(Fixture fixture)
{
    var linkPath = Path.Combine(fixture.BuildDirectory, "managed", "linked.dll");
    try
    {
        File.CreateSymbolicLink(linkPath, fixture.ManagedArtifact);
    }
    catch (Exception exception) when (exception is UnauthorizedAccessException
        or PlatformNotSupportedException or IOException)
    {
        Console.WriteLine($"Symbolic-link artifact test skipped: {exception.GetType().Name}.");
        return;
    }
    try
    {
        ExpectRejected(fixture, manifest =>
        {
            ManagedModule(manifest)["path"] = linkPath;
            ManagedModule(manifest)["sha256"] = Sha256(linkPath);
        }, "Linked/reparse-point artifacts must be rejected even when their hash matches.");
    }
    finally
    {
        File.Delete(linkPath);
    }
}

void TestLinkedManifestWhenSupported(Fixture fixture)
{
    var sourcePath = Path.Combine(fixture.CacheRoot, "regular-runtime-modules.json");
    WriteManifest(sourcePath, CreateValidManifest(fixture));
    if (File.Exists(fixture.ManifestPath)) File.Delete(fixture.ManifestPath);
    try
    {
        File.CreateSymbolicLink(fixture.ManifestPath, sourcePath);
    }
    catch (Exception exception) when (exception is UnauthorizedAccessException
        or PlatformNotSupportedException or IOException)
    {
        Console.WriteLine($"Symbolic-link manifest test skipped: {exception.GetType().Name}.");
        return;
    }
    try
    {
        using var runtime = new ProjectComponentRuntime(fixture.ManifestPath);
        Require(runtime.FactoryCount == 0
            && runtime.Diagnostics.Any(value =>
                value.Contains("authoring records remain preserved", StringComparison.Ordinal)),
            "A linked/reparse-point manifest must be rejected.");
    }
    finally
    {
        File.Delete(fixture.ManifestPath);
        File.Delete(sourcePath);
    }
}

JsonObject ManagedModule(JsonObject root) =>
    (JsonObject)((JsonArray)root["managedModules"]!)[0]!;

JsonObject ManagedComponent(JsonObject root) =>
    (JsonObject)((JsonArray)ManagedModule(root)["components"]!)[0]!;

JsonObject NativeComponent(JsonObject root) =>
    (JsonObject)((JsonObject)((JsonArray)root["nativeModules"]!)[0]!)["component"]!;

void WriteManifest(string path, JsonObject manifest)
{
    Directory.CreateDirectory(Path.GetDirectoryName(path)!);
    File.WriteAllText(path, manifest.ToJsonString(new JsonSerializerOptions { WriteIndented = true }));
}

string Sha256(string path)
{
    using var stream = File.OpenRead(path);
    return Convert.ToHexString(SHA256.HashData(stream)).ToLowerInvariant();
}

string CurrentPlatform() => OperatingSystem.IsWindows() ? "windows"
    : OperatingSystem.IsMacOS() ? "macos" : "linux";

string CurrentArchitecture() => RuntimeInformation.ProcessArchitecture switch
{
    Architecture.X64 => "x64",
    Architecture.Arm64 => "arm64",
    _ => RuntimeInformation.ProcessArchitecture.ToString().ToLowerInvariant(),
};

string OtherPlatform() => CurrentPlatform() == "windows" ? "linux" : "windows";
string OtherArchitecture() => CurrentArchitecture() == "x64" ? "arm64" : "x64";

void Require(bool condition, string message)
{
    if (!condition) throw new InvalidOperationException(message);
}

internal sealed record Fixture(
    string CacheRoot,
    string BuildDirectory,
    string ManifestPath,
    string ManagedArtifact,
    string NativeArtifact,
    string BuildHash,
    string ContractsSha256);

public static class ManagedRuntimeProbe
{
    public static int InitializeCount;
    public static int UpdateCount;
    public static int ShutdownCount;
    public static float LastMoveX;

    public static void Reset()
    {
        InitializeCount = 0;
        UpdateCount = 0;
        ShutdownCount = 0;
        LastMoveX = 0.0f;
    }
}

public sealed class ManagedTestComponent : IProjectComponent, IProjectComponentLifecycle
{
    private ProjectComponentContext? _context;
    private bool _failedInitialization;
    private bool _failLateUpdate;

    public void Initialize(ProjectComponentContext context, string propertiesJson)
    {
        _context = context;
        ManagedRuntimeProbe.InitializeCount++;
        using var properties = JsonDocument.Parse(propertiesJson);
        if (properties.RootElement.TryGetProperty("failInitialize", out var failInitialize)
            && failInitialize.ValueKind == JsonValueKind.True)
        {
            _failedInitialization = true;
            context.ReportDiagnostic(ProjectComponentDiagnosticSeverity.Info,
                "Managed runtime test component initialization failure injected.");
            throw new InvalidOperationException("injected managed initialization failure");
        }
        _failLateUpdate = properties.RootElement.TryGetProperty(
                "failLateUpdate", out var failLateUpdate)
            && failLateUpdate.ValueKind == JsonValueKind.True;
        context.ReportDiagnostic(ProjectComponentDiagnosticSeverity.Info,
            "Managed runtime test component initialized.");
    }

    public void PropertiesChanged(string propertiesJson) { }

    public void Update(ProjectComponentUpdate update)
    {
        ManagedRuntimeProbe.UpdateCount++;
        ManagedRuntimeProbe.LastMoveX = update.InputActions.TryGetValue("move.x", out var value)
            ? value : 0.0f;
        _context?.Transform.Translate(new NumericsVector3(
            ManagedRuntimeProbe.LastMoveX * (float)update.DeltaSeconds,
            0.0f,
            0.0f));
        var fullStateMoveX = update.InputState.Actions.TryGetValue("move.x", out var state)
            ? state.Value : 0.0f;
        _context?.ReportDiagnostic(ProjectComponentDiagnosticSeverity.Info,
            FormattableString.Invariant(
                $"Managed runtime test component updated: legacy={ManagedRuntimeProbe.LastMoveX}; full={fullStateMoveX}; revision={update.InputState.Revision}."));
    }

    public void OnEnable() => _context?.ReportDiagnostic(
        ProjectComponentDiagnosticSeverity.Info,
        "Managed runtime test component enabled.");

    public void FixedUpdate(ProjectComponentUpdate update) => _context?.ReportDiagnostic(
        ProjectComponentDiagnosticSeverity.Info,
        "Managed runtime test component fixed update.");

    public void LateUpdate(ProjectComponentUpdate update)
    {
        if (_failLateUpdate)
            throw new InvalidOperationException("injected managed late-update failure");
        _context?.ReportDiagnostic(
            ProjectComponentDiagnosticSeverity.Info,
            "Managed runtime test component late update.");
    }

    public void SubmitRender(ProjectComponentUpdate update) => _context?.ReportDiagnostic(
        ProjectComponentDiagnosticSeverity.Info,
        "Managed runtime test component render submission.");

    public void OnDisable() => _context?.ReportDiagnostic(
        ProjectComponentDiagnosticSeverity.Info,
        "Managed runtime test component disabled.");

    public void Shutdown()
    {
        ManagedRuntimeProbe.ShutdownCount++;
        _context?.ReportDiagnostic(ProjectComponentDiagnosticSeverity.Info,
            _failedInitialization
                ? "Managed runtime test component shut down after failed initialization."
                : "Managed runtime test component shut down.");
    }
}

public sealed class ControllerContractProbe : GameObjectController
{
    public int EnabledCount { get; private set; }
    public int DisabledCount { get; private set; }
    public int UpdateCount { get; private set; }
    public int FixedUpdateCount { get; private set; }
    public int PropertiesCount { get; private set; }
    public float LastMoveX { get; private set; }

    public override void Enabled() => EnabledCount++;
    public override void Disabled() => DisabledCount++;

    public override void Update()
    {
        UpdateCount++;
        LastMoveX = Input.GetAction("move.x");
        Transform.Translate(new NumericsVector3(LastMoveX * DeltaTime, 0.0f, 0.0f));
    }

    public override void FixedUpdate() => FixedUpdateCount++;
    public override void PropertiesChanged(string propertiesJson) => PropertiesCount++;
}

public sealed class ManagedTestComponentFactory : IProjectComponentFactory
{
    public string TypeId => "58f2e185-f112-442b-a56d-f6d113426081";
    public IProjectComponent Create() => new ManagedTestComponent();
}
