using System.Buffers.Binary;
using System.Diagnostics;
using System.Security.Cryptography;
using System.Text.Json;
using System.Text.Json.Nodes;
using DragonPixel.Contracts;
using DragonPixel.NativeInterop;

namespace DragonPixel.Runtime;

public static class WorkerHost
{
    private const int MaximumMessageLength = 1024 * 1024;
    // A 64 Hz absolute lattice leaves a 32 FPS two-slot cadence for adapters whose synchronous
    // 1280x720 device readback exceeds one deadline. The acceptance threshold remains 30 FPS.
    private const int TargetFrameRate = 64;

    public static async Task<int> RunAsync(IFrameworkSceneAdapter adapter, string[] args)
    {
        try
        {
            var options = WorkerOptions.Parse(args);
            using var frameBuffer = new SharedFrameBuffer(
                options.FrameFile,
                options.Width,
                options.Height,
                options.FrameVersion);
            using var shutdown = new CancellationTokenSource();
            using var state = new WorkerState(
                options.Width,
                options.Height,
                options.SessionKind,
                options.NativeLibraryPath,
                options.ComponentModulesPath,
                options.ViewId);
            var commandTask = Task.Run(async () =>
            {
                ControlConnection? connection = null;
                try
                {
                    Stream input;
                    Stream output;
                    if (options.ControlEndpoint is not null)
                    {
                        connection = await ControlConnection.AcceptAsync(options.ControlEndpoint, shutdown.Token)
                            .ConfigureAwait(false);
                        input = connection.Stream;
                        output = connection.Stream;
                    }
                    else
                    {
                        input = Console.OpenStandardInput();
                        output = Console.OpenStandardOutput();
                    }
                    await CommandLoopAsync(
                            adapter,
                            frameBuffer,
                            state,
                            shutdown,
                            input,
                            output,
                            options.ControlEndpoint is not null,
                            options.SessionKind,
                            options.ViewId)
                        .ConfigureAwait(false);
                }
                finally
                {
                    if (connection is not null)
                    {
                        await connection.DisposeAsync().ConfigureAwait(false);
                    }
                    shutdown.Cancel();
                }
            });
            // SDL/Cocoa require graphics-device work on the process entry thread on macOS.
            // This calling thread is therefore the framework graphics thread; IPC stays asynchronous.
            RenderLoop(adapter, frameBuffer, state, shutdown.Token);
            await commandTask.ConfigureAwait(false);
            return 0;
        }
        catch (OperationCanceledException)
        {
            return 0;
        }
        catch (Exception exception)
        {
            Console.Error.WriteLine(exception);
            return 1;
        }
    }

    private static void RenderLoop(
        IFrameworkSceneAdapter adapter,
        SharedFrameBuffer frameBuffer,
        WorkerState state,
        CancellationToken cancellationToken)
    {
        var stopwatch = Stopwatch.StartNew();
        var tickSource = StopwatchTickSource.Instance;
        var framePacer = new FramePacer(tickSource, TargetFrameRate);
        try
        {
            while (!cancellationToken.IsCancellationRequested)
            {
                var iterationStartedAtTicks = framePacer.GetTimestamp();
                var frameProduced = false;
                if (state.Mode is RuntimeMode.Running or RuntimeMode.Paused)
                {
                    var request = state.CreateRenderRequest(stopwatch.Elapsed);
                    var adapterStart = Stopwatch.GetTimestamp();
                    var frame = adapter.RenderFrame(request);
                    var adapterTicks = Stopwatch.GetTimestamp() - adapterStart;
                    var publishStart = Stopwatch.GetTimestamp();
                    frameBuffer.Publish(
                        frame,
                        stopwatch.Elapsed.Ticks,
                        StringComparer.Ordinal.GetHashCode(adapter.Name));
                    var publishTicks = Stopwatch.GetTimestamp() - publishStart;
                    state.FramePublished(adapterTicks, publishTicks, frame);
                    frameProduced = true;
                }
                adapter.ServicePendingOperations();

                var pacing = framePacer.CompleteIteration(iterationStartedAtTicks, frameProduced);
                state.UpdateFramePacing(framePacer.Counters);

                if (!WaitUntil(pacing.DeadlineTicks, tickSource, cancellationToken))
                {
                    break;
                }
            }
        }
        catch (Exception exception)
        {
            state.SetRenderFault(exception);
            cancellationToken.WaitHandle.WaitOne();
        }
        finally
        {
            adapter.Dispose();
        }
    }

    private static bool WaitUntil(
        long deadline,
        IMonotonicTickSource tickSource,
        CancellationToken cancellationToken)
    {
        var finalSpinWindowTicks = Math.Max(1L, tickSource.Frequency / 1000L);
        while (!cancellationToken.IsCancellationRequested)
        {
            var remainingTicks = deadline - tickSource.GetTimestamp();
            if (remainingTicks <= 0)
            {
                return true;
            }

            if (remainingTicks > finalSpinWindowTicks)
            {
                var coarseTicks = remainingTicks - finalSpinWindowTicks;
                var coarseMilliseconds = Math.Clamp(
                    (int)(coarseTicks * 1000L / tickSource.Frequency),
                    1,
                    5);
                if (cancellationToken.WaitHandle.WaitOne(coarseMilliseconds))
                {
                    return false;
                }
                continue;
            }

            Thread.SpinWait(64);
        }

        return false;
    }

    private static async Task CommandLoopAsync(
        IFrameworkSceneAdapter adapter,
        SharedFrameBuffer frameBuffer,
        WorkerState state,
        CancellationTokenSource shutdown,
        Stream input,
        Stream output,
        bool requireCapabilityToken,
        string sessionKind,
        string viewId)
    {
        var capabilityToken = Convert.ToHexString(RandomNumberGenerator.GetBytes(24)).ToLowerInvariant();
        var negotiatedProtocolVersion = 0;
        while (!shutdown.IsCancellationRequested)
        {
            var request = await ReadMessageAsync(input, shutdown.Token).ConfigureAwait(false);
            if (request is null)
            {
                shutdown.Cancel();
                break;
            }

            var id = request["id"]?.DeepClone();
            var method = request["method"]?.GetValue<string>() ?? string.Empty;
            if (method == "handshake" && negotiatedProtocolVersion != 0)
            {
                await WriteErrorAsync(output, id, -32013, "The worker session handshake is already complete.")
                    .ConfigureAwait(false);
                continue;
            }
            if (method != "handshake" && negotiatedProtocolVersion == 0)
            {
                await WriteErrorAsync(output, id, -32013, "The first worker request must be handshake.")
                    .ConfigureAwait(false);
                continue;
            }
            if (requireCapabilityToken && method != "handshake"
                && request["capabilityToken"]?.GetValue<string>() != capabilityToken)
            {
                await WriteErrorAsync(output, id, -32001, "A valid capability token is required.")
                    .ConfigureAwait(false);
                continue;
            }
            if (request["params"]?["viewId"] is JsonValue requestedView
                && !StringComparer.Ordinal.Equals(requestedView.GetValue<string>(), viewId))
            {
                await WriteErrorAsync(output, id, -32012, "The request targets a different named view output.")
                    .ConfigureAwait(false);
                continue;
            }

            JsonNode? result;
            switch (method)
            {
                case "handshake":
                    var requestedVersion = request["params"]?["protocolVersion"]?.GetValue<int>() ?? 1;
                    if (requestedVersion is not (1 or 2))
                    {
                        await WriteErrorAsync(output, id, -32010, "Local protocol versions 1 and 2 are supported.")
                            .ConfigureAwait(false);
                        continue;
                    }
                    negotiatedProtocolVersion = requestedVersion;
                    result = new JsonObject
                    {
                        ["protocolVersion"] = requestedVersion,
                        ["frameLayoutVersion"] = frameBuffer.Version,
                        ["frameHeaderSize"] = frameBuffer.HeaderSize,
                        ["adapter"] = adapter.Name,
                        ["adapterVersion"] = adapter.Version,
                        ["experimental"] = adapter.Experimental,
                        ["pixelFormat"] = "BGRA8",
                        ["frameTransport"] = "memory-mapped-file-seqlock",
                        ["revisionCorrelatedFrames"] = true,
                        ["runtimeInput"] = requestedVersion >= 2 && sessionKind == "play",
                        ["sceneDrivenGraphics"] = true,
                        ["nativePhysics"] = state.PhysicsAvailable,
                        ["projectComponentModules"] = requestedVersion >= 2,
                        ["simulatePreview"] = requestedVersion >= 2 && sessionKind == "preview",
                        ["idBufferPicking"] = requestedVersion >= 2,
                        ["viewportResize"] = requestedVersion >= 2,
                        ["backend"] = adapter.Backend,
                        ["device"] = adapter.Device,
                        ["capabilityToken"] = capabilityToken,
                        ["sessionKind"] = sessionKind,
                        ["viewId"] = viewId,
                        ["viewPurpose"] = sessionKind,
                        ["processId"] = Environment.ProcessId,
                    };
                    break;
                case "initialize":
                    state.StopAndDiscard();
                    result = new JsonObject { ["state"] = "stopped" };
                    break;
                case "loadSnapshot":
                case "reloadSnapshot":
                    try
                    {
                        result = LoadSnapshot(request, state);
                    }
                    catch (Exception exception) when (
                        exception is IOException or JsonException or InvalidDataException
                        or InvalidOperationException or NotSupportedException)
                    {
                        await WriteErrorAsync(output, id, -32002, exception.Message).ConfigureAwait(false);
                        continue;
                    }
                    break;
                case "play":
                case "resume":
                    state.StartOrResume();
                    result = new JsonObject { ["state"] = "running" };
                    break;
                case "simulatePreview":
                case "simulate-preview":
                    try
                    {
                        var enabled = request["params"]?["enabled"]?.GetValue<bool>() ?? true;
                        state.SetPreviewSimulation(enabled);
                        result = new JsonObject
                        {
                            ["state"] = state.Mode.ToString().ToLowerInvariant(),
                            ["enabled"] = enabled,
                            ["physics"] = state.CreatePhysicsDiagnostics(includeTransforms: false),
                        };
                    }
                    catch (Exception exception) when (
                        exception is ArgumentException or InvalidOperationException
                        or NotSupportedException)
                    {
                        await WriteErrorAsync(output, id, -32602, exception.Message).ConfigureAwait(false);
                        continue;
                    }
                    break;
                case "viewportInput":
                    try
                    {
                        var inputRevision = request["params"]?["inputRevision"]?.GetValue<long>() ?? 0;
                        state.UpdateViewport(request["params"] as JsonObject, inputRevision, requireNewInput: true);
                        result = new JsonObject
                        {
                            ["inputRevision"] = inputRevision,
                            ["cameraRevision"] = state.CameraRevision,
                            ["commandRevision"] = state.CommandRevision,
                        };
                    }
                    catch (Exception exception) when (
                        exception is ArgumentException or InvalidOperationException
                        or JsonException or OverflowException)
                    {
                        await WriteErrorAsync(output, id, -32602, exception.Message).ConfigureAwait(false);
                        continue;
                    }
                    break;
                case "runtimeInput":
                    if (negotiatedProtocolVersion < 2 || sessionKind != "play")
                    {
                        await WriteErrorAsync(output, id, -32011,
                            "runtimeInput was not negotiated for this worker session.")
                            .ConfigureAwait(false);
                        continue;
                    }
                    try
                    {
                        var actionRevision = request["params"]?["inputRevision"]?.GetValue<long>() ?? 0;
                        state.UpdateRuntimeInput(request["params"] as JsonObject, actionRevision);
                        result = new JsonObject
                        {
                            ["inputRevision"] = actionRevision,
                            ["neutral"] = state.InputIsNeutral,
                        };
                    }
                    catch (Exception exception) when (
                        exception is ArgumentException or InvalidOperationException
                        or JsonException or OverflowException)
                    {
                        await WriteErrorAsync(output, id, -32602, exception.Message).ConfigureAwait(false);
                        continue;
                    }
                    break;
                case "inputActions":
                    if (sessionKind != "play")
                    {
                        await WriteErrorAsync(output, id, -32011,
                            "Runtime action input is available only to an isolated Play session.")
                            .ConfigureAwait(false);
                        continue;
                    }
                    try
                    {
                        var actionRevision = request["params"]?["inputRevision"]?.GetValue<long>() ?? 0;
                        state.UpdateInputActions(request["params"]?["actions"] as JsonObject, actionRevision);
                        result = new JsonObject
                        {
                            ["inputRevision"] = actionRevision,
                            ["neutral"] = request["params"]?["actions"] is not JsonObject actions || actions.Count == 0,
                        };
                    }
                    catch (ArgumentException exception)
                    {
                        await WriteErrorAsync(output, id, -32602, exception.Message).ConfigureAwait(false);
                        continue;
                    }
                    break;
                case "setViewport":
                case "resizeViewport":
                    try
                    {
                        state.UpdateViewport(request["params"] as JsonObject, 0, requireNewInput: false);
                        result = new JsonObject
                        {
                            ["width"] = state.ViewportWidth,
                            ["height"] = state.ViewportHeight,
                            ["cameraRevision"] = state.CameraRevision,
                            ["commandRevision"] = state.CommandRevision,
                        };
                    }
                    catch (Exception exception) when (
                        exception is ArgumentException or InvalidOperationException
                        or JsonException or OverflowException)
                    {
                        await WriteErrorAsync(output, id, -32602, exception.Message).ConfigureAwait(false);
                        continue;
                    }
                    break;
                case "pick":
                    var x = request["params"]?["x"]?.GetValue<int>() ?? -1;
                    var y = request["params"]?["y"]?.GetValue<int>() ?? -1;
                    var minimumFrameRevision = request["params"]?["minimumFrameRevision"]?.GetValue<long>() ?? 0;
                    var expectedSnapshotRevision = request["params"]?["snapshotRevision"]?.GetValue<long>() ?? 0;
                    var expectedCameraRevision = request["params"]?["cameraRevision"]?.GetValue<long>() ?? 0;
                    var expectedCommandRevision = request["params"]?["commandRevision"]?.GetValue<long>() ?? 0;
                    FrameworkPickResult pick;
                    try
                    {
                        pick = adapter.Pick(x, y, minimumFrameRevision);
                    }
                    catch (TimeoutException exception)
                    {
                        await WriteErrorAsync(output, id, -32021, exception.Message).ConfigureAwait(false);
                        continue;
                    }
                    if (pick.SnapshotRevision < expectedSnapshotRevision
                        || pick.CameraRevision < expectedCameraRevision
                        || pick.CommandRevision < expectedCommandRevision)
                    {
                        await WriteErrorAsync(
                            output,
                            id,
                            -32020,
                            "The retained picking target is older than the requested viewport revisions.")
                            .ConfigureAwait(false);
                        continue;
                    }
                    result = new JsonObject
                    {
                        ["entityId"] = pick.EntityId,
                        ["frameRevision"] = pick.FrameRevision,
                        ["snapshotRevision"] = pick.SnapshotRevision,
                        ["cameraRevision"] = pick.CameraRevision,
                        ["commandRevision"] = pick.CommandRevision,
                    };
                    break;
                case "pause":
                    state.Pause();
                    result = new JsonObject { ["state"] = "paused" };
                    break;
                case "stop":
                    state.StopAndDiscard();
                    result = new JsonObject { ["state"] = "stopped" };
                    break;
                case "diagnostics":
                    result = CreateDiagnostics(adapter, state);
                    break;
                case "subscribeDiagnostics":
                    result = new JsonObject { ["subscriptionId"] = Guid.NewGuid().ToString("D") };
                    break;
                case "inspect":
                    result = new JsonObject
                    {
                        ["state"] = state.Mode.ToString().ToLowerInvariant(),
                        ["snapshotSha256"] = state.SnapshotHash,
                        ["snapshotRevision"] = state.SnapshotRevision,
                        ["adapter"] = adapter.Name,
                        ["backend"] = adapter.Backend,
                        ["device"] = adapter.Device,
                        ["physics"] = state.CreatePhysicsDiagnostics(includeTransforms: true),
                    };
                    break;
                case "applyCommand":
                    result = new JsonObject
                    {
                        ["applied"] = false,
                        ["reason"] = "The play snapshot is isolated and runtime commands are non-authoritative.",
                    };
                    break;
                case "cancel":
                    result = new JsonObject
                    {
                        ["cancelled"] = true,
                        ["requestId"] = request["params"]?["requestId"]?.DeepClone(),
                    };
                    break;
                case "crash":
                    await WriteResponseAsync(output, id, new JsonObject { ["crashing"] = true })
                        .ConfigureAwait(false);
                    Environment.Exit(86);
                    return;
                case "shutdown":
                    state.StopAndDiscard();
                    result = new JsonObject { ["state"] = "shutdown" };
                    await WriteResponseAsync(output, id, result).ConfigureAwait(false);
                    shutdown.Cancel();
                    return;
                default:
                    await WriteErrorAsync(output, id, -32601, $"Unknown method: {method}").ConfigureAwait(false);
                    continue;
            }

            await WriteResponseAsync(output, id, result).ConfigureAwait(false);
        }
    }

    private static JsonObject LoadSnapshot(JsonObject request, WorkerState state)
    {
        var snapshotPath = request["params"]?["snapshotPath"]?.GetValue<string>();
        if (string.IsNullOrWhiteSpace(snapshotPath))
        {
            throw new InvalidDataException("loadSnapshot requires params.snapshotPath.");
        }
        var requestedRevision = request["params"]?["snapshotRevision"]?.GetValue<long>() ?? 0;
        var parsed = state.ParseSnapshot(snapshotPath, requestedRevision);
        state.ReplaceSnapshot(parsed, snapshotPath);
        return new JsonObject
        {
            ["state"] = state.Mode.ToString().ToLowerInvariant(),
            ["snapshotSha256"] = parsed.Sha256,
            ["snapshotRevision"] = parsed.Scene.SnapshotRevision,
            ["entities"] = parsed.Scene.Entities.Count,
            ["physicsBodies"] = parsed.Physics.Bodies.Count,
            ["physicsWorldLoaded"] = state.PhysicsWorldLoaded,
        };
    }

    private static JsonObject CreateDiagnostics(IFrameworkSceneAdapter adapter, WorkerState state)
    {
        var sceneDiagnostics = new JsonArray();
        var framePacing = state.FramePacing;
        foreach (var diagnostic in state.SceneDiagnostics)
        {
            sceneDiagnostics.Add(diagnostic);
        }
        return new JsonObject
        {
            ["state"] = state.Mode.ToString().ToLowerInvariant(),
            ["frames"] = state.FrameCount,
            ["processId"] = Environment.ProcessId,
            ["snapshotSha256"] = state.SnapshotHash,
            ["snapshotRevision"] = state.SnapshotRevision,
            ["cameraRevision"] = state.CameraRevision,
            ["commandRevision"] = state.CommandRevision,
            ["frameRevision"] = state.PresentedFrameRevision,
            ["inputRevision"] = state.InputRevision,
            ["viewportInputRevision"] = state.ViewportInputRevision,
            ["presentedInputRevision"] = state.PresentedInputRevision,
            ["backend"] = state.LastBackend.Length == 0 ? adapter.Backend : state.LastBackend,
            ["device"] = state.LastDevice.Length == 0 ? adapter.Device : state.LastDevice,
            ["renderFault"] = state.RenderFault,
            ["viewId"] = state.ViewId,
            ["sceneDiagnostics"] = sceneDiagnostics,
            ["projectComponents"] = new JsonObject
            {
                ["factories"] = state.ProjectComponentFactoryCount,
                ["instances"] = state.ProjectComponentInstanceCount,
                ["diagnostics"] = new JsonArray(state.ProjectComponentDiagnostics
                    .Select(value => (JsonNode?)JsonValue.Create(value)).ToArray()),
            },
            ["framePacing"] = CreateFramePacingDiagnostics(framePacing),
            ["physics"] = state.CreatePhysicsDiagnostics(includeTransforms: false),
            ["lastFrameTimingsMs"] = new JsonObject
            {
                ["adapter"] = ToMilliseconds(state.AdapterTicks),
                ["render"] = ToMilliseconds(state.AdapterTicks),
                ["publish"] = ToMilliseconds(state.PublishTicks),
            },
        };
    }

    internal static JsonObject CreateFramePacingDiagnostics(FramePacingCounters framePacing) => new()
    {
        ["strategy"] = "origin-derived-skip-missed",
        ["targetFramesPerSecond"] = TargetFrameRate,
        ["clockFrequency"] = Stopwatch.Frequency,
        ["lateFrames"] = framePacing.LateFrames,
        ["droppedFrames"] = framePacing.DroppedFrames,
        ["overrunFrames"] = framePacing.OverrunFrames,
    };

    private static async Task<JsonObject?> ReadMessageAsync(Stream input, CancellationToken cancellationToken)
    {
        var lengthBytes = new byte[4];
        var firstRead = await input.ReadAsync(lengthBytes.AsMemory(0, 4), cancellationToken).ConfigureAwait(false);
        if (firstRead == 0)
        {
            return null;
        }

        await ReadRemainingAsync(input, lengthBytes, firstRead, cancellationToken).ConfigureAwait(false);
        var length = BinaryPrimitives.ReadInt32LittleEndian(lengthBytes);
        if (length <= 0 || length > MaximumMessageLength)
        {
            throw new InvalidDataException($"Invalid command length: {length}");
        }

        var payload = new byte[length];
        await ReadRemainingAsync(input, payload, 0, cancellationToken).ConfigureAwait(false);
        return JsonNode.Parse(payload)?.AsObject()
            ?? throw new InvalidDataException("Command root must be an object.");
    }

    private static async Task ReadRemainingAsync(
        Stream input,
        byte[] buffer,
        int offset,
        CancellationToken cancellationToken)
    {
        while (offset < buffer.Length)
        {
            var read = await input.ReadAsync(buffer.AsMemory(offset), cancellationToken).ConfigureAwait(false);
            if (read == 0)
            {
                throw new EndOfStreamException("Command stream closed in a framed message.");
            }
            offset += read;
        }
    }

    private static Task WriteResponseAsync(Stream output, JsonNode? id, JsonNode? result) =>
        WriteMessageAsync(output, new JsonObject
        {
            ["jsonrpc"] = "2.0",
            ["id"] = id,
            ["result"] = result,
        });

    private static Task WriteErrorAsync(Stream output, JsonNode? id, int code, string message) =>
        WriteMessageAsync(output, new JsonObject
        {
            ["jsonrpc"] = "2.0",
            ["id"] = id,
            ["error"] = new JsonObject { ["code"] = code, ["message"] = message },
        });

    private static async Task WriteMessageAsync(Stream output, JsonObject message)
    {
        var payload = JsonSerializer.SerializeToUtf8Bytes(message);
        var length = new byte[4];
        BinaryPrimitives.WriteInt32LittleEndian(length, payload.Length);
        await output.WriteAsync(length).ConfigureAwait(false);
        await output.WriteAsync(payload).ConfigureAwait(false);
        await output.FlushAsync().ConfigureAwait(false);
    }

    private static double ToMilliseconds(long stopwatchTicks) =>
        stopwatchTicks * 1000.0 / Stopwatch.Frequency;

    private enum RuntimeMode
    {
        Stopped,
        Running,
        Paused,
        Faulted,
    }

    private sealed class WorkerState : IDisposable
    {
        private readonly object _gate = new();
        private readonly int _maximumWidth;
        private readonly int _maximumHeight;
        private readonly bool _useSceneCamera;
        private readonly bool _playSimulation;
        private readonly string _viewId;
        private readonly WorkerPhysicsRuntime? _physicsRuntime;
        private readonly ProjectComponentRuntime? _projectComponents;
        private int _mode;
        private int _viewportWidth;
        private int _viewportHeight;
        private long _frameCount;
        private long _nextFrameRevision;
        private long _adapterTicks;
        private long _publishTicks;
        private long _inputRevision;
        private long _viewportInputRevision;
        private long _presentedInputRevision;
        private long _presentedFrameRevision;
        private long _cameraRevision;
        private long _commandRevision;
        private string _snapshotHash = string.Empty;
        private string _lastBackend = string.Empty;
        private string _lastDevice = string.Empty;
        private string _renderFault = string.Empty;
        private RenderScene _authoringScene = RenderScene.Empty;
        private RenderScene _scene = RenderScene.Empty;
        private ParsedPhysicsSnapshot? _physicsSnapshot;
        private WorkerPhysicsWorld? _physicsWorld;
        private TimeSpan _simulationElapsed;
        private TimeSpan? _lastRenderWallElapsed;
        private bool _previewSimulation;
        private NativePhysicsStepResult _lastPhysicsStep;
        private EditorCameraState _editorCamera = EditorCameraState.Default;
        private IReadOnlyList<string> _selection = Array.Empty<string>();
        private IReadOnlyDictionary<string, float> _inputActions = new Dictionary<string, float>();
        private Dictionary<string, RuntimeInputActionState> _runtimeInputActionStates =
            new(StringComparer.Ordinal);
        private RuntimeInputSnapshot _runtimeInputSnapshot = RuntimeInputSnapshot.Neutral;
        private bool _hasRuntimeInputState;
        private FramePacingCounters _framePacing = FramePacingCounters.Empty;
        private bool _disposed;

        public WorkerState(
            int maximumWidth,
            int maximumHeight,
            string sessionKind,
            string? nativeLibraryPath,
            string? componentModulesPath,
            string viewId)
        {
            _maximumWidth = maximumWidth;
            _maximumHeight = maximumHeight;
            _viewportWidth = maximumWidth;
            _viewportHeight = maximumHeight;
            _useSceneCamera = sessionKind is "play" or "game-preview";
            _playSimulation = sessionKind == "play";
            _viewId = viewId;
            if (!string.IsNullOrWhiteSpace(nativeLibraryPath))
            {
                _physicsRuntime = new WorkerPhysicsRuntime(Path.GetFullPath(nativeLibraryPath));
            }
            if (!string.IsNullOrWhiteSpace(componentModulesPath))
            {
                _projectComponents = new ProjectComponentRuntime(Path.GetFullPath(componentModulesPath));
            }
        }

        public RuntimeMode Mode => (RuntimeMode)Volatile.Read(ref _mode);
        public int ViewportWidth => Volatile.Read(ref _viewportWidth);
        public int ViewportHeight => Volatile.Read(ref _viewportHeight);
        public long FrameCount => Interlocked.Read(ref _frameCount);
        public long AdapterTicks => Interlocked.Read(ref _adapterTicks);
        public long PublishTicks => Interlocked.Read(ref _publishTicks);
        public long InputRevision => Interlocked.Read(ref _inputRevision);
        public long ViewportInputRevision => Interlocked.Read(ref _viewportInputRevision);
        public bool InputIsNeutral
        {
            get
            {
                lock (_gate)
                {
                    return _inputActions.Values.All(static value => value == 0);
                }
            }
        }
        public long PresentedInputRevision => Interlocked.Read(ref _presentedInputRevision);
        public long PresentedFrameRevision => Interlocked.Read(ref _presentedFrameRevision);
        public long CameraRevision => Interlocked.Read(ref _cameraRevision);
        public long CommandRevision => Interlocked.Read(ref _commandRevision);
        public long SnapshotRevision => Volatile.Read(ref _scene).SnapshotRevision;
        public string SnapshotHash => Volatile.Read(ref _snapshotHash);
        public string LastBackend => Volatile.Read(ref _lastBackend);
        public string LastDevice => Volatile.Read(ref _lastDevice);
        public string RenderFault => Volatile.Read(ref _renderFault);
        public IReadOnlyList<string> SceneDiagnostics => Volatile.Read(ref _scene).Diagnostics;
        public bool PhysicsAvailable => _physicsRuntime is not null;
        public string ViewId => _viewId;
        public int ProjectComponentFactoryCount => _projectComponents?.FactoryCount ?? 0;
        public int ProjectComponentInstanceCount => _projectComponents?.InstanceCount ?? 0;
        public IReadOnlyList<string> ProjectComponentDiagnostics =>
            _projectComponents?.Diagnostics ?? Array.Empty<string>();
        public FramePacingCounters FramePacing => Volatile.Read(ref _framePacing);
        public bool PhysicsWorldLoaded
        {
            get
            {
                lock (_gate)
                {
                    return _physicsWorld is not null;
                }
            }
        }

        private void SetMode(RuntimeMode mode) => Volatile.Write(ref _mode, (int)mode);

        public FrameworkRenderRequest CreateRenderRequest(TimeSpan elapsed)
        {
            lock (_gate)
            {
                var delta = Mode == RuntimeMode.Running && _lastRenderWallElapsed.HasValue
                    ? elapsed - _lastRenderWallElapsed.Value
                    : TimeSpan.Zero;
                _lastRenderWallElapsed = elapsed;
                if (Mode == RuntimeMode.Running)
                {
                    _simulationElapsed += delta;
                    _projectComponents?.Update(
                        _simulationElapsed,
                        delta,
                        _inputActions,
                        _runtimeInputSnapshot);
                    var runtimeScene = _authoringScene;
                    if (_physicsWorld is not null && ShouldSimulate())
                    {
                        _lastPhysicsStep = _physicsWorld.Advance(delta);
                        runtimeScene = _physicsWorld.ApplyTransforms(runtimeScene);
                    }
                    _scene = _projectComponents?.ApplyTransforms(runtimeScene) ?? runtimeScene;
                }
                return new FrameworkRenderRequest(
                    _scene,
                    _viewportWidth,
                    _viewportHeight,
                    _simulationElapsed,
                    _useSceneCamera,
                    _editorCamera,
                    _selection,
                    _scene.SnapshotRevision,
                    _cameraRevision,
                    _commandRevision,
                    _inputRevision,
                    _inputActions,
                    Interlocked.Increment(ref _nextFrameRevision));
            }
        }

        public void FramePublished(long adapterTicks, long publishTicks, FrameworkFrame frame)
        {
            Interlocked.Exchange(ref _adapterTicks, adapterTicks);
            Interlocked.Exchange(ref _publishTicks, publishTicks);
            Interlocked.Exchange(ref _presentedInputRevision, frame.InputRevision);
            Interlocked.Exchange(ref _presentedFrameRevision, frame.FrameRevision);
            Volatile.Write(ref _lastBackend, frame.Backend);
            Volatile.Write(ref _lastDevice, frame.Device);
            Interlocked.Increment(ref _frameCount);
        }

        public void UpdateFramePacing(FramePacingCounters counters) =>
            Volatile.Write(ref _framePacing, counters);

        public ParsedSceneSnapshot ParseSnapshot(string path, long requestedRevision) =>
            SceneSnapshotParser.Parse(path, requestedRevision, _projectComponents);

        public void ReplaceSnapshot(ParsedSceneSnapshot parsed, string snapshotPath)
        {
            lock (_gate)
            {
                if (parsed.Scene.SnapshotRevision <= _authoringScene.SnapshotRevision
                    && _authoringScene.SnapshotRevision != 0)
                {
                    throw new InvalidDataException(
                        "Snapshot revisions must increase for an in-place atomic reload.");
                }

                WorkerPhysicsWorld? candidate = null;
                if (_physicsRuntime is not null)
                {
                    candidate = _physicsRuntime.CreateWorld(parsed.Physics);
                }
                else if (parsed.Physics.Bodies.Count != 0)
                {
                    throw new InvalidOperationException(
                        "The snapshot contains physics components, but the worker was not launched with --native.");
                }

                _projectComponents?.ReloadSnapshot(snapshotPath, parsed.Scene);

                var previous = _physicsWorld;
                _physicsWorld = candidate;
                _physicsSnapshot = parsed.Physics;
                _authoringScene = parsed.Scene;
                _scene = parsed.Scene;
                _lastRenderWallElapsed = null;
                _lastPhysicsStep = default;
                Volatile.Write(ref _snapshotHash, parsed.Sha256);
                previous?.Dispose();
            }
        }

        public void StartOrResume()
        {
            lock (_gate)
            {
                ObjectDisposedException.ThrowIf(_disposed, this);
                EnsurePhysicsWorld();
                if (Mode == RuntimeMode.Stopped)
                {
                    _previewSimulation = false;
                    _scene = _authoringScene;
                    _simulationElapsed = TimeSpan.Zero;
                }
                _lastRenderWallElapsed = null;
                SetMode(RuntimeMode.Running);
            }
        }

        public void SetPreviewSimulation(bool enabled)
        {
            lock (_gate)
            {
                ObjectDisposedException.ThrowIf(_disposed, this);
                if (_useSceneCamera)
                {
                    throw new ArgumentException(
                        "simulatePreview is available only in an editor preview worker.");
                }
                if (_physicsRuntime is null)
                {
                    throw new NotSupportedException(
                        "The worker was not launched with a native physics runtime.");
                }
                if (_physicsSnapshot is null)
                {
                    throw new InvalidOperationException(
                        "A scene snapshot must be loaded before preview simulation starts.");
                }

                if (enabled)
                {
                    EnsurePhysicsWorld();
                    _previewSimulation = true;
                }
                else
                {
                    var candidate = _physicsRuntime.CreateWorld(_physicsSnapshot);
                    var previous = _physicsWorld;
                    _physicsWorld = candidate;
                    previous?.Dispose();
                    _previewSimulation = false;
                    _scene = _authoringScene;
                    _lastPhysicsStep = default;
                }
                _lastRenderWallElapsed = null;
                SetMode(RuntimeMode.Running);
            }
        }

        public void Pause()
        {
            lock (_gate)
            {
                if (Mode == RuntimeMode.Running)
                {
                    SetMode(RuntimeMode.Paused);
                }
            }
        }

        public void StopAndDiscard()
        {
            lock (_gate)
            {
                _physicsWorld?.Dispose();
                _physicsWorld = null;
                _previewSimulation = false;
                _simulationElapsed = TimeSpan.Zero;
                _lastRenderWallElapsed = null;
                _lastPhysicsStep = default;
                _inputActions = new Dictionary<string, float>();
                _runtimeInputActionStates.Clear();
                _hasRuntimeInputState = false;
                _runtimeInputSnapshot = RuntimeInputSnapshot.Neutral;
                _projectComponents?.ResetRuntimeTransforms();
                _scene = _authoringScene;
                SetMode(RuntimeMode.Stopped);
            }
        }

        public JsonObject CreatePhysicsDiagnostics(bool includeTransforms)
        {
            lock (_gate)
            {
                var simulationEnabled = Mode == RuntimeMode.Running && ShouldSimulate();
                var result = new JsonObject
                {
                    ["available"] = PhysicsAvailable,
                    ["worldLoaded"] = _physicsWorld is not null,
                    ["bodyCount"] = _physicsSnapshot?.Bodies.Count ?? 0,
                    ["simulationEnabled"] = simulationEnabled,
                    ["simulationMode"] = _playSimulation
                        ? "play"
                        : _previewSimulation ? "simulate-preview" : _useSceneCamera ? "game-preview" : "edit",
                    ["ticks"] = _physicsWorld is null ? 0L : checked((long)_physicsWorld.TotalTicks),
                    ["worldTick"] = _physicsWorld is null ? 0L : checked((long)_physicsWorld.WorldTick),
                    ["droppedSeconds"] = _physicsWorld?.DroppedSeconds ?? 0,
                    ["contacts"] = _physicsWorld?.ContactCount ?? 0,
                    ["lastStepTicks"] = _lastPhysicsStep.Ticks,
                };
                if (!includeTransforms)
                {
                    return result;
                }

                var transforms = new JsonArray();
                if (_physicsWorld is not null && _physicsSnapshot is not null)
                {
                    var bodyIds = _physicsSnapshot.Bodies
                        .Select(static body => body.EntityId.ToString())
                        .ToHashSet(StringComparer.Ordinal);
                    foreach (var entity in _scene.Entities.Where(entity => bodyIds.Contains(entity.Id)))
                    {
                        transforms.Add(new JsonObject
                        {
                            ["entityId"] = entity.Id,
                            ["position"] = new JsonObject
                            {
                                ["x"] = entity.Transform.Position.X,
                                ["y"] = entity.Transform.Position.Y,
                                ["z"] = entity.Transform.Position.Z,
                            },
                            ["rotation"] = new JsonObject
                            {
                                ["x"] = entity.Transform.Rotation.X,
                                ["y"] = entity.Transform.Rotation.Y,
                                ["z"] = entity.Transform.Rotation.Z,
                                ["w"] = entity.Transform.Rotation.W,
                            },
                        });
                    }
                }
                result["runtimeTransforms"] = transforms;
                return result;
            }
        }

        public void UpdateViewport(JsonObject? parameters, long inputRevision, bool requireNewInput)
        {
            parameters ??= new JsonObject();
            lock (_gate)
            {
                var viewportInputRevision = _viewportInputRevision;
                if (requireNewInput)
                {
                    if (inputRevision <= viewportInputRevision)
                    {
                        throw new ArgumentException(
                            "viewportInput requires a monotonically increasing positive inputRevision.");
                    }
                    viewportInputRevision = inputRevision;
                }

                var width = parameters["width"]?.GetValue<int>() ?? _viewportWidth;
                var height = parameters["height"]?.GetValue<int>() ?? _viewportHeight;
                if (width is < 64 || width > _maximumWidth || height is < 64 || height > _maximumHeight)
                {
                    throw new ArgumentException(
                        $"Viewport must be between 64x64 and {_maximumWidth}x{_maximumHeight}.");
                }
                var cameraRevision = parameters["cameraRevision"]?.GetValue<long>() ?? _cameraRevision;
                var commandRevision = parameters["commandRevision"]?.GetValue<long>()
                    ?? (requireNewInput ? inputRevision : _commandRevision);
                if (cameraRevision < _cameraRevision || commandRevision < _commandRevision)
                {
                    throw new ArgumentException("Viewport revisions cannot move backwards.");
                }
                var editorCamera = _editorCamera;
                if (parameters["camera"] is JsonObject camera)
                {
                    editorCamera = new EditorCameraState(
                        camera["orthographic"]?.GetValue<bool>() ?? editorCamera.Orthographic,
                        ReadVector(camera["position"] as JsonArray, editorCamera.Position),
                        ReadVector(camera["target"] as JsonArray, editorCamera.Target),
                        camera["fieldOfViewDegrees"]?.GetValue<float>() ?? editorCamera.FieldOfViewDegrees,
                        camera["orthographicSize"]?.GetValue<float>() ?? editorCamera.OrthographicSize);
                }

                var candidateSelection = _selection;
                if (parameters["selection"] is JsonArray selectionArray)
                {
                    candidateSelection = selectionArray
                        .Select(static node => node?.GetValue<string>())
                        .Where(static value => !string.IsNullOrWhiteSpace(value))
                        .Select(static value => value!)
                        .ToArray();
                }

                _viewportWidth = width;
                _viewportHeight = height;
                _cameraRevision = cameraRevision;
                _commandRevision = commandRevision;
                _editorCamera = editorCamera;
                _selection = candidateSelection;
                _viewportInputRevision = viewportInputRevision;
            }
        }

        public void UpdateInputActions(JsonObject? actions, long inputRevision)
        {
            actions ??= new JsonObject();
            lock (_gate)
            {
                if (!_playSimulation)
                {
                    throw new ArgumentException("Runtime input actions are accepted only by an isolated Play worker.");
                }
                if (inputRevision <= _inputRevision)
                {
                    throw new ArgumentException("inputActions requires a monotonically increasing inputRevision.");
                }
                if (actions.Count > 64)
                {
                    throw new ArgumentException("inputActions exceeded the 64-action safety limit.");
                }
                var parsed = new Dictionary<string, float>(StringComparer.Ordinal);
                foreach (var (name, node) in actions)
                {
                    if (string.IsNullOrWhiteSpace(name) || name.Length > 128 || node is not JsonValue actionValue
                        || !actionValue.TryGetValue<float>(out var value) || !float.IsFinite(value)
                        || value is < -1 or > 1)
                    {
                        throw new ArgumentException("Input action names and finite normalized values are required.");
                    }
                    parsed.Add(name, value);
                }
                _inputActions = parsed;
                _runtimeInputSnapshot = RuntimeInputSnapshot.Neutral;
                _inputRevision = inputRevision;
            }
        }

        public void UpdateRuntimeInput(JsonObject? parameters, long inputRevision)
        {
            if (parameters is null
                || parameters["focused"] is not JsonValue focusedNode
                || !focusedNode.TryGetValue<bool>(out var focused)
                || parameters["captured"] is not JsonValue capturedNode
                || !capturedNode.TryGetValue<bool>(out var captured)
                || parameters["actions"] is not JsonObject actions)
            {
                throw new ArgumentException(
                    "runtimeInput requires focused, captured, and a full actions object.");
            }
            if (captured && !focused)
            {
                throw new ArgumentException("runtimeInput cannot be captured while unfocused.");
            }
            if (actions.Count is < 1 or > 64)
            {
                throw new ArgumentException("runtimeInput requires between 1 and 64 action states.");
            }

            var parsedValues = new Dictionary<string, float>(StringComparer.Ordinal);
            var parsedStates = new Dictionary<string, RuntimeInputActionState>(StringComparer.Ordinal);
            foreach (var (name, node) in actions)
            {
                if (!IsCanonicalActionName(name) || node is not JsonObject action || action.Count != 4
                    || action["kind"] is not JsonValue kindNode
                    || !kindNode.TryGetValue<string>(out var kind)
                    || kind is not ("button" or "axis1d")
                    || action["value"] is not JsonValue valueNode
                    || !valueNode.TryGetValue<float>(out var value)
                    || !float.IsFinite(value) || value is < -1 or > 1
                    || action["pressCount"] is not JsonValue pressNode
                    || !pressNode.TryGetValue<long>(out var pressCount) || pressCount < 0
                    || action["releaseCount"] is not JsonValue releaseNode
                    || !releaseNode.TryGetValue<long>(out var releaseCount) || releaseCount < 0
                    || (kind == "button" && value is not (0 or 1)))
                {
                    throw new ArgumentException(
                        "runtimeInput action states require canonical names, button/axis1d kinds, "
                        + "finite normalized values, and nonnegative edge counters.");
                }
                if ((!focused || !captured) && value != 0)
                {
                    throw new ArgumentException(
                        "Unfocused or uncaptured runtimeInput snapshots must be neutral.");
                }
                parsedValues.Add(name, value);
                parsedStates.Add(name, new RuntimeInputActionState(
                    kind == "button" ? RuntimeInputActionKind.Button : RuntimeInputActionKind.Axis1D,
                    value,
                    0,
                    checked((ulong)pressCount),
                    checked((ulong)releaseCount)));
            }

            lock (_gate)
            {
                if (!_playSimulation)
                {
                    throw new ArgumentException("Runtime input is accepted only by an isolated Play worker.");
                }
                if (inputRevision <= _inputRevision)
                {
                    throw new ArgumentException(
                        "runtimeInput requires a monotonically increasing positive inputRevision.");
                }
                if (_hasRuntimeInputState
                    && !_runtimeInputActionStates.Keys.ToHashSet(StringComparer.Ordinal)
                        .SetEquals(parsedStates.Keys))
                {
                    throw new ArgumentException(
                        "runtimeInput full-state snapshots must retain the negotiated action set.");
                }
                foreach (var (name, current) in parsedStates)
                {
                    if (_runtimeInputActionStates.TryGetValue(name, out var previous))
                    {
                        var expectedPress = previous.PressCount;
                        var expectedRelease = previous.ReleaseCount;
                        var wasActive = previous.Value != 0;
                        var isActive = current.Value != 0;
                        if (!wasActive && isActive) ++expectedPress;
                        else if (wasActive && !isActive) ++expectedRelease;
                        else if (wasActive && isActive && previous.Value * current.Value < 0)
                        {
                            ++expectedRelease;
                            ++expectedPress;
                        }
                        if (previous.Kind != current.Kind
                            || current.PressCount != expectedPress
                            || current.ReleaseCount != expectedRelease)
                        {
                            throw new ArgumentException(
                                $"runtimeInput edge counters or kind are inconsistent for '{name}'.");
                        }
                    }
                    else
                    {
                        var active = current.Value != 0;
                        if (current.PressCount < current.ReleaseCount
                            || current.PressCount - current.ReleaseCount != (active ? 1UL : 0UL))
                        {
                            throw new ArgumentException(
                                $"runtimeInput initial edge counters are inconsistent for '{name}'.");
                        }
                    }
                }

                _inputActions = parsedValues;
                _runtimeInputActionStates = parsedStates;
                _hasRuntimeInputState = true;
                _runtimeInputSnapshot = new RuntimeInputSnapshot(
                    inputRevision,
                    focused,
                    captured,
                    parsedStates);
                _inputRevision = inputRevision;
            }
        }

        private static bool IsCanonicalActionName(string name)
        {
            if (name.Length is < 1 or > 128 || name[0] is < 'a' or > 'z') return false;
            foreach (var character in name)
            {
                if ((character is >= 'a' and <= 'z') || (character is >= '0' and <= '9')
                    || character is '.' or '_' or '-')
                {
                    continue;
                }
                return false;
            }
            return true;
        }


        public void SetRenderFault(Exception exception)
        {
            lock (_gate)
            {
                Volatile.Write(ref _renderFault, exception.ToString());
                _physicsWorld?.Dispose();
                _physicsWorld = null;
                _previewSimulation = false;
                _scene = _authoringScene;
                SetMode(RuntimeMode.Faulted);
            }
        }

        public void Dispose()
        {
            lock (_gate)
            {
                if (_disposed)
                {
                    return;
                }
                _disposed = true;
                _projectComponents?.Dispose();
                _physicsWorld?.Dispose();
                _physicsWorld = null;
                _physicsRuntime?.Dispose();
            }
        }

        private bool ShouldSimulate() => _playSimulation || _previewSimulation;

        private void EnsurePhysicsWorld()
        {
            if (_physicsWorld is not null || _physicsRuntime is null || _physicsSnapshot is null)
            {
                return;
            }
            _physicsWorld = _physicsRuntime.CreateWorld(_physicsSnapshot);
        }

        private static RenderVector3 ReadVector(JsonArray? array, RenderVector3 fallback)
        {
            if (array is null || array.Count != 3)
            {
                return fallback;
            }
            return new RenderVector3(
                array[0]?.GetValue<float>() ?? fallback.X,
                array[1]?.GetValue<float>() ?? fallback.Y,
                array[2]?.GetValue<float>() ?? fallback.Z);
        }
    }

    private sealed record WorkerOptions(
        string FrameFile,
        int Width,
        int Height,
        int FrameVersion,
        string? ControlEndpoint,
        string SessionKind,
        string? NativeLibraryPath,
        string? ComponentModulesPath,
        string ViewId)
    {
        public static WorkerOptions Parse(string[] args)
        {
            string? frameFile = null;
            string? controlEndpoint = null;
            string? nativeLibraryPath = null;
            string? componentModulesPath = null;
            string? viewId = null;
            var sessionKind = "play";
            var width = 640;
            var height = 360;
            var frameVersion = FrameLayout.Version1;
            for (var index = 0; index < args.Length; index++)
            {
                switch (args[index])
                {
                    case "--frame-file" when index + 1 < args.Length:
                        frameFile = args[++index];
                        break;
                    case "--width" when index + 1 < args.Length:
                        width = int.Parse(args[++index], System.Globalization.CultureInfo.InvariantCulture);
                        break;
                    case "--height" when index + 1 < args.Length:
                        height = int.Parse(args[++index], System.Globalization.CultureInfo.InvariantCulture);
                        break;
                    case "--frame-version" when index + 1 < args.Length:
                        frameVersion = int.Parse(args[++index], System.Globalization.CultureInfo.InvariantCulture);
                        break;
                    case "--control" when index + 1 < args.Length:
                        controlEndpoint = args[++index];
                        break;
                    case "--session" when index + 1 < args.Length:
                        sessionKind = args[++index];
                        break;
                    case "--native" when index + 1 < args.Length:
                        nativeLibraryPath = args[++index];
                        break;
                    case "--component-modules" when index + 1 < args.Length:
                        componentModulesPath = args[++index];
                        break;
                    case "--view-id" when index + 1 < args.Length:
                        viewId = args[++index];
                        break;
                }
            }

            if (string.IsNullOrWhiteSpace(frameFile))
            {
                throw new ArgumentException("--frame-file is required.");
            }
            if (width is < 64 or > 4096 || height is < 64 or > 4096)
            {
                throw new ArgumentOutOfRangeException(nameof(args), "Frame dimensions are out of range.");
            }
            if (frameVersion is not (FrameLayout.Version1 or FrameLayout.Version2))
            {
                throw new ArgumentOutOfRangeException(nameof(args), "--frame-version must be 1 or 2.");
            }
            if (sessionKind is not ("preview" or "game-preview" or "play"))
            {
                throw new ArgumentOutOfRangeException(nameof(args), "--session must be preview, game-preview, or play.");
            }
            viewId ??= sessionKind == "preview" ? "scene" : sessionKind == "game-preview" ? "game" : "play";
            if (viewId.Length is < 1 or > 64 || viewId.Any(character =>
                    !(char.IsAsciiLetterOrDigit(character) || character is '-' or '_' or '.')))
            {
                throw new ArgumentOutOfRangeException(nameof(args), "--view-id must be a portable 1-64 character identifier.");
            }
            return new WorkerOptions(
                frameFile,
                width,
                height,
                frameVersion,
                controlEndpoint,
                sessionKind,
                nativeLibraryPath,
                componentModulesPath,
                viewId);
        }
    }
}
