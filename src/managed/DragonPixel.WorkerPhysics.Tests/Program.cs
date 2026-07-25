using System.Buffers.Binary;
using System.Diagnostics;
using System.Reflection;
using System.Security.Cryptography;
using System.Text.Json;
using System.Text.Json.Nodes;
using DragonPixel.Runtime;

namespace DragonPixel.WorkerPhysics.Tests;

internal static class Program
{
    private const string FallingEntityId = "11111111-1111-4111-8111-111111111111";
    private const string GroundEntityId = "22222222-2222-4222-8222-222222222222";
    private const string SampleSpriteId = "e75d033b-bba0-4f8f-8f54-c7a1e6990aef";
    private const string SampleCubeId = "3466ea8a-d7d4-458c-83ae-cc33291e5c26";
    private const string TransformType = "52e52fbd-ea15-40c5-bd9a-7dd320f7cd1e";
    private const string MeshType = "9be44558-78e9-4912-bee5-046b5ad0a410";
    private const string RigidBody3DType = "1025c21c-34a8-4f64-977b-453d81e402c5";
    private const string BoxCollider3DType = "bfc9ff93-a892-4c1f-ad8f-f13d8bc1ba38";

    private static async Task<int> Main(string[] args)
    {
        if (args.Length > 0 && args[0] == "--worker-child")
        {
            Console.SetOut(Console.Error);
            return await WorkerHost.RunAsync(new TestAdapter(), args[1..]);
        }

        try
        {
            if (args.Length is not (2 or 3))
            {
                throw new ArgumentException(
                    "Usage: worker physics tests <native C ABI library> <temporary directory> "
                    + "[tracked sample scene]");
            }
            var nativeLibrary = Path.GetFullPath(args[0]);
            var temporaryDirectory = Path.GetFullPath(args[1]);
            Directory.CreateDirectory(temporaryDirectory);
            var revisionOne = WriteScene(
                Path.Combine(temporaryDirectory, "physics-r1.dpescene"),
                snapshotRevision: 1,
                fallingHeight: 4,
                validSettings: true);
            var revisionTwo = WriteScene(
                Path.Combine(temporaryDirectory, "physics-r2.dpescene"),
                snapshotRevision: 2,
                fallingHeight: 6,
                validSettings: true);
            var invalid = WriteScene(
                Path.Combine(temporaryDirectory, "physics-invalid.dpescene"),
                snapshotRevision: 2,
                fallingHeight: 9,
                validSettings: false);

            await VerifyPreviewLifecycleAsync(nativeLibrary, temporaryDirectory, revisionOne);
            await VerifyPlayLifecycleAsync(
                nativeLibrary,
                temporaryDirectory,
                revisionOne,
                revisionTwo,
                invalid);
            await VerifyCrashCleanupAsync(nativeLibrary, temporaryDirectory, revisionOne);
            if (args.Length == 3)
            {
                await VerifyTrackedSampleAsync(
                    nativeLibrary,
                    temporaryDirectory,
                    new SceneFile(Path.GetFullPath(args[2]), Hash(Path.GetFullPath(args[2]))));
            }
            Console.WriteLine("Dragon Pixel worker physics lifecycle POC G passed.");
            return 0;
        }
        catch (Exception exception)
        {
            Console.Error.WriteLine(exception);
            return 1;
        }
    }

    private static async Task VerifyPreviewLifecycleAsync(
        string nativeLibrary,
        string temporaryDirectory,
        SceneFile scene)
    {
        using var worker = StartWorker(
            nativeLibrary,
            Path.Combine(temporaryDirectory, "preview.frame"),
            "preview");
        var handshake = await worker.CallAsync(
            "handshake",
            new JsonObject { ["protocolVersion"] = 2 });
        Assert(handshake["nativePhysics"]!.GetValue<bool>(), "Preview did not negotiate native physics.");
        Assert(handshake["simulatePreview"]!.GetValue<bool>(), "Preview simulation was not negotiated.");
        await worker.CallAsync("initialize");
        var loaded = await LoadAsync(worker, scene, 1, reload: false);
        Assert(loaded["physicsBodies"]!.GetValue<int>() == 2, "Preview loaded the wrong body count.");
        Assert(loaded["physicsWorldLoaded"]!.GetValue<bool>(), "Preview did not build an isolated physics world.");
        await worker.CallAsync("play");
        await Task.Delay(250);

        var edit = await worker.CallAsync("inspect");
        var editPhysics = edit["physics"]!.AsObject();
        Assert(editPhysics["simulationMode"]!.GetValue<string>() == "edit", "Preview did not remain in Edit mode.");
        Assert(editPhysics["worldTick"]!.GetValue<long>() == 0, "Ordinary Edit preview advanced physics.");
        AssertNear(ReadEntityY(editPhysics, FallingEntityId), 4, 0.001, "Edit preview changed authoring position.");

        var simulation = await worker.CallAsync(
            "simulatePreview",
            new JsonObject { ["enabled"] = true });
        Assert(simulation["enabled"]!.GetValue<bool>(), "Simulate Preview did not start.");
        var simulated = await WaitForPhysicsAsync(
            worker,
            static physics => physics["worldTick"]!.GetValue<long>() >= 8
                && ReadEntityY(physics, FallingEntityId) < 3.9,
            TimeSpan.FromSeconds(3));
        Assert(simulated["simulationMode"]!.GetValue<string>() == "simulate-preview",
            "Preview diagnostics did not identify simulation mode.");

        await worker.CallAsync("pause");
        var paused = (await worker.CallAsync("inspect"))["physics"]!.AsObject();
        var pausedTick = paused["worldTick"]!.GetValue<long>();
        await Task.Delay(250);
        var stillPaused = (await worker.CallAsync("inspect"))["physics"]!.AsObject();
        Assert(stillPaused["worldTick"]!.GetValue<long>() == pausedTick, "Pause advanced preview physics.");

        await worker.CallAsync("resume");
        await WaitForPhysicsAsync(
            worker,
            physics => physics["worldTick"]!.GetValue<long>() > pausedTick,
            TimeSpan.FromSeconds(2));

        await worker.CallAsync(
            "simulate-preview",
            new JsonObject { ["enabled"] = false });
        var restored = (await worker.CallAsync("inspect"))["physics"]!.AsObject();
        Assert(restored["worldTick"]!.GetValue<long>() == 0, "Stopping preview simulation retained runtime ticks.");
        AssertNear(ReadEntityY(restored, FallingEntityId), 4, 0.001,
            "Stopping preview simulation did not restore authoring transforms.");

        await worker.CallAsync("stop");
        var stopped = (await worker.CallAsync("inspect"))["physics"]!.AsObject();
        Assert(!stopped["worldLoaded"]!.GetValue<bool>(), "Stop did not destroy the preview physics world.");
        Assert(stopped["runtimeTransforms"]!.AsArray().Count == 0, "Stop retained runtime transform mirrors.");

        await worker.CallAsync("play");
        await Task.Delay(200);
        var restartedEdit = (await worker.CallAsync("inspect"))["physics"]!.AsObject();
        Assert(restartedEdit["worldLoaded"]!.GetValue<bool>(), "Preview did not rebuild after Stop.");
        Assert(restartedEdit["worldTick"]!.GetValue<long>() == 0, "Restarted Edit preview simulated without consent.");
        AssertNear(ReadEntityY(restartedEdit, FallingEntityId), 4, 0.001,
            "Restarted Edit preview did not use authoring state.");
        await worker.CallAsync("shutdown");
        worker.WaitForExit();
        AssertFileUnchanged(scene);
    }

    private static async Task VerifyPlayLifecycleAsync(
        string nativeLibrary,
        string temporaryDirectory,
        SceneFile first,
        SceneFile second,
        SceneFile invalid)
    {
        using var worker = StartWorker(
            nativeLibrary,
            Path.Combine(temporaryDirectory, "play.frame"),
            "play");
        await worker.CallAsync("handshake", new JsonObject { ["protocolVersion"] = 2 });
        await worker.CallAsync("initialize");
        await LoadAsync(worker, first, 1, reload: false);
        var simulateError = await worker.CallExpectErrorAsync(
            "simulatePreview",
            new JsonObject { ["enabled"] = true });
        Assert(simulateError["code"]!.GetValue<int>() == -32602,
            "A play worker accepted the preview-only simulation command.");

        await worker.CallAsync("play");
        var running = await WaitForPhysicsAsync(
            worker,
            static physics => physics["worldTick"]!.GetValue<long>() >= 8
                && ReadEntityY(physics, FallingEntityId) < 3.9,
            TimeSpan.FromSeconds(3));
        Assert(running["simulationMode"]!.GetValue<string>() == "play",
            "Play diagnostics did not identify play simulation.");

        var invalidReload = await worker.CallExpectErrorAsync(
            "reloadSnapshot",
            SnapshotParameters(invalid.Path, 2));
        Assert(invalidReload["code"]!.GetValue<int>() == -32002,
            "Invalid physics reload returned the wrong structured error.");
        var afterInvalid = await worker.CallAsync("inspect");
        Assert(afterInvalid["snapshotRevision"]!.GetValue<long>() == 1,
            "Invalid reload replaced the active scene candidate.");
        Assert(afterInvalid["physics"]!["worldLoaded"]!.GetValue<bool>(),
            "Invalid reload destroyed the active physics world.");

        await LoadAsync(worker, second, 2, reload: true);
        var reloaded = await WaitForPhysicsAsync(
            worker,
            static physics => physics["worldTick"]!.GetValue<long>() >= 1
                && ReadEntityY(physics, FallingEntityId) > 5.0,
            TimeSpan.FromSeconds(2));
        Assert(reloaded["bodyCount"]!.GetValue<int>() == 2, "Reload changed the physics body set.");

        await worker.CallAsync("stop");
        var stopped = (await worker.CallAsync("inspect"))["physics"]!.AsObject();
        Assert(!stopped["worldLoaded"]!.GetValue<bool>(), "Play Stop did not destroy its physics world.");
        await worker.CallAsync("play");
        var replayed = await WaitForPhysicsAsync(
            worker,
            static physics => physics["worldTick"]!.GetValue<long>() >= 2
                && ReadEntityY(physics, FallingEntityId) < 6.0,
            TimeSpan.FromSeconds(2));
        Assert(replayed["worldLoaded"]!.GetValue<bool>(), "Play did not rebuild after Stop.");
        await worker.CallAsync("shutdown");
        worker.WaitForExit();
        AssertFileUnchanged(first);
        AssertFileUnchanged(second);
        AssertFileUnchanged(invalid);
    }

    private static async Task VerifyCrashCleanupAsync(
        string nativeLibrary,
        string temporaryDirectory,
        SceneFile scene)
    {
        using (var crashing = StartWorker(
                   nativeLibrary,
                   Path.Combine(temporaryDirectory, "crash.frame"),
                   "play"))
        {
            await crashing.CallAsync("handshake", new JsonObject { ["protocolVersion"] = 2 });
            await crashing.CallAsync("initialize");
            await LoadAsync(crashing, scene, 1, reload: false);
            await crashing.CallAsync("play");
            await WaitForPhysicsAsync(
                crashing,
                static physics => physics["worldTick"]!.GetValue<long>() >= 2,
                TimeSpan.FromSeconds(2));
            await crashing.CallAsync("crash");
            crashing.WaitForExit();
            var crashError = crashing.ReadStandardError();
            Assert(
                crashing.ExitCode == 86,
                $"Forced worker crash returned exit code {crashing.ExitCode} instead of 86. {crashError}");
        }

        using var recovered = StartWorker(
            nativeLibrary,
            Path.Combine(temporaryDirectory, "recovered.frame"),
            "play");
        await recovered.CallAsync("handshake", new JsonObject { ["protocolVersion"] = 2 });
        await recovered.CallAsync("initialize");
        await LoadAsync(recovered, scene, 1, reload: false);
        await recovered.CallAsync("play");
        await WaitForPhysicsAsync(
            recovered,
            static physics => physics["worldTick"]!.GetValue<long>() >= 2,
            TimeSpan.FromSeconds(2));
        await recovered.CallAsync("shutdown");
        recovered.WaitForExit();
        AssertFileUnchanged(scene);
    }

    private static async Task VerifyTrackedSampleAsync(
        string nativeLibrary,
        string temporaryDirectory,
        SceneFile scene)
    {
        using (var preview = StartWorker(
                   nativeLibrary,
                   Path.Combine(temporaryDirectory, "tracked-sample-preview.frame"),
                   "preview"))
        {
            await preview.CallAsync("handshake", new JsonObject { ["protocolVersion"] = 2 });
            await preview.CallAsync("initialize");
            var loaded = await LoadAsync(preview, scene, 1, reload: false);
            Assert(loaded["entities"]!.GetValue<int>() == 7,
                "Tracked sample did not retain its complete authoring entity set.");
            Assert(loaded["physicsBodies"]!.GetValue<int>() == 4,
                "Tracked sample did not load two dynamic bodies and two static grounds.");
            await preview.CallAsync("play");
            await Task.Delay(200);
            var edit = (await preview.CallAsync("inspect"))["physics"]!.AsObject();
            Assert(edit["worldTick"]!.GetValue<long>() == 0,
                "Tracked sample advanced during ordinary Edit preview.");
            AssertNear(ReadEntityY(edit, SampleSpriteId), 0, 0.001,
                "Tracked sample sprite changed before simulation.");
            AssertNear(ReadEntityY(edit, SampleCubeId), 0, 0.001,
                "Tracked sample cube changed before simulation.");

            await preview.CallAsync(
                "simulatePreview",
                new JsonObject { ["enabled"] = true });
            await WaitForPhysicsAsync(
                preview,
                static physics => physics["worldTick"]!.GetValue<long>() >= 8
                    && ReadEntityY(physics, SampleSpriteId) < -0.02
                    && ReadEntityY(physics, SampleCubeId) < -0.02,
                TimeSpan.FromSeconds(3));
            await preview.CallAsync(
                "simulatePreview",
                new JsonObject { ["enabled"] = false });
            var restored = (await preview.CallAsync("inspect"))["physics"]!.AsObject();
            AssertNear(ReadEntityY(restored, SampleSpriteId), 0, 0.001,
                "Stopping tracked-sample simulation did not restore the sprite.");
            AssertNear(ReadEntityY(restored, SampleCubeId), 0, 0.001,
                "Stopping tracked-sample simulation did not restore the cube.");
            await preview.CallAsync("shutdown");
            preview.WaitForExit();
        }
        AssertFileUnchanged(scene);

        using (var play = StartWorker(
                   nativeLibrary,
                   Path.Combine(temporaryDirectory, "tracked-sample-play.frame"),
                   "play"))
        {
            await play.CallAsync("handshake", new JsonObject { ["protocolVersion"] = 2 });
            await play.CallAsync("initialize");
            await LoadAsync(play, scene, 1, reload: false);
            await play.CallAsync("play");
            await WaitForPhysicsAsync(
                play,
                static physics => physics["worldTick"]!.GetValue<long>() >= 8
                    && ReadEntityY(physics, SampleSpriteId) < -0.02
                    && ReadEntityY(physics, SampleCubeId) < -0.02,
                TimeSpan.FromSeconds(3));
            await play.CallAsync("shutdown");
            play.WaitForExit();
        }
        AssertFileUnchanged(scene);
    }

    private static async Task<JsonObject> WaitForPhysicsAsync(
        WorkerProcess worker,
        Func<JsonObject, bool> predicate,
        TimeSpan timeout)
    {
        var stopwatch = Stopwatch.StartNew();
        JsonObject? last = null;
        while (stopwatch.Elapsed < timeout)
        {
            last = (await worker.CallAsync("inspect"))["physics"]!.AsObject();
            if (predicate(last))
            {
                return last;
            }
            await Task.Delay(20);
        }
        throw new TimeoutException($"Physics state did not converge: {last?.ToJsonString()}.");
    }

    private static async Task<JsonObject> LoadAsync(
        WorkerProcess worker,
        SceneFile scene,
        long revision,
        bool reload) => await worker.CallAsync(
            reload ? "reloadSnapshot" : "loadSnapshot",
            SnapshotParameters(scene.Path, revision));

    private static JsonObject SnapshotParameters(string path, long revision) => new()
    {
        ["snapshotPath"] = path,
        ["snapshotRevision"] = revision,
    };

    private static double ReadEntityY(JsonObject physics, string entityId)
    {
        foreach (var node in physics["runtimeTransforms"]!.AsArray())
        {
            var transform = node!.AsObject();
            if (transform["entityId"]!.GetValue<string>() == entityId)
            {
                return transform["position"]!["y"]!.GetValue<double>();
            }
        }
        throw new InvalidOperationException($"Runtime transform {entityId} was missing.");
    }

    private static WorkerProcess StartWorker(
        string nativeLibrary,
        string frameFile,
        string sessionKind)
    {
        File.Delete(frameFile);
        var startInfo = new ProcessStartInfo
        {
            FileName = "dotnet",
            RedirectStandardInput = true,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            UseShellExecute = false,
            CreateNoWindow = true,
        };
        foreach (var argument in new[]
                 {
                     Assembly.GetExecutingAssembly().Location,
                     "--worker-child",
                     "--frame-file", frameFile,
                     "--frame-version", "2",
                     "--width", "64",
                     "--height", "64",
                     "--session", sessionKind,
                     "--native", nativeLibrary,
                 })
        {
            startInfo.ArgumentList.Add(argument);
        }
        return new WorkerProcess(Process.Start(startInfo)
            ?? throw new InvalidOperationException("Could not launch worker physics child."));
    }

    private static SceneFile WriteScene(
        string path,
        long snapshotRevision,
        double fallingHeight,
        bool validSettings)
    {
        var root = new JsonObject
        {
            ["$schema"] = "https://dragonpixel.dev/schemas/v3/scene.schema.json",
            ["format"] = "dpe.scene",
            ["formatVersion"] = 3,
            ["engineVersion"] = "0.2.0-test",
            ["sceneId"] = "33333333-3333-4333-8333-333333333333",
            ["name"] = "Worker Physics Lifecycle",
            ["snapshotRevision"] = snapshotRevision,
            ["physicsSettings"] = new JsonObject
            {
                ["fixedTimeStepSeconds"] = validSettings ? 1.0 / 60.0 : -1,
                ["maxCatchUpTicks"] = 4,
                ["box2DSolverSubsteps"] = 4,
                ["joltCollisionSteps"] = 1,
                ["gravity2D"] = Vector2(0, -9.81),
                ["gravity3D"] = Vector3(0, -9.81, 0),
            },
            ["prefabInstances"] = new JsonArray(),
            ["entities"] = new JsonArray
            {
                Entity(
                    GroundEntityId,
                    "Ground",
                    0,
                    new JsonArray
                    {
                        Transform(0, -0.5, 0),
                        Mesh(),
                        RigidBody("static"),
                        BoxCollider(20, 1, 20),
                    }),
                Entity(
                    FallingEntityId,
                    "Falling Cube",
                    1,
                    new JsonArray
                    {
                        Transform(0, fallingHeight, 0),
                        Mesh(),
                        RigidBody("dynamic"),
                        BoxCollider(1, 1, 1),
                    }),
            },
        };
        File.WriteAllText(path, root.ToJsonString(new JsonSerializerOptions { WriteIndented = true }));
        return new SceneFile(path, Hash(path));
    }

    private static JsonObject Entity(
        string id,
        string name,
        int siblingOrder,
        JsonArray components) => new()
    {
        ["id"] = id,
        ["name"] = name,
        ["parentId"] = null,
        ["enabled"] = true,
        ["siblingOrder"] = siblingOrder,
        ["components"] = components,
    };

    private static JsonObject Transform(double x, double y, double z) => Component(
        TransformType,
        "DragonPixel.Native.TransformComponent",
        new JsonObject
        {
            ["dpe.transform.position"] = Vector3(x, y, z),
            ["dpe.transform.rotation"] = new JsonObject
            {
                ["x"] = 0,
                ["y"] = 0,
                ["z"] = 0,
                ["w"] = 1,
            },
            ["dpe.transform.scale"] = Vector3(1, 1, 1),
        });

    private static JsonObject Mesh() => Component(
        MeshType,
        "DragonPixel.Native.StaticMeshComponent",
        new JsonObject { ["dpe.mesh.asset"] = "builtin://unit-cube" });

    private static JsonObject RigidBody(string mode) => Component(
        RigidBody3DType,
        "DragonPixel.Native.RigidBody3DComponent",
        new JsonObject
        {
            ["dpe.physics3d.body_mode"] = mode,
            ["dpe.physics3d.linear_damping"] = 0.05,
            ["dpe.physics3d.angular_damping"] = 0.05,
            ["dpe.physics3d.gravity_scale"] = 1,
            ["dpe.physics3d.initial_velocity"] = Vector3(0, 0, 0),
            ["dpe.physics3d.ccd"] = true,
        });

    private static JsonObject BoxCollider(double x, double y, double z) => Component(
        BoxCollider3DType,
        "DragonPixel.Native.BoxCollider3DComponent",
        new JsonObject
        {
            ["dpe.physics3d.size"] = Vector3(x, y, z),
            ["dpe.physics3d.offset"] = Vector3(0, 0, 0),
            ["dpe.physics.sensor"] = false,
            ["dpe.physics.friction"] = 0.6,
            ["dpe.physics.restitution"] = 0,
            ["dpe.physics.layer"] = 0,
            ["dpe.physics.mask"] = 65535,
        });

    private static JsonObject Component(string typeId, string qualifiedName, JsonObject properties) => new()
    {
        ["typeId"] = typeId,
        ["qualifiedName"] = qualifiedName,
        ["schemaVersion"] = 1,
        ["owner"] = "native",
        ["enabled"] = true,
        ["properties"] = properties,
    };

    private static JsonObject Vector2(double x, double y) => new() { ["x"] = x, ["y"] = y };
    private static JsonObject Vector3(double x, double y, double z) => new()
    {
        ["x"] = x,
        ["y"] = y,
        ["z"] = z,
    };

    private static void AssertFileUnchanged(SceneFile scene) =>
        Assert(Hash(scene.Path) == scene.Sha256, $"Worker wrote to authoring snapshot {scene.Path}.");

    private static string Hash(string path) =>
        Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(path))).ToLowerInvariant();

    private static void AssertNear(double actual, double expected, double tolerance, string message) =>
        Assert(Math.Abs(actual - expected) <= tolerance, $"{message} Actual {actual}, expected {expected}.");

    private static void Assert(bool condition, string message)
    {
        if (!condition)
        {
            throw new InvalidOperationException(message);
        }
    }

    private sealed record SceneFile(string Path, string Sha256);

    private sealed class TestAdapter : IFrameworkSceneAdapter
    {
        private FrameworkRenderRequest? _lastRequest;

        public string Name => "POC G Test Adapter";
        public string Version => "1.0";
        public bool Experimental => false;
        public string Backend => "POC-G/Headless";
        public string Device => "In-memory lifecycle verifier";

        public FrameworkFrame RenderFrame(FrameworkRenderRequest request)
        {
            _lastRequest = request;
            var pixels = new byte[checked(request.Width * request.Height * 4)];
            var falling = request.Scene.Entities.FirstOrDefault(
                static entity => entity.Id == FallingEntityId);
            if (falling is not null)
            {
                pixels[0] = checked((byte)Math.Clamp(
                    (int)Math.Round((falling.Transform.Position.Y + 10) * 8),
                    0,
                    byte.MaxValue));
            }
            return new FrameworkFrame(
                pixels,
                request.Width,
                request.Height,
                0,
                Backend,
                Device,
                request.FrameRevision,
                request.SnapshotRevision,
                request.CameraRevision,
                request.CommandRevision,
                request.InputRevision);
        }

        public FrameworkPickResult Pick(int x, int y, long minimumFrameRevision)
        {
            _ = x;
            _ = y;
            var request = _lastRequest;
            return new FrameworkPickResult(
                null,
                Math.Max(minimumFrameRevision, request?.FrameRevision ?? 0),
                request?.SnapshotRevision ?? 0,
                request?.CameraRevision ?? 0,
                request?.CommandRevision ?? 0);
        }

        public void ServicePendingOperations()
        {
        }

        public void Dispose()
        {
        }
    }

    private sealed class WorkerProcess : IDisposable
    {
        private readonly Process _process;
        private long _nextId;

        public WorkerProcess(Process process)
        {
            _process = process;
        }

        public int ExitCode => _process.ExitCode;

        public async Task<JsonObject> CallAsync(string method, JsonObject? parameters = null)
        {
            var response = await ExchangeAsync(method, parameters);
            if (response["error"] is JsonObject error)
            {
                throw new InvalidOperationException(
                    $"Worker {method} failed: {error.ToJsonString()}");
            }
            return response["result"]?.AsObject() ?? new JsonObject();
        }

        public async Task<JsonObject> CallExpectErrorAsync(string method, JsonObject? parameters = null)
        {
            var response = await ExchangeAsync(method, parameters);
            return response["error"]?.AsObject()
                ?? throw new InvalidOperationException($"Worker {method} unexpectedly succeeded.");
        }

        public void WaitForExit()
        {
            if (!_process.WaitForExit(5000))
            {
                throw new TimeoutException("Worker process did not exit.");
            }
        }

        public string ReadStandardError() => _process.StandardError.ReadToEnd().Trim();

        public void Dispose()
        {
            if (!_process.HasExited)
            {
                _process.Kill(entireProcessTree: true);
                _process.WaitForExit();
            }
            _process.Dispose();
        }

        private async Task<JsonObject> ExchangeAsync(string method, JsonObject? parameters)
        {
            var id = Interlocked.Increment(ref _nextId);
            var request = new JsonObject
            {
                ["jsonrpc"] = "2.0",
                ["id"] = id,
                ["method"] = method,
                ["params"] = parameters,
            };
            var payload = JsonSerializer.SerializeToUtf8Bytes(request);
            var length = new byte[4];
            BinaryPrimitives.WriteInt32LittleEndian(length, payload.Length);
            await _process.StandardInput.BaseStream.WriteAsync(length);
            await _process.StandardInput.BaseStream.WriteAsync(payload);
            await _process.StandardInput.BaseStream.FlushAsync();

            var responseLength = new byte[4];
            await ReadExactlyAsync(_process.StandardOutput.BaseStream, responseLength);
            var count = BinaryPrimitives.ReadInt32LittleEndian(responseLength);
            var responsePayload = new byte[count];
            await ReadExactlyAsync(_process.StandardOutput.BaseStream, responsePayload);
            var response = JsonNode.Parse(responsePayload)?.AsObject()
                ?? throw new InvalidDataException("Worker response was invalid JSON.");
            Assert(response["id"]!.GetValue<long>() == id, "Worker response ID was not correlated.");
            return response;
        }

        private static async Task ReadExactlyAsync(Stream stream, byte[] buffer)
        {
            var offset = 0;
            while (offset < buffer.Length)
            {
                var read = await stream.ReadAsync(buffer.AsMemory(offset));
                if (read == 0)
                {
                    throw new EndOfStreamException("Worker closed its control stream.");
                }
                offset += read;
            }
        }
    }
}
