using System.Buffers.Binary;
using System.Diagnostics;
using System.Security.Cryptography;
using System.Text.Json;
using System.Text.Json.Nodes;

namespace DragonPixel.Runtime;

public static class WorkerHost
{
    private const int MaximumMessageLength = 1024 * 1024;

    public static async Task<int> RunAsync(IFrameworkSceneAdapter adapter, string[] args)
    {
        try
        {
            var options = WorkerOptions.Parse(args);
            using var frameBuffer = new SharedFrameBuffer(options.FrameFile, options.Width, options.Height);
            using var shutdown = new CancellationTokenSource();
            var state = new WorkerState();
            var renderTask = RenderLoopAsync(adapter, frameBuffer, state, shutdown.Token);
            ControlConnection? connection = null;
            try
            {
                Stream input;
                Stream output;
                if (options.ControlEndpoint is not null)
                {
                    connection = await ControlConnection.AcceptAsync(options.ControlEndpoint, shutdown.Token).ConfigureAwait(false);
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
            }
            shutdown.Cancel();
            await renderTask.ConfigureAwait(false);
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

    private static async Task RenderLoopAsync(
        IFrameworkSceneAdapter adapter,
        SharedFrameBuffer frameBuffer,
        WorkerState state,
        CancellationToken cancellationToken)
    {
        var pixels = new byte[checked(frameBuffer.Stride * frameBuffer.Height)];
        var stopwatch = Stopwatch.StartNew();
        while (!cancellationToken.IsCancellationRequested)
        {
            if (state.Mode == RuntimeMode.Running)
            {
                var phase = (float)stopwatch.Elapsed.TotalSeconds;
                var triangles = adapter.CreateTriangles(phase * 0.8f, frameBuffer.Width, frameBuffer.Height);
                SoftwareFrameRenderer.Render(
                    pixels,
                    frameBuffer.Width,
                    frameBuffer.Height,
                    triangles,
                    phase * 2.0f,
                    adapter.Experimental);
                frameBuffer.Publish(
                    pixels,
                    DateTime.UtcNow.Ticks,
                    FrameLayout.ContentSprite | FrameLayout.ContentStaticMesh,
                    StringComparer.Ordinal.GetHashCode(adapter.Name));
                state.FramePublished();
            }

            await Task.Delay(16, cancellationToken).ConfigureAwait(false);
        }
    }

    private static async Task CommandLoopAsync(
        IFrameworkSceneAdapter adapter,
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
                await WriteErrorAsync(output, id, -32001, "A valid capability token is required.").ConfigureAwait(false);
                continue;
            }
            JsonNode? result;
            switch (method)
            {
                case "handshake":
                    var requestedVersion = request["params"]?["protocolVersion"]?.GetValue<int>() ?? 1;
                    if (requestedVersion != 1)
                    {
                        await WriteErrorAsync(output, id, -32010, "Only local protocol version 1 is supported.")
                            .ConfigureAwait(false);
                        continue;
                    }
                    result = new JsonObject
                    {
                        ["protocolVersion"] = 1,
                        ["adapter"] = adapter.Name,
                        ["adapterVersion"] = adapter.Version,
                        ["experimental"] = adapter.Experimental,
                        ["pixelFormat"] = "BGRA8",
                        ["frameTransport"] = "memory-mapped-file-seqlock",
                        ["capabilityToken"] = capabilityToken,
                        ["sessionKind"] = sessionKind,
                        ["processId"] = Environment.ProcessId,
                    };
                    break;
                case "initialize":
                    state.SetMode(RuntimeMode.Stopped);
                    result = new JsonObject { ["state"] = "stopped" };
                    break;
                case "loadSnapshot":
                    try
                    {
                        result = LoadSnapshot(request, state);
                    }
                    catch (Exception exception) when (exception is IOException or JsonException)
                    {
                        await WriteErrorAsync(output, id, -32002, exception.Message).ConfigureAwait(false);
                        continue;
                    }
                    break;
                case "play":
                case "resume":
                    state.SetMode(RuntimeMode.Running);
                    result = new JsonObject { ["state"] = "running" };
                    break;
                case "pause":
                    state.SetMode(RuntimeMode.Paused);
                    result = new JsonObject { ["state"] = "paused" };
                    break;
                case "stop":
                    state.SetMode(RuntimeMode.Stopped);
                    result = new JsonObject { ["state"] = "stopped" };
                    break;
                case "diagnostics":
                    result = new JsonObject
                    {
                        ["state"] = state.Mode.ToString().ToLowerInvariant(),
                        ["frames"] = state.FrameCount,
                        ["processId"] = Environment.ProcessId,
                        ["snapshotSha256"] = state.SnapshotHash,
                    };
                    break;
                case "subscribeDiagnostics":
                    result = new JsonObject { ["subscriptionId"] = Guid.NewGuid().ToString("D") };
                    break;
                case "inspect":
                    result = new JsonObject
                    {
                        ["state"] = state.Mode.ToString().ToLowerInvariant(),
                        ["snapshotSha256"] = state.SnapshotHash,
                        ["adapter"] = adapter.Name,
                    };
                    break;
                case "applyCommand":
                    result = new JsonObject
                    {
                        ["applied"] = false,
                        ["reason"] = "The Slice 1 play snapshot is isolated and runtime commands are non-authoritative.",
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
                    await WriteResponseAsync(output, id, new JsonObject { ["crashing"] = true }).ConfigureAwait(false);
                    Environment.Exit(86);
                    return;
                case "shutdown":
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
        using var stream = new FileStream(snapshotPath, FileMode.Open, FileAccess.Read, FileShare.Read);
        using var document = JsonDocument.Parse(stream);
        var root = document.RootElement;
        var formatVersion = root.GetProperty("formatVersion").GetInt32();
        if (root.GetProperty("format").GetString() != "dpe.scene" || formatVersion is not (1 or 2))
        {
            throw new InvalidDataException("Snapshot format was unsupported.");
        }
        if (formatVersion == 2
            && (!root.TryGetProperty("$schema", out var schema)
                || schema.GetString() != "https://dragonpixel.dev/schemas/v2/scene.schema.json"
                || !root.TryGetProperty("engineVersion", out var engineVersion)
                || string.IsNullOrWhiteSpace(engineVersion.GetString())))
        {
            throw new InvalidDataException(
                "Version 2 snapshots require the canonical schema URI and engineVersion.");
        }
        stream.Position = 0;
        state.SetSnapshotHash(Convert.ToHexString(SHA256.HashData(stream)).ToLowerInvariant());
        state.SetMode(RuntimeMode.Stopped);
        return new JsonObject { ["state"] = "stopped", ["snapshotSha256"] = state.SnapshotHash };
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

    private enum RuntimeMode
    {
        Stopped,
        Running,
        Paused,
    }

    private sealed class WorkerState
    {
        private int _mode;
        private long _frameCount;
        private string _snapshotHash = string.Empty;

        public RuntimeMode Mode => (RuntimeMode)Volatile.Read(ref _mode);
        public long FrameCount => Interlocked.Read(ref _frameCount);
        public string SnapshotHash => Volatile.Read(ref _snapshotHash);
        public void SetMode(RuntimeMode mode) => Volatile.Write(ref _mode, (int)mode);
        public void FramePublished() => Interlocked.Increment(ref _frameCount);
        public void SetSnapshotHash(string value) => Volatile.Write(ref _snapshotHash, value);
    }

    private sealed record WorkerOptions(
        string FrameFile,
        int Width,
        int Height,
        string? ControlEndpoint,
        string SessionKind)
    {
        public static WorkerOptions Parse(string[] args)
        {
            string? frameFile = null;
            string? controlEndpoint = null;
            var sessionKind = "play";
            var width = 640;
            var height = 360;
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
                    case "--control" when index + 1 < args.Length:
                        controlEndpoint = args[++index];
                        break;
                    case "--session" when index + 1 < args.Length:
                        sessionKind = args[++index];
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

            if (sessionKind is not ("preview" or "play"))
            {
                throw new ArgumentOutOfRangeException(nameof(args), "--session must be preview or play.");
            }

            return new WorkerOptions(frameFile, width, height, controlEndpoint, sessionKind);
        }
    }
}
