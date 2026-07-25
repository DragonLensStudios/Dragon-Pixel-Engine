using System.Buffers.Binary;
using System.Diagnostics;
using System.IO.MemoryMappedFiles;
using System.Text.Json;
using System.Text.Json.Nodes;

namespace DragonPixel.PocB.Tests;

internal static class Program
{
    private const int HeaderSize = 64;
    private const int FrameMagic = 0x46504544;

    private static async Task<int> Main(string[] args)
    {
        try
        {
            if (args.Length != 3)
            {
                throw new ArgumentException("Usage: POC-B tests <MonoGame worker DLL> <KNI worker DLL> <temporary directory>");
            }

            Directory.CreateDirectory(args[2]);
            await VerifyWorkerAsync("MonoGame", Path.GetFullPath(args[0]), Path.Combine(args[2], "monogame.frame"));
            await VerifyWorkerAsync("KNI", Path.GetFullPath(args[1]), Path.Combine(args[2], "kni.frame"));
            Console.WriteLine("POC B passed for MonoGame and KNI workers.");
            return 0;
        }
        catch (Exception exception)
        {
            Console.Error.WriteLine(exception);
            return 1;
        }
    }

    private static async Task VerifyWorkerAsync(string expectedAdapter, string workerDll, string frameFile)
    {
        File.Delete(frameFile);
        using var worker = StartWorker(workerDll, frameFile);
        var latency = Stopwatch.StartNew();
        var handshake = await worker.CallAsync("handshake");
        latency.Stop();
        Assert(handshake["adapter"]!.GetValue<string>() == expectedAdapter, "Worker reported the wrong adapter.");
        Assert(handshake["pixelFormat"]!.GetValue<string>() == "BGRA8", "Worker did not negotiate BGRA8.");
        Assert(latency.ElapsedMilliseconds < 1000, "Initial control response exceeded one second.");

        await worker.CallAsync("initialize");
        await worker.CallAsync("play");
        var firstFrame = await ReadFrameAfterAsync(frameFile, 0, TimeSpan.FromSeconds(5));
        Assert(firstFrame.Width == 640 && firstFrame.Height == 360 && firstFrame.Stride == 2560,
            "Frame dimensions or stride were invalid.");
        Assert(firstFrame.PixelFormat == 1 && firstFrame.ContentFlags == 3,
            "Frame did not declare BGRA8 sprite and static-mesh content.");
        Assert(firstFrame.DistinctColorEstimate > 5, "Frame did not contain rendered scene variation.");

        await Task.Delay(1500);
        var warmedFrame = await ReadFrameAfterAsync(frameFile, firstFrame.Sequence, TimeSpan.FromSeconds(3));
        var rateStart = warmedFrame.Sequence;
        var rateTimer = Stopwatch.StartNew();
        await Task.Delay(2000);
        var rateEnd = await ReadFrameAfterAsync(frameFile, rateStart, TimeSpan.FromSeconds(2));
        rateTimer.Stop();
        var framesPerSecond = ((rateEnd.Sequence - rateStart) / 2.0) / rateTimer.Elapsed.TotalSeconds;
        Assert(framesPerSecond >= 20.0, $"{expectedAdapter} frame rate was only {framesPerSecond:F1} FPS.");

        await worker.CallAsync("pause");
        await Task.Delay(100);
        var pausedSequence = ReadFrame(frameFile).Sequence;
        await Task.Delay(250);
        Assert(ReadFrame(frameFile).Sequence == pausedSequence, "Pause did not stop frame publication.");

        await worker.CallAsync("resume");
        var resumed = await ReadFrameAfterAsync(frameFile, pausedSequence, TimeSpan.FromSeconds(2));
        await worker.CallAsync("stop");
        await Task.Delay(100);
        var stoppedSequence = ReadFrame(frameFile).Sequence;
        await Task.Delay(250);
        Assert(ReadFrame(frameFile).Sequence == stoppedSequence, "Stop did not stop frame publication.");
        Assert(resumed.Sequence > pausedSequence, "Resume did not restart frame publication.");

        await worker.CallAsync("play");
        await ReadFrameAfterAsync(frameFile, stoppedSequence, TimeSpan.FromSeconds(2));
        var firstProcessId = worker.ProcessId;
        await worker.CallAsync("crash");
        await worker.WaitForExitAsync(TimeSpan.FromSeconds(5));
        Assert(worker.ExitCode == 86, "Forced worker crash did not cross the process boundary.");

        var recoveryTimer = Stopwatch.StartNew();
        using var restarted = StartWorker(workerDll, frameFile);
        var restartedHandshake = await restarted.CallAsync("handshake");
        Assert(restartedHandshake["adapter"]!.GetValue<string>() == expectedAdapter,
            "Restarted worker reported the wrong adapter.");
        Assert(restarted.ProcessId != firstProcessId, "Crash recovery reused the terminated process.");
        await restarted.CallAsync("play");
        await ReadFrameAfterAsync(frameFile, 0, TimeSpan.FromSeconds(5));
        recoveryTimer.Stop();
        Assert(recoveryTimer.Elapsed < TimeSpan.FromSeconds(5), "Worker crash recovery exceeded five seconds.");
        await restarted.CallAsync("shutdown");
        await restarted.WaitForExitAsync(TimeSpan.FromSeconds(5));
        Assert(restarted.ExitCode == 0, "Worker did not shut down cleanly.");

        Console.WriteLine(
            $"{expectedAdapter}: {framesPerSecond:F1} FPS, control {latency.Elapsed.TotalMilliseconds:F1} ms, " +
            $"crash recovery {recoveryTimer.Elapsed.TotalMilliseconds:F1} ms.");
    }

    private static WorkerProcess StartWorker(string workerDll, string frameFile)
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
        startInfo.ArgumentList.Add(workerDll);
        startInfo.ArgumentList.Add("--frame-file");
        startInfo.ArgumentList.Add(frameFile);
        startInfo.ArgumentList.Add("--width");
        startInfo.ArgumentList.Add("640");
        startInfo.ArgumentList.Add("--height");
        startInfo.ArgumentList.Add("360");
        return new WorkerProcess(Process.Start(startInfo)
            ?? throw new InvalidOperationException("Could not launch worker."));
    }

    private static async Task<FrameSnapshot> ReadFrameAfterAsync(
        string path,
        long sequence,
        TimeSpan timeout)
    {
        var stopwatch = Stopwatch.StartNew();
        Exception? lastError = null;
        while (stopwatch.Elapsed < timeout)
        {
            try
            {
                var frame = ReadFrame(path);
                if (frame.Sequence > sequence && (frame.Sequence & 1) == 0)
                {
                    return frame;
                }
            }
            catch (IOException exception)
            {
                lastError = exception;
            }

            await Task.Delay(1);
        }

        throw new TimeoutException($"No new stable frame appeared in {timeout}.", lastError);
    }

    private static FrameSnapshot ReadFrame(string path)
    {
        using var stream = new FileStream(
            path,
            FileMode.Open,
            FileAccess.Read,
            FileShare.ReadWrite | FileShare.Delete);
        using var mapping = MemoryMappedFile.CreateFromFile(
            stream,
            mapName: null,
            capacity: 0,
            MemoryMappedFileAccess.Read,
            HandleInheritability.None,
            leaveOpen: false);
        using var view = mapping.CreateViewAccessor(0, 0, MemoryMappedFileAccess.Read);
        var sequenceBefore = view.ReadInt64(24);
        if ((sequenceBefore & 1) != 0)
        {
            throw new IOException("Shared frame is currently being written.");
        }

        Thread.MemoryBarrier();
        var magic = view.ReadInt32(0);
        var version = view.ReadInt32(4);
        var width = view.ReadInt32(8);
        var height = view.ReadInt32(12);
        var stride = view.ReadInt32(16);
        var format = view.ReadInt32(20);
        var contentFlags = view.ReadInt32(40);
        var pixels = new byte[checked(stride * height)];
        var bytesRead = view.ReadArray(HeaderSize, pixels, 0, pixels.Length);
        Thread.MemoryBarrier();
        var sequenceAfter = view.ReadInt64(24);
        if (bytesRead != pixels.Length)
        {
            throw new IOException("Shared frame pixel payload was incomplete.");
        }

        if (magic != FrameMagic || version != 1 || sequenceBefore != sequenceAfter || (sequenceAfter & 1) != 0)
        {
            throw new IOException("Shared frame was unavailable or changed while being read.");
        }

        var colors = new HashSet<int>();
        var samplingStep = Math.Max(4, pixels.Length / 2048);
        samplingStep -= samplingStep % 4;
        for (var index = 0; index + 3 < pixels.Length; index += samplingStep)
        {
            colors.Add(BinaryPrimitives.ReadInt32LittleEndian(pixels.AsSpan(index, 4)));
        }

        return new FrameSnapshot(width, height, stride, format, sequenceAfter, contentFlags, colors.Count);
    }

    private static void Assert(bool condition, string message)
    {
        if (!condition)
        {
            throw new InvalidOperationException(message);
        }
    }

    private sealed record FrameSnapshot(
        int Width,
        int Height,
        int Stride,
        int PixelFormat,
        long Sequence,
        int ContentFlags,
        int DistinctColorEstimate);

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

        public async Task<JsonObject> CallAsync(string method)
        {
            var request = new JsonObject
            {
                ["jsonrpc"] = "2.0",
                ["id"] = Interlocked.Increment(ref _nextId),
                ["method"] = method,
            };
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
