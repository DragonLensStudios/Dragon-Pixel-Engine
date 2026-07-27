using System.Buffers.Binary;
using System.Diagnostics;
using System.IO.MemoryMappedFiles;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;

namespace DragonPixel.PocB.Tests;

internal static class Program
{
    private const int FrameMagic = 0x46504544;
    private const int Version1HeaderSize = 64;
    private const int Version2HeaderSize = 96;
    private const int LatencySampleCount = 21;
    private const string SpriteId = "e75d033b-bba0-4f8f-8f54-c7a1e6990aef";
    private const string CubeId = "3466ea8a-d7d4-458c-83ae-cc33291e5c26";
    private const string RuntimeSpriteAssetId = "4dc81fa5-c0f7-4dfd-8610-bcaffb053bcf";
    private const string CyanPngBase64 = "iVBORw0KGgoAAAANSUhEUgAAAAIAAAACCAYAAABytg0kAAAAAXNSR0IArs4c6QAAAARnQU1BAACxjwv8YQUAAAAJcEhZcwAADsMAAA7DAcdvqGQAAAAQSURBVBhXY2D4//8/GMMYAGWsC/VzMJBzAAAAAElFTkSuQmCC";

    private static async Task<int> Main(string[] args)
    {
        try
        {
            if (args.Length is not (4 or 6)
                || (args.Length == 6 && args[4] != "--adapter"))
            {
                throw new ArgumentException(
                    "Usage: POC-B/E tests <MonoGame worker DLL> <KNI worker DLL> <temporary directory> "
                    + "<native C ABI library> "
                    + "[--adapter monogame|kni]");
            }

            Directory.CreateDirectory(args[2]);
            var nativeLibrary = Path.GetFullPath(args[3]);
            var requestedAdapter = args.Length == 6 ? args[5].ToLowerInvariant() : "both";
            if (requestedAdapter is not ("both" or "monogame" or "kni"))
            {
                throw new ArgumentException("--adapter must be monogame or kni.");
            }
            var runs = new List<AdapterRun>();
            if (requestedAdapter is "both" or "monogame")
            {
                runs.Add(await RunIndependentlyAsync(
                    "MonoGame",
                    Path.GetFullPath(args[0]),
                    Path.Combine(args[2], "monogame"),
                    nativeLibrary));
            }
            if (requestedAdapter is "both" or "kni")
            {
                runs.Add(await RunIndependentlyAsync(
                    "KNI",
                    Path.GetFullPath(args[1]),
                    Path.Combine(args[2], "kni"),
                    nativeLibrary));
            }
            var failures = runs.Where(static run => run.Exception is not null).ToArray();
            if (failures.Length != 0)
            {
                Console.Error.WriteLine(
                    $"POC B/E failed for {string.Join(", ", failures.Select(static run => run.Adapter))}; "
                    + "all adapter runs were recorded independently.");
                return 1;
            }

            Console.WriteLine(
                $"POC B/E passed with scene-driven {string.Join(" and ", runs.Select(static run => run.Adapter))} "
                + "device frames.");
            return 0;
        }
        catch (Exception exception)
        {
            Console.Error.WriteLine(exception);
            return 1;
        }
    }

    private static async Task<AdapterRun> RunIndependentlyAsync(
        string expectedAdapter,
        string workerDll,
        string runDirectory,
        string nativeLibrary)
    {
        try
        {
            Directory.CreateDirectory(runDirectory);
            await VerifyWorkerAsync(expectedAdapter, workerDll, runDirectory, nativeLibrary);
            await VerifyInputComponentAsync(expectedAdapter, workerDll, runDirectory, nativeLibrary);
            await VerifyV1CompatibilityAsync(expectedAdapter, workerDll, runDirectory, nativeLibrary);
            return new AdapterRun(expectedAdapter, null);
        }
        catch (Exception exception)
        {
            Console.Error.WriteLine($"{expectedAdapter} FAILED:{Environment.NewLine}{exception}");
            return new AdapterRun(expectedAdapter, exception);
        }
    }

    private static async Task VerifyInputComponentAsync(
        string expectedAdapter,
        string workerDll,
        string runDirectory,
        string nativeLibrary)
    {
        var frameFile = Path.Combine(runDirectory, "input-component-v2.frame");
        File.Delete(frameFile);
        var snapshots = CreateSnapshots(runDirectory);
        using var worker = StartWorker(
            workerDll,
            frameFile,
            nativeLibrary,
            frameVersion: 2,
            width: 1280,
            height: 720,
            session: "play");

        await AssertRpcRejectedAsync(
            () => worker.CallAsync(
                "runtimeInput",
                new JsonObject
                {
                    ["inputRevision"] = 1,
                    ["actions"] = new JsonObject { ["move.x"] = 1.0 },
                }),
            "-32013",
            "A runtimeInput request was accepted before the mandatory handshake.");
        var handshake = await worker.CallAsync(
            "handshake",
            new JsonObject { ["protocolVersion"] = 2 });
        Assert(handshake["adapter"]!.GetValue<string>() == expectedAdapter,
            "Input proof worker reported the wrong adapter.");
        Assert(handshake["runtimeInput"]!.GetValue<bool>(),
            "Input proof worker did not negotiate runtimeInput.");
        await worker.CallAsync("initialize");
        await worker.CallAsync(
            "setViewport",
            new JsonObject
            {
                ["width"] = 1280,
                ["height"] = 720,
                ["cameraRevision"] = 1,
                ["commandRevision"] = 1,
                ["camera"] = OrthographicCamera(),
            });
        await LoadSnapshotAsync(worker, snapshots[14], 14, reload: false);
        await worker.CallAsync("play");

        var viewportAcknowledgement = await worker.CallAsync(
            "viewportInput",
            new JsonObject
            {
                ["inputRevision"] = 1,
                ["width"] = 1280,
                ["height"] = 720,
                ["cameraRevision"] = 1,
                ["commandRevision"] = 2,
            });
        Assert(viewportAcknowledgement["inputRevision"]!.GetValue<long>() == 1,
            "Viewport command revision was not acknowledged independently.");
        await AssertRpcRejectedAsync(
            () => worker.CallAsync(
                "viewportInput",
                new JsonObject
                {
                    ["inputRevision"] = 2,
                    ["width"] = 640,
                    ["height"] = 360,
                    ["cameraRevision"] = 1,
                    ["commandRevision"] = 1,
                }),
            "-32602",
            "A regressing viewport command was accepted.");
        var viewportAfterRejection = await worker.CallAsync("setViewport");
        Assert(viewportAfterRejection["width"]!.GetValue<int>() == 1280
            && viewportAfterRejection["height"]!.GetValue<int>() == 720
            && viewportAfterRejection["commandRevision"]!.GetValue<long>() == 2,
            "Rejected viewportInput partially mutated viewport state.");
        viewportAcknowledgement = await worker.CallAsync(
            "viewportInput",
            new JsonObject
            {
                ["inputRevision"] = 2,
                ["width"] = 1280,
                ["height"] = 720,
                ["cameraRevision"] = 1,
                ["commandRevision"] = 3,
            });
        Assert(viewportAcknowledgement["inputRevision"]!.GetValue<long>() == 2,
            "Rejected viewportInput consumed its viewport command revision.");
        var mixedRevisionDiagnostics = await worker.CallAsync("diagnostics");
        Assert(mixedRevisionDiagnostics["viewportInputRevision"]!.GetValue<long>() == 2
            && mixedRevisionDiagnostics["inputRevision"]!.GetValue<long>() == 0,
            "Viewport command input contaminated the runtime action revision stream.");

        var baseline = await ReadFrameAfterAsync(frameFile, 0, 14, TimeSpan.FromSeconds(8));
        Assert(baseline.InputRevision == 0, "Input proof did not start from an uncredited neutral state.");
        await AssertPickAsync(worker, 496, 360, baseline.FrameRevision, SpriteId);

        var ignoredAcknowledgement = await worker.CallAsync(
            "runtimeInput",
            RuntimeInputSnapshot(
                inputRevision: 1,
                lookX: 1,
                lookXPressCount: 1));
        Assert(ignoredAcknowledgement["inputRevision"]!.GetValue<long>() == 1,
            "Non-applicable input revision was not acknowledged.");
        var ignoredFrame = await ReadFrameAfterAsync(
            frameFile,
            baseline.Sequence,
            14,
            TimeSpan.FromSeconds(4));
        var ignoredStableFrame = await ReadFrameAfterAsync(
            frameFile,
            ignoredFrame.Sequence,
            14,
            TimeSpan.FromSeconds(4));
        Assert(ignoredFrame.InputRevision == 0 && ignoredStableFrame.InputRevision == 0,
            "A non-applicable input revision was falsely credited to rendered frames.");
        Assert(ignoredFrame.PixelSha256 == baseline.PixelSha256
            && ignoredStableFrame.PixelSha256 == baseline.PixelSha256,
            "A non-applicable input action changed device-produced pixels.");

        var pressAcknowledgement = await worker.CallAsync(
            "runtimeInput",
            RuntimeInputSnapshot(
                inputRevision: 2,
                moveX: 1,
                moveXPressCount: 1,
                lookX: 1,
                lookXPressCount: 1));
        Assert(pressAcknowledgement["inputRevision"]!.GetValue<long>() == 2,
            "Applicable input action revision was not acknowledged.");
        var moved = await ReadFrameAfterAsync(
            frameFile,
            ignoredStableFrame.Sequence,
            14,
            TimeSpan.FromSeconds(4),
            minimumInputRevision: 2);
        Assert(moved.InputRevision == 2,
            "The press revision advanced before applicable InputMotion behavior was reflected.");
        Assert(moved.PixelSha256 != ignoredStableFrame.PixelSha256,
            "InputMotion2D did not change device-produced pixels before crediting move.x input.");

        await AssertRpcRejectedAsync(
            () => worker.CallAsync(
                "runtimeInput",
                RuntimeInputSnapshot(
                    inputRevision: 3,
                    moveX: 1,
                    moveXPressCount: 2,
                    lookX: 1,
                    lookXPressCount: 1)),
            "-32602",
            "runtimeInput accepted inconsistent edge counters.");
        var runtimeAfterRejection = await worker.CallAsync("diagnostics");
        Assert(runtimeAfterRejection["inputRevision"]!.GetValue<long>() == 2
            && runtimeAfterRejection["viewportInputRevision"]!.GetValue<long>() == 2,
            "Rejected runtimeInput partially advanced an input revision stream.");
        await AssertRpcRejectedAsync(
            () => worker.CallAsync(
                "runtimeInput",
                RuntimeInputSnapshot(
                    inputRevision: 2,
                    moveX: 1,
                    moveXPressCount: 1,
                    lookX: 1,
                    lookXPressCount: 1)),
            "-32602",
            "runtimeInput accepted a stale revision.");

        var identicalAcknowledgement = await worker.CallAsync(
            "runtimeInput",
            RuntimeInputSnapshot(
                inputRevision: 3,
                moveX: 1,
                moveXPressCount: 1,
                lookX: 1,
                lookXPressCount: 1));
        Assert(identicalAcknowledgement["inputRevision"]!.GetValue<long>() == 3,
            "Semantically identical input revision was not acknowledged.");
        var identicalFrame = await ReadFrameAfterAsync(
            frameFile,
            moved.Sequence,
            14,
            TimeSpan.FromSeconds(4));
        var identicalLaterFrame = await ReadFrameAfterAsync(
            frameFile,
            identicalFrame.Sequence,
            14,
            TimeSpan.FromSeconds(4));
        Assert(identicalFrame.InputRevision == 2 && identicalLaterFrame.InputRevision == 2,
            "A semantically identical action state was falsely credited as new reflected input.");

        await Task.Delay(350);
        var displacedActiveFrame = await ReadFrameAfterAsync(
            frameFile,
            identicalLaterFrame.Sequence,
            14,
            TimeSpan.FromSeconds(4),
            minimumInputRevision: 2);

        var neutral = await worker.CallAsync(
            "runtimeInput",
            RuntimeInputSnapshot(
                inputRevision: 4,
                moveXPressCount: 1,
                moveXReleaseCount: 1,
                lookXPressCount: 1,
                lookXReleaseCount: 1));
        Assert(neutral["neutral"]!.GetValue<bool>(), "Neutral input state was not accepted.");
        var neutralFrame = await ReadFrameAfterAsync(
            frameFile,
            displacedActiveFrame.Sequence,
            14,
            TimeSpan.FromSeconds(4),
            minimumInputRevision: 4);
        Assert(neutralFrame.InputRevision == 4,
            "The active-to-neutral release was not credited as a consumed stop state.");
        var stableNeutralFrame = await ReadFrameAfterAsync(
            frameFile,
            neutralFrame.Sequence,
            14,
            TimeSpan.FromSeconds(4),
            minimumInputRevision: 4);
        Assert(stableNeutralFrame.InputRevision == 4,
            "The reflected neutral input revision was not retained.");
        Assert(stableNeutralFrame.PixelSha256 == neutralFrame.PixelSha256,
            "InputMotion2D continued changing pixels after the neutral revision was reflected.");
        await AssertPickAsync(worker, 496, 360, stableNeutralFrame.FrameRevision, null);
        var displacedPickX = await FindPickXAsync(
            worker,
            520,
            1000,
            8,
            360,
            stableNeutralFrame.FrameRevision,
            SpriteId);
        Assert(displacedPickX is > 496,
            "The ID-buffer pick did not move with the input-displaced device pixels.");

        await worker.CallAsync("shutdown");
        await worker.WaitForExitAsync(TimeSpan.FromSeconds(5));
    }

    private static async Task VerifyWorkerAsync(
        string expectedAdapter,
        string workerDll,
        string runDirectory,
        string nativeLibrary)
    {
        var frameFile = Path.Combine(runDirectory, "device-v2.frame");
        File.Delete(frameFile);
        var snapshots = CreateSnapshots(runDirectory);
        using var worker = StartWorker(
            workerDll,
            frameFile,
            nativeLibrary,
            frameVersion: 2,
            width: 1280,
            height: 720);

        var controlTimer = Stopwatch.StartNew();
        var handshake = await worker.CallAsync(
            "handshake",
            new JsonObject { ["protocolVersion"] = 2 });
        controlTimer.Stop();
        Assert(handshake["adapter"]!.GetValue<string>() == expectedAdapter, "Worker reported the wrong adapter.");
        Assert(handshake["pixelFormat"]!.GetValue<string>() == "BGRA8", "Worker did not negotiate BGRA8.");
        Assert(handshake["frameLayoutVersion"]!.GetValue<int>() == 2, "Worker did not negotiate frame layout v2.");
        Assert(handshake["frameHeaderSize"]!.GetValue<int>() == Version2HeaderSize, "Frame v2 header size was wrong.");
        Assert(handshake["sceneDrivenGraphics"]!.GetValue<bool>(), "Worker did not declare scene-driven graphics.");
        Assert(handshake["idBufferPicking"]!.GetValue<bool>(), "Worker did not negotiate ID-buffer picking.");
        Assert(handshake["revisionCorrelatedFrames"]!.GetValue<bool>(), "Worker did not negotiate revision correlation.");
        Assert(!handshake["runtimeInput"]!.GetValue<bool>(),
            "Preview worker incorrectly negotiated Play-only runtimeInput.");
        await AssertRpcRejectedAsync(
            () => worker.CallAsync(
                "runtimeInput",
                new JsonObject
                {
                    ["inputRevision"] = 1,
                    ["actions"] = new JsonObject { ["move.x"] = 1.0 },
                }),
            "-32011",
            "A Preview worker accepted Play-only runtimeInput.");
        Assert(
            handshake["backend"]!.GetValue<string>().Contains(expectedAdapter, StringComparison.Ordinal),
            "Worker backend diagnostics did not identify the selected adapter.");
        Assert(controlTimer.ElapsedMilliseconds < 1000, "Initial control response exceeded one second.");

        await worker.CallAsync("initialize");
        await worker.CallAsync(
            "setViewport",
            new JsonObject
            {
                ["width"] = 1280,
                ["height"] = 720,
                ["cameraRevision"] = 1,
                ["commandRevision"] = 1,
                ["camera"] = OrthographicCamera(),
            });
        await LoadSnapshotAsync(worker, snapshots[1], 1, reload: false);
        await worker.CallAsync("play");

        var spriteFrame = await ReadFrameAfterAsync(frameFile, 0, 1, TimeSpan.FromSeconds(8));
        Assert(spriteFrame.Width == 1280 && spriteFrame.Height == 720 && spriteFrame.Stride == 5120,
            "Frame dimensions or stride were invalid.");
        Assert(spriteFrame.PixelFormat == 1 && spriteFrame.ContentFlags == 1,
            "Sprite-only snapshot did not produce the expected device-frame content flags.");
        Assert(spriteFrame.DistinctColorEstimate >= 3, "Device frame did not contain rendered sprite variation.");
        await AssertPickAsync(worker, 496, 360, spriteFrame.FrameRevision, SpriteId);
        await AssertStalePickIsRejectedAsync(worker, spriteFrame.FrameRevision);

        await LoadSnapshotAsync(worker, snapshots[2], 2, reload: true);
        var addedFrame = await ReadFrameAfterAsync(frameFile, spriteFrame.Sequence, 2, TimeSpan.FromSeconds(4));
        Assert(addedFrame.ContentFlags == 3, "Adding a mesh did not update frame content flags.");
        Assert(addedFrame.PixelSha256 != spriteFrame.PixelSha256, "Adding a mesh did not change device pixels.");
        await AssertPickAsync(worker, 784, 360, addedFrame.FrameRevision, CubeId);

        await LoadSnapshotAsync(worker, snapshots[3], 3, reload: true);
        var movedFrame = await ReadFrameAfterAsync(frameFile, addedFrame.Sequence, 3, TimeSpan.FromSeconds(4));
        Assert(movedFrame.PixelSha256 != addedFrame.PixelSha256, "Moving the mesh did not change device pixels.");
        await AssertPickAsync(worker, 640, 360, movedFrame.FrameRevision, CubeId);
        await AssertPickAsync(worker, 784, 360, movedFrame.FrameRevision, null);

        await LoadSnapshotAsync(worker, snapshots[4], 4, reload: true);
        var coloredFrame = await ReadFrameAfterAsync(frameFile, movedFrame.Sequence, 4, TimeSpan.FromSeconds(4));
        Assert(coloredFrame.PixelSha256 != movedFrame.PixelSha256, "Changing material color did not change device pixels.");
        await AssertPickAsync(worker, 640, 360, coloredFrame.FrameRevision, CubeId);

        await LoadSnapshotAsync(worker, snapshots[5], 5, reload: true);
        var disabledFrame = await ReadFrameAfterAsync(frameFile, coloredFrame.Sequence, 5, TimeSpan.FromSeconds(4));
        Assert(disabledFrame.ContentFlags == 1, "Disabling the mesh did not remove mesh content.");
        Assert(disabledFrame.PixelSha256 != coloredFrame.PixelSha256, "Disabling the mesh did not change device pixels.");
        await AssertPickAsync(worker, 640, 360, disabledFrame.FrameRevision, null);

        await LoadSnapshotAsync(worker, snapshots[6], 6, reload: true);
        var enabledFrame = await ReadFrameAfterAsync(frameFile, disabledFrame.Sequence, 6, TimeSpan.FromSeconds(4));
        Assert(enabledFrame.ContentFlags == 3, "Re-enabling the mesh did not restore mesh content.");
        await LoadSnapshotAsync(worker, snapshots[7], 7, reload: true);
        var deletedFrame = await ReadFrameAfterAsync(frameFile, enabledFrame.Sequence, 7, TimeSpan.FromSeconds(4));
        Assert(deletedFrame.ContentFlags == 1, "Deleting the mesh did not remove mesh content.");
        Assert(deletedFrame.PixelSha256 != enabledFrame.PixelSha256, "Deleting the mesh did not change device pixels.");
        await AssertPickAsync(worker, 640, 360, deletedFrame.FrameRevision, null);

        await LoadSnapshotAsync(worker, snapshots[8], 8, reload: true);
        var colliderBaselineFrame = await ReadFrameAfterAsync(
            frameFile,
            deletedFrame.Sequence,
            8,
            TimeSpan.FromSeconds(4));
        Assert(colliderBaselineFrame.ContentFlags == 3,
            "The collider baseline did not retain its sprite and mesh content flags.");
        await AssertPickAsync(worker, 496, 360, colliderBaselineFrame.FrameRevision, SpriteId);
        await AssertPickAsync(worker, 640, 360, colliderBaselineFrame.FrameRevision, CubeId);

        var colliderFrame = colliderBaselineFrame;
        foreach (var (revision, colliderName) in new[]
                 {
                     (9, "BoxCollider2D"),
                     (10, "CircleCollider2D"),
                     (11, "BoxCollider3D"),
                     (12, "SphereCollider3D"),
                     (13, "all collider kinds"),
                 })
        {
            await LoadSnapshotAsync(worker, snapshots[revision], revision, reload: true);
            colliderFrame = await ReadFrameAfterAsync(
                frameFile,
                colliderFrame.Sequence,
                revision,
                TimeSpan.FromSeconds(4));
            Assert(colliderFrame.ContentFlags == colliderBaselineFrame.ContentFlags,
                $"{colliderName} overlays incorrectly changed runtime content flags.");
            Assert(colliderFrame.PixelSha256 != colliderBaselineFrame.PixelSha256,
                $"Adding {colliderName} did not change Edit-mode device pixels.");
        }
        await AssertPickAsync(worker, 496, 360, colliderFrame.FrameRevision, SpriteId);
        await AssertPickAsync(worker, 640, 360, colliderFrame.FrameRevision, CubeId);

        await LoadSnapshotAsync(worker, snapshots[15], 15, reload: true);
        var squareFrame = await ReadFrameAfterAsync(
            frameFile,
            colliderFrame.Sequence,
            15,
            TimeSpan.FromSeconds(4));
        Assert(squareFrame.ContentFlags == 1,
            "The square primitive did not render through the Sprite component path.");
        await AssertPickAsync(worker, 496, 360, squareFrame.FrameRevision, SpriteId);
        await AssertPickAsync(worker, 456, 320, squareFrame.FrameRevision, SpriteId);

        await LoadSnapshotAsync(worker, snapshots[16], 16, reload: true);
        var circleFrame = await ReadFrameAfterAsync(
            frameFile,
            squareFrame.Sequence,
            16,
            TimeSpan.FromSeconds(4));
        Assert(circleFrame.ContentFlags == 1,
            "The circle primitive did not render through the Sprite component path.");
        Assert(circleFrame.PixelSha256 != squareFrame.PixelSha256,
            "Square and circle Sprite primitives produced identical device pixels.");
        await AssertPickAsync(worker, 496, 360, circleFrame.FrameRevision, SpriteId);
        await AssertPickAsync(worker, 456, 320, circleFrame.FrameRevision, null);

        await LoadSnapshotAsync(worker, snapshots[17], 17, reload: true);
        var importedSpriteFrame = await ReadFrameAfterAsync(
            frameFile,
            circleFrame.Sequence,
            17,
            TimeSpan.FromSeconds(4));
        Assert(importedSpriteFrame.ContentFlags == 1
                && importedSpriteFrame.PixelSha256 != circleFrame.PixelSha256,
            "An immutable imported sprite binding did not change framework device pixels.");
        var importedBlue = importedSpriteFrame.SamplePixelBgra & 0xff;
        var importedGreen = (importedSpriteFrame.SamplePixelBgra >> 8) & 0xff;
        var importedRed = (importedSpriteFrame.SamplePixelBgra >> 16) & 0xff;
        Assert(importedBlue is >= 172 and <= 184
                && importedGreen is >= 72 and <= 82
                && importedRed <= 4,
            $"Imported cyan PNG bytes did not reach the Sprite shader; sampled BGRA was "
            + $"{importedBlue},{importedGreen},{importedRed}.");
        await AssertPickAsync(worker, 496, 360, importedSpriteFrame.FrameRevision, SpriteId);

        var resized = await worker.CallAsync(
            "resizeViewport",
            new JsonObject
            {
                ["width"] = 640,
                ["height"] = 360,
                ["cameraRevision"] = 2,
                ["commandRevision"] = 2,
            });
        Assert(resized["width"]!.GetValue<int>() == 640, "Viewport resize was not acknowledged.");
        var smallFrame = await ReadFrameAfterAsync(
            frameFile,
            importedSpriteFrame.Sequence,
            17,
            TimeSpan.FromSeconds(4),
            minimumCameraRevision: 2);
        Assert(smallFrame.Width == 640 && smallFrame.Height == 360, "Resized frame dimensions were not published.");
        await worker.CallAsync(
            "resizeViewport",
            new JsonObject
            {
                ["width"] = 1280,
                ["height"] = 720,
                ["cameraRevision"] = 3,
                ["commandRevision"] = 3,
            });
        var fullFrame = await ReadFrameAfterAsync(
            frameFile,
            smallFrame.Sequence,
            16,
            TimeSpan.FromSeconds(4),
            minimumCameraRevision: 3);
        Assert(fullFrame.Width == 1280 && fullFrame.Height == 720, "Full viewport size was not restored.");

        await Task.Delay(750);
        FrameHeader rateStart;
        FrameHeader rateEnd;
        using (var rateReader = new SharedFrameReader(frameFile))
        {
            rateStart = await ReadFrameHeaderAfterAsync(
                rateReader,
                fullFrame.Sequence,
                16,
                TimeSpan.FromSeconds(3));
            await Task.Delay(2000);
            rateEnd = await ReadFrameHeaderAfterAsync(
                rateReader,
                rateStart.Sequence,
                16,
                TimeSpan.FromSeconds(2));
        }
        var presentedDuration = TimeSpan.FromTicks(rateEnd.TimestampTicks - rateStart.TimestampTicks);
        Assert(presentedDuration > TimeSpan.Zero, "Presented-frame timestamps did not advance.");
        var framesPerSecond = ((rateEnd.Sequence - rateStart.Sequence) / 2.0) / presentedDuration.TotalSeconds;
        var diagnostics = await worker.CallAsync("diagnostics");
        var timings = diagnostics["lastFrameTimingsMs"]!.AsObject();
        var timingSummary =
            $"device render/readback {timings["adapter"]!.GetValue<double>():F1} ms, "
            + $"publish {timings["publish"]!.GetValue<double>():F1} ms";
        Assert(string.IsNullOrEmpty(diagnostics["renderFault"]!.GetValue<string>()),
            $"Framework render faulted: {diagnostics["renderFault"]}");
        Assert(
            diagnostics["device"]!.GetValue<string>() != "not initialized",
            "Diagnostics did not identify the actual initialized graphics device.");
        Assert(framesPerSecond >= 30.0,
            $"{expectedAdapter} frame rate was only {framesPerSecond:F1} FPS ({timingSummary}).");

        var viewportCommandToPresentSamples = new double[LatencySampleCount];
        using (var correlationReader = new SharedFrameReader(frameFile))
        {
            for (var index = 0; index < viewportCommandToPresentSamples.Length; index++)
            {
                var inputRevision = index + 1L;
                var commandRevision = inputRevision + 3;
                var inputTimestamp = Stopwatch.GetTimestamp();
                var acknowledgement = await worker.CallAsync(
                    "viewportInput",
                    new JsonObject
                    {
                        ["inputRevision"] = inputRevision,
                        ["cameraRevision"] = 3,
                        ["commandRevision"] = commandRevision,
                    });
                Assert(
                    acknowledgement["inputRevision"]!.GetValue<long>() == inputRevision,
                    "Worker acknowledged the wrong input revision.");
                var correlatedFrame = await ReadFrameForCommandRevisionAsync(
                    correlationReader,
                    commandRevision,
                    TimeSpan.FromSeconds(2));
                Assert(correlatedFrame.CommandRevision >= commandRevision && correlatedFrame.Sequence > 0,
                    "Presented frame did not carry the requested viewport command revision.");
                viewportCommandToPresentSamples[index] = Stopwatch.GetElapsedTime(inputTimestamp).TotalMilliseconds;
            }
        }

        Array.Sort(viewportCommandToPresentSamples);
        var medianViewportCommandToPresentMilliseconds =
            viewportCommandToPresentSamples[viewportCommandToPresentSamples.Length / 2];
        Assert(medianViewportCommandToPresentMilliseconds < 100.0,
            $"{expectedAdapter} median viewport-command-to-present latency was "
            + $"{medianViewportCommandToPresentMilliseconds:F1} ms, above the 100 ms gate.");

        var postLatencyDiagnostics = await worker.CallAsync("diagnostics");
        Assert(
            postLatencyDiagnostics["presentedInputRevision"]!.GetValue<long>() == 0,
            "Viewport-only commands were falsely credited as reflected gameplay input.");

        long stoppedSequence;
        using (var lifecycleReader = new SharedFrameReader(frameFile))
        {
            await worker.CallAsync("pause");
            await Task.Delay(100);
            var pausedSnapshot = lifecycleReader.ReadFrame();
            var pausedFrame = lifecycleReader.ReadHeader();
            var pausedSequence = pausedFrame.Sequence;
            await AssertPickTimeoutIsStructuredAsync(worker, pausedFrame.FrameRevision + 1_000_000);
            await AssertPickAsync(worker, 496, 360, pausedFrame.FrameRevision, SpriteId);
            var renderedWhilePaused = await ReadFrameHeaderAfterAsync(
                lifecycleReader,
                pausedSequence,
                16,
                TimeSpan.FromSeconds(2));
            var stablePausedSnapshot = lifecycleReader.ReadFrame();
            Assert(renderedWhilePaused.Sequence > pausedSequence,
                "Pause stopped rendering instead of preserving the viewport.");
            Assert(stablePausedSnapshot.PixelSha256 == pausedSnapshot.PixelSha256,
                "Pause advanced render-affecting simulation state.");

            await worker.CallAsync("resume");
            var resumed = await ReadFrameHeaderAfterAsync(
                lifecycleReader,
                renderedWhilePaused.Sequence,
                16,
                TimeSpan.FromSeconds(2));
            await worker.CallAsync("stop");
            await Task.Delay(100);
            stoppedSequence = lifecycleReader.ReadHeader().Sequence;
            await Task.Delay(250);
            Assert(lifecycleReader.ReadHeader().Sequence == stoppedSequence, "Stop did not stop frame publication.");
            Assert(resumed.Sequence > renderedWhilePaused.Sequence, "Resume did not continue frame publication.");

            await worker.CallAsync("play");
            await ReadFrameHeaderAfterAsync(
                lifecycleReader,
                stoppedSequence,
                16,
                TimeSpan.FromSeconds(2));
        }
        var firstProcessId = worker.ProcessId;
        await worker.CallAsync("crash");
        await worker.WaitForExitAsync(TimeSpan.FromSeconds(5));
        Assert(worker.ExitCode == 86, "Forced worker crash did not cross the process boundary.");

        var recoveryTimer = Stopwatch.StartNew();
        File.Delete(frameFile);
        using var restarted = StartWorker(
            workerDll,
            frameFile,
            nativeLibrary,
            frameVersion: 2,
            width: 1280,
            height: 720);
        var restartedHandshake = await restarted.CallAsync(
            "handshake",
            new JsonObject { ["protocolVersion"] = 2 });
        Assert(restartedHandshake["adapter"]!.GetValue<string>() == expectedAdapter,
            "Restarted worker reported the wrong adapter.");
        Assert(restarted.ProcessId != firstProcessId, "Crash recovery reused the terminated process.");
        await LoadSnapshotAsync(restarted, snapshots[1], 1, reload: false);
        await restarted.CallAsync("play");
        await ReadFrameAfterAsync(frameFile, 0, 1, TimeSpan.FromSeconds(8));
        recoveryTimer.Stop();
        Assert(recoveryTimer.Elapsed < TimeSpan.FromSeconds(8), "Worker crash recovery exceeded eight seconds.");
        await restarted.CallAsync("shutdown");
        await restarted.WaitForExitAsync(TimeSpan.FromSeconds(5));
        Assert(restarted.ExitCode == 0, "Worker did not shut down cleanly.");

        Console.WriteLine(
            $"{expectedAdapter}: real scene-driven frames, {framesPerSecond:F1} FPS, "
            + $"median viewport-command-to-present {medianViewportCommandToPresentMilliseconds:F1} ms, "
            + $"control {controlTimer.Elapsed.TotalMilliseconds:F1} ms, "
            + $"crash recovery {recoveryTimer.Elapsed.TotalMilliseconds:F1} ms, {timingSummary}, "
            + $"backend {diagnostics["backend"]}, device {diagnostics["device"]}.");
    }

    private static async Task VerifyV1CompatibilityAsync(
        string expectedAdapter,
        string workerDll,
        string runDirectory,
        string nativeLibrary)
    {
        var frameFile = Path.Combine(runDirectory, "compat-v1.frame");
        File.Delete(frameFile);
        var snapshot = Path.Combine(runDirectory, "snapshot-1.dpescene");
        using var worker = StartWorker(
            workerDll,
            frameFile,
            nativeLibrary,
            frameVersion: 1,
            width: 320,
            height: 180);
        var handshake = await worker.CallAsync("handshake");
        Assert(handshake["protocolVersion"]!.GetValue<int>() == 1, "Protocol v1 handshake regressed.");
        Assert(handshake["frameLayoutVersion"]!.GetValue<int>() == 1, "Frame layout v1 handshake regressed.");
        Assert(handshake["frameHeaderSize"]!.GetValue<int>() == Version1HeaderSize, "Frame layout v1 header changed.");
        Assert(handshake["adapter"]!.GetValue<string>() == expectedAdapter, "v1 adapter identity regressed.");
        await AssertRpcRejectedAsync(
            () => worker.CallAsync(
                "runtimeInput",
                new JsonObject
                {
                    ["inputRevision"] = 1,
                    ["actions"] = new JsonObject { ["move.x"] = 1.0 },
                }),
            "-32011",
            "A protocol-v1 session accepted unnegotiated runtimeInput.");
        await LoadSnapshotAsync(worker, snapshot, 1, reload: false);
        await worker.CallAsync("play");
        var frame = await ReadFrameAfterAsync(frameFile, 0, 0, TimeSpan.FromSeconds(8));
        Assert(frame.Version == 1 && frame.Width == 320 && frame.Height == 180,
            "A v1 client could not read the legacy shared-frame prefix.");
        await worker.CallAsync("shutdown");
        await worker.WaitForExitAsync(TimeSpan.FromSeconds(5));
    }

    private static Dictionary<int, string> CreateSnapshots(string directory)
    {
        var configurations = new[]
        {
            new SnapshotConfiguration(1, IncludeCube: false, CubeEnabled: false, CubeX: 2, CubeColor: "red"),
            new SnapshotConfiguration(2, IncludeCube: true, CubeEnabled: true, CubeX: 2, CubeColor: "red"),
            new SnapshotConfiguration(3, IncludeCube: true, CubeEnabled: true, CubeX: 0, CubeColor: "red"),
            new SnapshotConfiguration(4, IncludeCube: true, CubeEnabled: true, CubeX: 0, CubeColor: "green"),
            new SnapshotConfiguration(5, IncludeCube: true, CubeEnabled: false, CubeX: 0, CubeColor: "green"),
            new SnapshotConfiguration(6, IncludeCube: true, CubeEnabled: true, CubeX: 0, CubeColor: "blue"),
            new SnapshotConfiguration(7, IncludeCube: false, CubeEnabled: false, CubeX: 0, CubeColor: "blue"),
            new SnapshotConfiguration(
                8,
                IncludeCube: true,
                CubeEnabled: true,
                CubeX: 0,
                CubeColor: "blue",
                IncludeColliderFixture: true),
            new SnapshotConfiguration(
                9,
                IncludeCube: true,
                CubeEnabled: true,
                CubeX: 0,
                CubeColor: "blue",
                IncludeColliderFixture: true,
                IncludeBox2D: true),
            new SnapshotConfiguration(
                10,
                IncludeCube: true,
                CubeEnabled: true,
                CubeX: 0,
                CubeColor: "blue",
                IncludeColliderFixture: true,
                IncludeCircle2D: true),
            new SnapshotConfiguration(
                11,
                IncludeCube: true,
                CubeEnabled: true,
                CubeX: 0,
                CubeColor: "blue",
                IncludeColliderFixture: true,
                IncludeBox3D: true),
            new SnapshotConfiguration(
                12,
                IncludeCube: true,
                CubeEnabled: true,
                CubeX: 0,
                CubeColor: "blue",
                IncludeColliderFixture: true,
                IncludeSphere3D: true),
            new SnapshotConfiguration(
                13,
                IncludeCube: true,
                CubeEnabled: true,
                CubeX: 0,
                CubeColor: "blue",
                IncludeColliderFixture: true,
                IncludeBox2D: true,
                IncludeCircle2D: true,
                IncludeBox3D: true,
                IncludeSphere3D: true),
            new SnapshotConfiguration(
                14,
                IncludeCube: false,
                CubeEnabled: false,
                CubeX: 0,
                CubeColor: "blue",
                IncludeInputMotion: true),
            new SnapshotConfiguration(
                15,
                IncludeCube: false,
                CubeEnabled: false,
                CubeX: 0,
                CubeColor: "blue",
                SpriteAsset: "builtin://square"),
            new SnapshotConfiguration(
                16,
                IncludeCube: false,
                CubeEnabled: false,
                CubeX: 0,
                CubeColor: "blue",
                SpriteAsset: "builtin://circle"),
            new SnapshotConfiguration(
                17,
                IncludeCube: false,
                CubeEnabled: false,
                CubeX: 0,
                CubeColor: "blue",
                SpriteAsset: RuntimeSpriteAssetId,
                IncludeRuntimeSpriteAsset: true),
        };
        var paths = new Dictionary<int, string>();
        foreach (var configuration in configurations)
        {
            var path = Path.Combine(directory, $"snapshot-{configuration.Revision}.dpescene");
            File.WriteAllText(path, CreateSnapshot(configuration), new UTF8Encoding(false));
            paths[configuration.Revision] = path;
        }
        return paths;
    }

    private static string CreateSnapshot(SnapshotConfiguration configuration)
    {
        var runtimeAssetBytes = Convert.FromBase64String(CyanPngBase64);
        var runtimeAssetHash = Convert.ToHexString(SHA256.HashData(runtimeAssetBytes)).ToLowerInvariant();
        var runtimeAssets = configuration.IncludeRuntimeSpriteAsset
            ? $$"""
              "assets": [{
                "assetId": "{{RuntimeSpriteAssetId}}",
                "assetType": "sprite",
                "mediaType": "image/png",
                "contentHash": "{{runtimeAssetHash}}",
                "embeddedBytesBase64": "{{CyanPngBase64}}"
              }],
              """
            : string.Empty;
        var color = configuration.CubeColor switch
        {
            "green" => "{ \"r\": 0.1, \"g\": 0.95, \"b\": 0.2, \"a\": 1.0 }",
            "blue" => "{ \"r\": 0.15, \"g\": 0.3, \"b\": 1.0, \"a\": 1.0 }",
            _ => "{ \"r\": 1.0, \"g\": 0.15, \"b\": 0.1, \"a\": 1.0 }",
        };
        var box2D = configuration.IncludeBox2D
            ? """
                ,{
                  "typeId": "edbe79a3-4e97-4440-a23d-05c2d8faa1ca",
                  "qualifiedName": "DragonPixel.Native.BoxCollider2DComponent",
                  "schemaVersion": 1,
                  "owner": "native",
                  "enabled": true,
                  "properties": {
                    "dpe.physics2d.size": { "x": 2.0, "y": 1.5 },
                    "dpe.physics2d.offset": { "x": 0.15, "y": 0.0 },
                    "dpe.physics.sensor": false
                  }
                }
                """
            : string.Empty;
        var circle2D = configuration.IncludeCircle2D
            ? """
                ,{
                  "typeId": "aee65743-286c-431e-8aaf-321cac47eaa1",
                  "qualifiedName": "DragonPixel.Native.CircleCollider2DComponent",
                  "schemaVersion": 1,
                  "owner": "native",
                  "enabled": true,
                  "properties": {
                    "dpe.physics2d.radius": 0.85,
                    "dpe.physics2d.offset": { "x": -0.15, "y": 0.15 },
                    "dpe.physics.sensor": true
                  }
                }
                """
            : string.Empty;
        var box3D = configuration.IncludeBox3D
            ? """
                ,{
                  "typeId": "bfc9ff93-a892-4c1f-ad8f-f13d8bc1ba38",
                  "qualifiedName": "DragonPixel.Native.BoxCollider3DComponent",
                  "schemaVersion": 1,
                  "owner": "native",
                  "enabled": true,
                  "properties": {
                    "dpe.physics3d.size": { "x": 1.4, "y": 1.4, "z": 1.4 },
                    "dpe.physics3d.offset": { "x": 0.15, "y": 0.0, "z": 0.0 },
                    "dpe.physics.sensor": false
                  }
                }
                """
            : string.Empty;
        var sphere3D = configuration.IncludeSphere3D
            ? """
                ,{
                  "typeId": "11f84a3a-b568-4107-ad02-c53a86e50971",
                  "qualifiedName": "DragonPixel.Native.SphereCollider3DComponent",
                  "schemaVersion": 1,
                  "owner": "native",
                  "enabled": true,
                  "properties": {
                    "dpe.physics3d.radius": 0.7,
                    "dpe.physics3d.offset": { "x": 0.0, "y": 0.0, "z": 0.0 },
                    "dpe.physics.sensor": true
                  }
                }
                """
            : string.Empty;
        var inputMotion = configuration.IncludeInputMotion
            ? """
                ,{
                  "typeId": "64348aba-c5a4-42fc-86e6-e99f9640e36d",
                  "qualifiedName": "DragonPixel.Managed.InputMotion2D",
                  "schemaVersion": 1,
                  "owner": "managed",
                  "enabled": true,
                  "properties": {
                    "dpe.input.horizontal_action": "move.x",
                    "dpe.input.vertical_action": "move.y",
                    "dpe.input.speed": 4.0
                  }
                }
                """
            : string.Empty;
        var cube = configuration.IncludeCube
            ? $$"""
                ,{
                  "id": "{{CubeId}}",
                  "name": "POC E Cube",
                  "parentId": null,
                  "enabled": {{configuration.CubeEnabled.ToString().ToLowerInvariant()}},
                  "components": [
                    {
                      "typeId": "52e52fbd-ea15-40c5-bd9a-7dd320f7cd1e",
                      "qualifiedName": "DragonPixel.Native.TransformComponent",
                      "schemaVersion": 2,
                      "owner": "native",
                      "enabled": true,
                      "properties": {
                        "dpe.transform.position": { "x": {{configuration.CubeX}}, "y": 0.0, "z": 0.0 },
                        "dpe.transform.rotation": { "x": 0.0, "y": 0.0, "z": 0.0, "w": 1.0 },
                        "dpe.transform.scale": { "x": 1.5, "y": 1.5, "z": 1.5 }
                      }
                    },
                    {
                      "typeId": "9be44558-78e9-4912-bee5-046b5ad0a410",
                      "qualifiedName": "DragonPixel.Native.StaticMeshComponent",
                      "schemaVersion": 1,
                      "owner": "native",
                      "enabled": true,
                      "properties": { "dpe.mesh.asset": "builtin://unit-cube" }
                    },
                    {
                      "typeId": "90d93631-746a-4f52-9f95-4895e27edf51",
                      "qualifiedName": "DragonPixel.Native.MaterialComponent",
                      "schemaVersion": 1,
                      "owner": "native",
                      "enabled": true,
                      "properties": { "dpe.material.base_color": {{color}} }
                    }
                    {{box3D}}
                  ]
                }
                """
            : string.Empty;
        var colliderFixture = configuration.IncludeColliderFixture
            ? $$"""
                ,{
                  "id": "6d52666e-98c1-4079-b3f1-2255f36b3528",
                  "name": "POC E Sphere Collider",
                  "parentId": null,
                  "enabled": true,
                  "components": [
                    {
                      "typeId": "52e52fbd-ea15-40c5-bd9a-7dd320f7cd1e",
                      "qualifiedName": "DragonPixel.Native.TransformComponent",
                      "schemaVersion": 2,
                      "owner": "native",
                      "enabled": true,
                      "properties": {
                        "dpe.transform.position": { "x": 2.0, "y": 1.5, "z": 0.0 },
                        "dpe.transform.rotation": { "x": 0.0, "y": 0.0, "z": 0.0, "w": 1.0 },
                        "dpe.transform.scale": { "x": 1.0, "y": 1.0, "z": 1.0 }
                      }
                    }
                    {{sphere3D}}
                  ]
                }
                """
            : string.Empty;
        return $$"""
            {
              "$schema": "https://dragonpixel.dev/schemas/v2/scene.schema.json",
              "format": "dpe.scene",
              "formatVersion": 2,
              "engineVersion": "0.2.0-poc-e",
              "snapshotRevision": {{configuration.Revision}},
              {{runtimeAssets}}
              "sceneId": "af8ffc47-d69b-4ba9-886a-8b8876dd0ed1",
              "name": "POC E Scene",
              "entities": [
                {
                  "id": "{{SpriteId}}",
                  "name": "POC E Sprite",
                  "parentId": null,
                  "enabled": true,
                  "components": [
                    {
                      "typeId": "52e52fbd-ea15-40c5-bd9a-7dd320f7cd1e",
                      "qualifiedName": "DragonPixel.Native.TransformComponent",
                      "schemaVersion": 2,
                      "owner": "native",
                      "enabled": true,
                      "properties": {
                        "dpe.transform.position": { "x": -2.0, "y": 0.0, "z": 0.0 },
                        "dpe.transform.rotation": { "x": 0.0, "y": 0.0, "z": 0.0, "w": 1.0 },
                        "dpe.transform.scale": { "x": 1.0, "y": 1.0, "z": 1.0 }
                      }
                    },
                    {
                      "typeId": "b527395a-93a5-44f3-8d6c-7ea83a8568d1",
                      "qualifiedName": "DragonPixel.Native.SpriteComponent",
                      "schemaVersion": 1,
                      "owner": "native",
                      "enabled": true,
                      "properties": {
                        "dpe.sprite.asset": "{{configuration.SpriteAsset}}",
                        "dpe.sprite.color": { "r": 1.0, "g": 0.3, "b": 0.7, "a": 1.0 },
                        "dpe.sprite.layer": 0
                      }
                    }
                    {{box2D}}
                    {{circle2D}}
                    {{inputMotion}}
                  ]
                }
                {{cube}}
                {{colliderFixture}}
              ]
            }
            """;
    }

    private static JsonObject OrthographicCamera() => new()
    {
        ["orthographic"] = true,
        ["position"] = new JsonArray(0.0, 0.0, 10.0),
        ["target"] = new JsonArray(0.0, 0.0, 0.0),
        ["orthographicSize"] = 10.0,
        ["fieldOfViewDegrees"] = 60.0,
    };

    private static JsonObject RuntimeInputSnapshot(
        long inputRevision,
        float moveX = 0,
        long moveXPressCount = 0,
        long moveXReleaseCount = 0,
        float lookX = 0,
        long lookXPressCount = 0,
        long lookXReleaseCount = 0,
        bool focused = true,
        bool captured = true) => new()
    {
        ["inputRevision"] = inputRevision,
        ["focused"] = focused,
        ["captured"] = captured,
        ["actions"] = new JsonObject
        {
            ["move.x"] = RuntimeInputAction(
                "axis1d", moveX, moveXPressCount, moveXReleaseCount),
            ["move.y"] = RuntimeInputAction("axis1d", 0, 0, 0),
            ["jump"] = RuntimeInputAction("button", 0, 0, 0),
            ["look.x"] = RuntimeInputAction(
                "axis1d", lookX, lookXPressCount, lookXReleaseCount),
        },
    };

    private static JsonObject RuntimeInputAction(
        string kind,
        float value,
        long pressCount,
        long releaseCount) => new()
    {
        ["kind"] = kind,
        ["value"] = value,
        ["pressCount"] = pressCount,
        ["releaseCount"] = releaseCount,
    };

    private static async Task LoadSnapshotAsync(
        WorkerProcess worker,
        string path,
        long revision,
        bool reload)
    {
        var result = await worker.CallAsync(
            reload ? "reloadSnapshot" : "loadSnapshot",
            new JsonObject
            {
                ["snapshotPath"] = path,
                ["snapshotRevision"] = revision,
            });
        Assert(result["snapshotRevision"]!.GetValue<long>() == revision, "Snapshot revision was not acknowledged.");
    }

    private static async Task AssertPickAsync(
        WorkerProcess worker,
        int x,
        int y,
        long minimumFrameRevision,
        string? expectedEntityId)
    {
        var pick = await worker.CallAsync(
            "pick",
            new JsonObject
            {
                ["x"] = x,
                ["y"] = y,
                ["minimumFrameRevision"] = minimumFrameRevision,
            });
        var actual = pick["entityId"]?.GetValue<string>();
        Assert(actual == expectedEntityId,
            $"Picking ({x}, {y}) returned {actual ?? "nothing"}; expected {expectedEntityId ?? "nothing"}.");
    }

    private static async Task<int?> FindPickXAsync(
        WorkerProcess worker,
        int minimumX,
        int maximumX,
        int step,
        int y,
        long minimumFrameRevision,
        string expectedEntityId)
    {
        for (var x = minimumX; x <= maximumX; x += step)
        {
            var pick = await worker.CallAsync(
                "pick",
                new JsonObject
                {
                    ["x"] = x,
                    ["y"] = y,
                    ["minimumFrameRevision"] = minimumFrameRevision,
                });
            if (pick["entityId"]?.GetValue<string>() == expectedEntityId)
            {
                return x;
            }
        }
        return null;
    }

    private static async Task AssertStalePickIsRejectedAsync(
        WorkerProcess worker,
        long retainedFrameRevision)
    {
        try
        {
            await worker.CallAsync(
                "pick",
                new JsonObject
                {
                    ["x"] = 496,
                    ["y"] = 360,
                    ["minimumFrameRevision"] = retainedFrameRevision,
                    ["snapshotRevision"] = 2,
                    ["cameraRevision"] = 1,
                    ["commandRevision"] = 1,
                });
            throw new InvalidOperationException("A pick against a stale retained snapshot was accepted.");
        }
        catch (InvalidOperationException exception) when (
            exception.Message.Contains("-32020", StringComparison.Ordinal))
        {
            // The worker must reject a target that predates the requested snapshot revision.
        }
    }

    private static async Task AssertPickTimeoutIsStructuredAsync(
        WorkerProcess worker,
        long unreachableFrameRevision)
    {
        try
        {
            await worker.CallAsync(
                "pick",
                new JsonObject
                {
                    ["x"] = 496,
                    ["y"] = 360,
                    ["minimumFrameRevision"] = unreachableFrameRevision,
                });
            throw new InvalidOperationException("An unreachable paused pick revision unexpectedly succeeded.");
        }
        catch (InvalidOperationException exception) when (
            exception.Message.Contains("-32021", StringComparison.Ordinal))
        {
            // The timed-out queue entry must be skipped so the next valid paused pick can complete.
        }
    }

    private static WorkerProcess StartWorker(
        string workerDll,
        string frameFile,
        string nativeLibrary,
        int frameVersion,
        int width,
        int height,
        string session = "preview")
    {
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
                     workerDll,
                      "--frame-file", frameFile,
                      "--native", nativeLibrary,
                     "--frame-version", frameVersion.ToString(System.Globalization.CultureInfo.InvariantCulture),
                     "--width", width.ToString(System.Globalization.CultureInfo.InvariantCulture),
                     "--height", height.ToString(System.Globalization.CultureInfo.InvariantCulture),
                      "--session", session,
                 })
        {
            startInfo.ArgumentList.Add(argument);
        }
        return new WorkerProcess(Process.Start(startInfo)
            ?? throw new InvalidOperationException("Could not launch worker."));
    }

    private static async Task<FrameSnapshot> ReadFrameAfterAsync(
        string path,
        long sequence,
        long minimumSnapshotRevision,
        TimeSpan timeout,
        long minimumCameraRevision = 0,
        long minimumInputRevision = 0)
    {
        var stopwatch = Stopwatch.StartNew();
        Exception? lastError = null;
        SharedFrameReader? reader = null;
        try
        {
            while (stopwatch.Elapsed < timeout)
            {
                try
                {
                    reader ??= new SharedFrameReader(path);
                    var header = reader.ReadHeader();
                    if (header.Sequence > sequence
                        && header.SnapshotRevision >= minimumSnapshotRevision
                        && header.CameraRevision >= minimumCameraRevision
                        && header.InputRevision >= minimumInputRevision)
                    {
                        var frame = reader.ReadFrame();
                        if (frame.Sequence > sequence
                            && frame.SnapshotRevision >= minimumSnapshotRevision
                            && frame.CameraRevision >= minimumCameraRevision
                            && frame.InputRevision >= minimumInputRevision)
                        {
                            return frame;
                        }
                    }
                }
                catch (IOException exception)
                {
                    lastError = exception;
                }
                await Task.Delay(1);
            }
        }
        finally
        {
            reader?.Dispose();
        }
        throw new TimeoutException(
            $"No new stable frame for snapshot revision {minimumSnapshotRevision} appeared in {timeout}.",
            lastError);
    }

    private static async Task<FrameHeader> ReadFrameForCommandRevisionAsync(
        SharedFrameReader reader,
        long commandRevision,
        TimeSpan timeout)
    {
        var stopwatch = Stopwatch.StartNew();
        Exception? lastError = null;
        while (stopwatch.Elapsed < timeout)
        {
            try
            {
                var header = reader.ReadHeader();
                if (header.CommandRevision >= commandRevision)
                {
                    return header;
                }
            }
            catch (IOException exception)
            {
                lastError = exception;
            }
            await Task.Delay(1);
        }
        throw new TimeoutException(
            $"No stable frame correlated viewport command revision {commandRevision} in {timeout}.",
            lastError);
    }

    private static async Task<FrameHeader> ReadFrameHeaderAfterAsync(
        SharedFrameReader reader,
        long sequence,
        long minimumSnapshotRevision,
        TimeSpan timeout,
        long minimumCameraRevision = 0)
    {
        var stopwatch = Stopwatch.StartNew();
        Exception? lastError = null;
        while (stopwatch.Elapsed < timeout)
        {
            try
            {
                var header = reader.ReadHeader();
                if (header.Sequence > sequence
                    && header.SnapshotRevision >= minimumSnapshotRevision
                    && header.CameraRevision >= minimumCameraRevision)
                {
                    return header;
                }
            }
            catch (IOException exception)
            {
                lastError = exception;
            }
            await Task.Delay(1);
        }
        throw new TimeoutException(
            $"No new stable frame header for snapshot revision {minimumSnapshotRevision} appeared in {timeout}.",
            lastError);
    }

    private static void Assert(bool condition, string message)
    {
        if (!condition)
        {
            throw new InvalidOperationException(message);
        }
    }

    private static async Task AssertRpcRejectedAsync(
        Func<Task<JsonObject>> operation,
        string expectedCode,
        string failureMessage)
    {
        try
        {
            await operation();
        }
        catch (InvalidOperationException exception) when (
            exception.Message.Contains(expectedCode, StringComparison.Ordinal))
        {
            return;
        }

        throw new InvalidOperationException(failureMessage);
    }

    private sealed record SnapshotConfiguration(
        int Revision,
        bool IncludeCube,
        bool CubeEnabled,
        float CubeX,
        string CubeColor,
        bool IncludeColliderFixture = false,
        bool IncludeBox2D = false,
        bool IncludeCircle2D = false,
        bool IncludeBox3D = false,
        bool IncludeSphere3D = false,
        bool IncludeInputMotion = false,
        string SpriteAsset = "builtin://checker",
        bool IncludeRuntimeSpriteAsset = false);

    private sealed record FrameSnapshot(
        int Version,
        int Width,
        int Height,
        int Stride,
        int PixelFormat,
        long Sequence,
        int ContentFlags,
        int DistinctColorEstimate,
        string PixelSha256,
        int SamplePixelBgra,
        long InputRevision,
        long FrameRevision,
        long SnapshotRevision,
        long CameraRevision,
        long CommandRevision);

    private sealed record AdapterRun(string Adapter, Exception? Exception);
    private sealed record FrameHeader(
        long Sequence,
        long TimestampTicks,
        long InputRevision,
        long FrameRevision,
        long SnapshotRevision,
        long CameraRevision,
        long CommandRevision);

    private sealed unsafe class SharedFrameReader : IDisposable
    {
        private readonly FileStream _stream;
        private readonly MemoryMappedFile _mapping;
        private readonly MemoryMappedViewAccessor _view;
        private readonly byte* _viewPointer;
        private byte[] _pixels = Array.Empty<byte>();

        public SharedFrameReader(string path)
        {
            _stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete);
            _mapping = MemoryMappedFile.CreateFromFile(
                _stream,
                mapName: null,
                capacity: 0,
                MemoryMappedFileAccess.Read,
                HandleInheritability.None,
                leaveOpen: true);
            _view = _mapping.CreateViewAccessor(0, 0, MemoryMappedFileAccess.Read);
            byte* viewPointer = null;
            _view.SafeMemoryMappedViewHandle.AcquirePointer(ref viewPointer);
            _viewPointer = viewPointer + _view.PointerOffset;
        }

        public FrameHeader ReadHeader()
        {
            var deadline = Stopwatch.GetTimestamp() + (Stopwatch.Frequency / 4);
            var spinner = new SpinWait();
            do
            {
                var sequenceBefore = _view.ReadInt64(24);
                if ((sequenceBefore & 1) != 0)
                {
                    spinner.SpinOnce();
                    continue;
                }
                Thread.MemoryBarrier();
                var magic = _view.ReadInt32(0);
                var version = _view.ReadInt32(4);
                var timestampTicks = _view.ReadInt64(32);
                var inputRevision = _view.ReadInt64(48);
                var frameRevision = version >= 2 ? _view.ReadInt64(56) : 0;
                var snapshotRevision = version >= 2 ? _view.ReadInt64(64) : 0;
                var cameraRevision = version >= 2 ? _view.ReadInt64(72) : 0;
                var commandRevision = version >= 2 ? _view.ReadInt64(80) : 0;
                Thread.MemoryBarrier();
                var sequenceAfter = _view.ReadInt64(24);
                if (sequenceBefore != sequenceAfter || (sequenceAfter & 1) != 0)
                {
                    spinner.SpinOnce();
                    continue;
                }
                if (magic != FrameMagic || version is not (1 or 2))
                {
                    throw new IOException("Shared frame header was invalid.");
                }
                return new FrameHeader(
                    sequenceAfter,
                    timestampTicks,
                    inputRevision,
                    frameRevision,
                    snapshotRevision,
                    cameraRevision,
                    commandRevision);
            } while (Stopwatch.GetTimestamp() < deadline);

            throw new IOException("Shared frame header remained unavailable while the writer was publishing.");
        }

        public FrameSnapshot ReadFrame()
        {
            var deadline = Stopwatch.GetTimestamp() + (Stopwatch.Frequency / 4);
            var spinner = new SpinWait();
            do
            {
                var sequenceBefore = _view.ReadInt64(24);
                if ((sequenceBefore & 1) != 0)
                {
                    spinner.SpinOnce();
                    continue;
                }

                Thread.MemoryBarrier();
                var magic = _view.ReadInt32(0);
                var version = _view.ReadInt32(4);
                var width = _view.ReadInt32(8);
                var height = _view.ReadInt32(12);
                var stride = _view.ReadInt32(16);
                var format = _view.ReadInt32(20);
                var contentFlags = _view.ReadInt32(40);
                var inputRevision = _view.ReadInt64(48);
                var frameRevision = version >= 2 ? _view.ReadInt64(56) : 0;
                var snapshotRevision = version >= 2 ? _view.ReadInt64(64) : 0;
                var cameraRevision = version >= 2 ? _view.ReadInt64(72) : 0;
                var commandRevision = version >= 2 ? _view.ReadInt64(80) : 0;
                var headerSize = version >= 2 ? Version2HeaderSize : Version1HeaderSize;
                if (magic != FrameMagic
                    || version is not (1 or 2)
                    || width <= 0
                    || height <= 0
                    || stride != checked(width * 4))
                {
                    throw new IOException("Shared frame header was invalid.");
                }

                var pixelLength = checked(stride * height);
                if ((long)headerSize + pixelLength > _view.Capacity)
                {
                    throw new IOException("Shared frame pixel payload exceeded the mapped capacity.");
                }
                if (_pixels.Length != pixelLength)
                {
                    _pixels = new byte[pixelLength];
                }
                new ReadOnlySpan<byte>(_viewPointer + headerSize, pixelLength).CopyTo(_pixels);
                Thread.MemoryBarrier();
                var sequenceAfter = _view.ReadInt64(24);
                if (sequenceBefore != sequenceAfter || (sequenceAfter & 1) != 0)
                {
                    spinner.SpinOnce();
                    continue;
                }

                var colors = new HashSet<int>();
                var samplingStep = Math.Max(4, pixelLength / 4096);
                samplingStep -= samplingStep % 4;
                for (var index = 0; index + 3 < pixelLength; index += samplingStep)
                {
                    colors.Add(BinaryPrimitives.ReadInt32LittleEndian(_pixels.AsSpan(index, 4)));
                }
                return new FrameSnapshot(
                    version,
                    width,
                    height,
                    stride,
                    format,
                    sequenceAfter,
                    contentFlags,
                    colors.Count,
                    Convert.ToHexString(SHA256.HashData(_pixels.AsSpan(0, pixelLength))),
                    BinaryPrimitives.ReadInt32LittleEndian(
                        _pixels.AsSpan((Math.Min(360, height - 1) * stride)
                            + (Math.Min(496, width - 1) * 4), 4)),
                    inputRevision,
                    frameRevision,
                    snapshotRevision,
                    cameraRevision,
                    commandRevision);
            } while (Stopwatch.GetTimestamp() < deadline);

            throw new IOException("Shared frame remained unavailable while the writer was publishing.");
        }

        public void Dispose()
        {
            _view.SafeMemoryMappedViewHandle.ReleasePointer();
            _view.Dispose();
            _mapping.Dispose();
            _stream.Dispose();
        }
    }

    private sealed class WorkerProcess : IDisposable
    {
        private readonly Process _process;
        private int _nextId;

        public WorkerProcess(Process process)
        {
            _process = process;
        }

        public int ProcessId => _process.Id;
        public int ExitCode => _process.ExitCode;

        public async Task<JsonObject> CallAsync(string method, JsonObject? parameters = null)
        {
            var request = new JsonObject
            {
                ["jsonrpc"] = "2.0",
                ["id"] = Interlocked.Increment(ref _nextId),
                ["method"] = method,
            };
            if (parameters is not null)
            {
                request["params"] = parameters;
            }
            var payload = JsonSerializer.SerializeToUtf8Bytes(request);
            var length = new byte[4];
            BinaryPrimitives.WriteInt32LittleEndian(length, payload.Length);
            await _process.StandardInput.BaseStream.WriteAsync(length);
            await _process.StandardInput.BaseStream.WriteAsync(payload);
            await _process.StandardInput.BaseStream.FlushAsync();

            await ReadExactlyAsync(_process.StandardOutput.BaseStream, length);
            var responseLength = BinaryPrimitives.ReadInt32LittleEndian(length);
            if (responseLength <= 0 || responseLength > 1024 * 1024)
            {
                throw new InvalidDataException($"Invalid response length: {responseLength}");
            }
            var responseBytes = new byte[responseLength];
            await ReadExactlyAsync(_process.StandardOutput.BaseStream, responseBytes);
            var response = JsonNode.Parse(responseBytes)?.AsObject()
                ?? throw new InvalidDataException("Worker response was not an object.");
            if (response["error"] is not null)
            {
                throw new InvalidOperationException(response["error"]!.ToJsonString());
            }
            return response["result"]?.AsObject()
                ?? throw new InvalidDataException("Worker response had no object result.");
        }

        public async Task WaitForExitAsync(TimeSpan timeout)
        {
            using var cancellation = new CancellationTokenSource(timeout);
            try
            {
                await _process.WaitForExitAsync(cancellation.Token);
            }
            catch (OperationCanceledException)
            {
                var stderr = await _process.StandardError.ReadToEndAsync(cancellationToken: CancellationToken.None);
                throw new TimeoutException($"Worker did not exit. stderr: {stderr}");
            }
        }

        public void Dispose()
        {
            if (!_process.HasExited)
            {
                _process.Kill(entireProcessTree: true);
                _process.WaitForExit();
            }
            _process.Dispose();
        }

        private static async Task ReadExactlyAsync(Stream stream, byte[] buffer)
        {
            var offset = 0;
            while (offset < buffer.Length)
            {
                var read = await stream.ReadAsync(buffer.AsMemory(offset));
                if (read == 0)
                {
                    throw new EndOfStreamException("Worker closed its response stream.");
                }
                offset += read;
            }
        }
    }
}
