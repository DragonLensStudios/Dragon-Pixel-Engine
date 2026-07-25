using System.Buffers.Binary;
using System.Diagnostics;
using System.Security.Cryptography;
using System.Text.Json;
using System.Text.Json.Nodes;
using DragonPixel.NativeInterop;

namespace DragonPixel.Runtime;

public static class WorkerHost
{
    private const int MaximumMessageLength = 1024 * 1024;
    private const int TargetFrameRate = 60;
    private static readonly long FrameIntervalTicks = Math.Max(1L, Stopwatch.Frequency / TargetFrameRate);
    private static readonly long FinalSpinWindowTicks = Math.Max(1L, Stopwatch.Frequency / 1000L);
    private static readonly long MaximumScheduleLatenessTicks = FrameIntervalTicks * 4L;

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
                options.NativeLibraryPath);
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
                            options.SessionKind)
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
        var nextFrameDeadline = Stopwatch.GetTimestamp();
        try
        {
            while (!cancellationToken.IsCancellationRequested)
            {
                if (state.Mode == RuntimeMode.Running)
                {
                    var request = state.CreateRenderRequest(stopwatch.Elapsed);
                    var adapterStart = Stopwatch.GetTimestamp();
                    var frame = adapter.RenderFrame(request);
                    var adapterTicks = Stopwatch.GetTimestamp() - adapterStart;
                    var publishStart = Stopwatch.GetTimestamp();
                    frameBuffer.Publish(
                        frame,
                        DateTime.UtcNow.Ticks,
                        StringComparer.Ordinal.GetHashCode(adapter.Name));
                    var publishTicks = Stopwatch.GetTimestamp() - publishStart;
                    state.FramePublished(adapterTicks, publishTicks, frame);
                }
                adapter.ServicePendingOperations();

                nextFrameDeadline += FrameIntervalTicks;
                var now = Stopwatch.GetTimestamp();
                if (now - nextFrameDeadline > MaximumScheduleLatenessTicks)
                {
                    nextFrameDeadline = now + FrameIntervalTicks;
                }

                if (!WaitUntil(nextFrameDeadline, cancellationToken))
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

    private static bool WaitUntil(long deadline, CancellationToken cancellationToken)
    {
        while (!cancellationToken.IsCancellationRequested)
        {
            var remainingTicks = deadline - Stopwatch.GetTimestamp();
            if (remainingTicks <= 0)
            {
                return true;
            }

            if (remainingTicks > FinalSpinWindowTicks)
            {
                var coarseTicks = remainingTicks - FinalSpinWindowTicks;
                var coarseMilliseconds = Math.Clamp(
                    (int)(coarseTicks * 1000L / Stopwatch.Frequency),
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
        string sessionKind)
    {
        var capabilityToken = Convert.ToHexString(RandomNumberGenerator.GetBytes(24)).ToLowerInvariant();
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
            if (requireCapabilityToken && method != "handshake"
                && request["capabilityToken"]?.GetValue<string>() != capabilityToken)
            {
                await WriteErrorAsync(output, id, -32001, "A valid capability token is required.")
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
                        ["sceneDrivenGraphics"] = true,
                        ["nativePhysics"] = state.PhysicsAvailable,
                        ["simulatePreview"] = requestedVersion >= 2 && sessionKind == "preview",
                        ["idBufferPicking"] = requestedVersion >= 2,
                        ["viewportResize"] = requestedVersion >= 2,
                        ["backend"] = adapter.Backend,
                        ["device"] = adapter.Device,
                        ["capabilityToken"] = capabilityToken,
                        ["sessionKind"] = sessionKind,
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
                    catch (ArgumentException exception)
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
        var parsed = SceneSnapshotParser.Parse(snapshotPath, requestedRevision);
        state.ReplaceSnapshot(parsed);
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
            ["presentedInputRevision"] = state.PresentedInputRevision,
            ["backend"] = state.LastBackend.Length == 0 ? adapter.Backend : state.LastBackend,
            ["device"] = state.LastDevice.Length == 0 ? adapter.Device : state.LastDevice,
            ["renderFault"] = state.RenderFault,
            ["sceneDiagnostics"] = sceneDiagnostics,
            ["physics"] = state.CreatePhysicsDiagnostics(includeTransforms: false),
            ["lastFrameTimingsMs"] = new JsonObject
            {
                ["adapter"] = ToMilliseconds(state.AdapterTicks),
                ["render"] = ToMilliseconds(state.AdapterTicks),
                ["publish"] = ToMilliseconds(state.PublishTicks),
            },
        };
    }

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
        private readonly WorkerPhysicsRuntime? _physicsRuntime;
        private int _mode;
        private int _viewportWidth;
        private int _viewportHeight;
        private long _frameCount;
        private long _nextFrameRevision;
        private long _adapterTicks;
        private long _publishTicks;
        private long _inputRevision;
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
        private TimeSpan? _lastPhysicsElapsed;
        private bool _previewSimulation;
        private NativePhysicsStepResult _lastPhysicsStep;
        private EditorCameraState _editorCamera = EditorCameraState.Default;
        private IReadOnlyList<string> _selection = Array.Empty<string>();
        private bool _disposed;

        public WorkerState(
            int maximumWidth,
            int maximumHeight,
            string sessionKind,
            string? nativeLibraryPath)
        {
            _maximumWidth = maximumWidth;
            _maximumHeight = maximumHeight;
            _viewportWidth = maximumWidth;
            _viewportHeight = maximumHeight;
            _useSceneCamera = sessionKind == "play";
            if (!string.IsNullOrWhiteSpace(nativeLibraryPath))
            {
                _physicsRuntime = new WorkerPhysicsRuntime(Path.GetFullPath(nativeLibraryPath));
            }
        }

        public RuntimeMode Mode => (RuntimeMode)Volatile.Read(ref _mode);
        public int ViewportWidth => Volatile.Read(ref _viewportWidth);
        public int ViewportHeight => Volatile.Read(ref _viewportHeight);
        public long FrameCount => Interlocked.Read(ref _frameCount);
        public long AdapterTicks => Interlocked.Read(ref _adapterTicks);
        public long PublishTicks => Interlocked.Read(ref _publishTicks);
        public long InputRevision => Interlocked.Read(ref _inputRevision);
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
                if (_physicsWorld is not null && ShouldSimulate())
                {
                    var delta = _lastPhysicsElapsed.HasValue
                        ? elapsed - _lastPhysicsElapsed.Value
                        : TimeSpan.Zero;
                    _lastPhysicsElapsed = elapsed;
                    _lastPhysicsStep = _physicsWorld.Advance(delta);
                    _scene = _physicsWorld.ApplyTransforms(_authoringScene);
                }
                return new FrameworkRenderRequest(
                    _scene,
                    _viewportWidth,
                    _viewportHeight,
                    elapsed,
                    _useSceneCamera,
                    _editorCamera,
                    _selection,
                    _scene.SnapshotRevision,
                    _cameraRevision,
                    _commandRevision,
                    _inputRevision,
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

        public void ReplaceSnapshot(ParsedSceneSnapshot parsed)
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

                var previous = _physicsWorld;
                _physicsWorld = candidate;
                _physicsSnapshot = parsed.Physics;
                _authoringScene = parsed.Scene;
                _scene = parsed.Scene;
                _lastPhysicsElapsed = null;
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
                }
                _lastPhysicsElapsed = null;
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
                _lastPhysicsElapsed = null;
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
                _lastPhysicsElapsed = null;
                _lastPhysicsStep = default;
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
                    ["simulationMode"] = _useSceneCamera
                        ? "play"
                        : _previewSimulation ? "simulate-preview" : "edit",
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
                if (requireNewInput)
                {
                    if (inputRevision <= _inputRevision)
                    {
                        throw new ArgumentException(
                            "viewportInput requires a monotonically increasing positive inputRevision.");
                    }
                    _inputRevision = inputRevision;
                }

                var width = parameters["width"]?.GetValue<int>() ?? _viewportWidth;
                var height = parameters["height"]?.GetValue<int>() ?? _viewportHeight;
                if (width is < 64 || width > _maximumWidth || height is < 64 || height > _maximumHeight)
                {
                    throw new ArgumentException(
                        $"Viewport must be between 64x64 and {_maximumWidth}x{_maximumHeight}.");
                }
                _viewportWidth = width;
                _viewportHeight = height;

                var cameraRevision = parameters["cameraRevision"]?.GetValue<long>() ?? _cameraRevision;
                var commandRevision = parameters["commandRevision"]?.GetValue<long>()
                    ?? (requireNewInput ? inputRevision : _commandRevision);
                if (cameraRevision < _cameraRevision || commandRevision < _commandRevision)
                {
                    throw new ArgumentException("Viewport revisions cannot move backwards.");
                }
                _cameraRevision = cameraRevision;
                _commandRevision = commandRevision;

                if (parameters["camera"] is JsonObject camera)
                {
                    _editorCamera = new EditorCameraState(
                        camera["orthographic"]?.GetValue<bool>() ?? _editorCamera.Orthographic,
                        ReadVector(camera["position"] as JsonArray, _editorCamera.Position),
                        ReadVector(camera["target"] as JsonArray, _editorCamera.Target),
                        camera["fieldOfViewDegrees"]?.GetValue<float>() ?? _editorCamera.FieldOfViewDegrees,
                        camera["orthographicSize"]?.GetValue<float>() ?? _editorCamera.OrthographicSize);
                }

                if (parameters["selection"] is JsonArray selection)
                {
                    _selection = selection
                        .Select(static node => node?.GetValue<string>())
                        .Where(static value => !string.IsNullOrWhiteSpace(value))
                        .Select(static value => value!)
                        .ToArray();
                }
            }
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
                _physicsWorld?.Dispose();
                _physicsWorld = null;
                _physicsRuntime?.Dispose();
            }
        }

        private bool ShouldSimulate() => _useSceneCamera || _previewSimulation;

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
        string? NativeLibraryPath)
    {
        public static WorkerOptions Parse(string[] args)
        {
            string? frameFile = null;
            string? controlEndpoint = null;
            string? nativeLibraryPath = null;
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
            if (sessionKind is not ("preview" or "play"))
            {
                throw new ArgumentOutOfRangeException(nameof(args), "--session must be preview or play.");
            }
            return new WorkerOptions(
                frameFile,
                width,
                height,
                frameVersion,
                controlEndpoint,
                sessionKind,
                nativeLibraryPath);
        }
    }
}
