using System.Diagnostics;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json.Nodes;

namespace DragonPixel.PocD.Tests;

internal static class Program
{
    private static async Task<int> Main(string[] args)
    {
        try
        {
            if (args.Length != 3)
            {
                throw new ArgumentException("Usage: POC-D tests <scanner DLL> <fixtures root> <temporary root>");
            }

            var scannerDll = Path.GetFullPath(args[0]);
            var fixturesRoot = Path.GetFullPath(args[1]);
            var temporaryRoot = Path.GetFullPath(args[2]);
            if (Directory.Exists(temporaryRoot))
            {
                Directory.Delete(temporaryRoot, recursive: true);
            }
            Directory.CreateDirectory(temporaryRoot);

            await VerifyFixtureAsync(scannerDll, fixturesRoot, temporaryRoot, "MonoGameSample", "MonoGame");
            await VerifyFixtureAsync(scannerDll, fixturesRoot, temporaryRoot, "KniSample", "KNI");
            await VerifyOutputContainmentGuardAsync(scannerDll, fixturesRoot, temporaryRoot);
            Console.WriteLine("POC D passed: JSON/Markdown reports produced with byte- and metadata-identical input trees.");
            return 0;
        }
        catch (Exception exception)
        {
            Console.Error.WriteLine(exception);
            return 1;
        }
    }

    private static async Task VerifyFixtureAsync(
        string scannerDll,
        string fixturesRoot,
        string temporaryRoot,
        string fixtureName,
        string expectedFramework)
    {
        var input = Path.Combine(fixturesRoot, fixtureName);
        var output = Path.Combine(temporaryRoot, fixtureName);
        var jsonPath = Path.Combine(output, "report.json");
        var markdownPath = Path.Combine(output, "report.md");
        var before = CaptureTree(input);

        var result = await RunScannerAsync(scannerDll, input, jsonPath, markdownPath);
        Assert(result.ExitCode == 0, $"Scanner failed for {fixtureName}: {result.StandardError}");
        var after = CaptureTree(input);
        Assert(before.SequenceEqual(after), $"Scanner changed the {fixtureName} source tree.");
        Assert(File.Exists(jsonPath) && File.Exists(markdownPath), "Scanner did not create both report formats.");

        var report = JsonNode.Parse(await File.ReadAllTextAsync(jsonPath))!.AsObject();
        Assert(report["format"]!.GetValue<string>() == "dpe.migration-scan", "JSON report format was invalid.");
        Assert(report["frameworks"]!.AsArray().Any(node => node!.GetValue<string>() == expectedFramework),
            $"JSON report did not detect {expectedFramework}.");
        Assert(report["inspectedTreeHash"]!.GetValue<string>() == ComputeTreeHash(input),
            "Report tree hash did not match independently captured input bytes.");

        var markdown = await File.ReadAllTextAsync(markdownPath);
        Assert(markdown.Contains(expectedFramework, StringComparison.Ordinal),
            $"Markdown report did not name {expectedFramework}.");
        Assert(markdown.Contains("Read-only analysis", StringComparison.Ordinal),
            "Markdown report omitted the analysis limitation.");
    }

    private static async Task VerifyOutputContainmentGuardAsync(
        string scannerDll,
        string fixturesRoot,
        string temporaryRoot)
    {
        var input = Path.Combine(fixturesRoot, "MonoGameSample");
        var forbiddenOutput = Path.Combine(input, "forbidden-report.json");
        var safeMarkdown = Path.Combine(temporaryRoot, "guard.md");
        var before = CaptureTree(input);
        var result = await RunScannerAsync(scannerDll, input, forbiddenOutput, safeMarkdown);
        Assert(result.ExitCode != 0, "Scanner accepted a report path inside the inspected tree.");
        Assert(!File.Exists(forbiddenOutput), "Scanner wrote a forbidden report inside the inspected tree.");
        Assert(before.SequenceEqual(CaptureTree(input)), "Containment rejection changed the input tree.");
    }

    private static async Task<ProcessResult> RunScannerAsync(
        string scannerDll,
        string input,
        string jsonPath,
        string markdownPath)
    {
        var startInfo = new ProcessStartInfo
        {
            FileName = "dotnet",
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            UseShellExecute = false,
            CreateNoWindow = true,
        };
        foreach (var argument in new[] { scannerDll, "--input", input, "--json", jsonPath, "--markdown", markdownPath })
        {
            startInfo.ArgumentList.Add(argument);
        }

        using var process = Process.Start(startInfo) ?? throw new InvalidOperationException("Could not start scanner.");
        var stdoutTask = process.StandardOutput.ReadToEndAsync();
        var stderrTask = process.StandardError.ReadToEndAsync();
        await process.WaitForExitAsync();
        return new ProcessResult(process.ExitCode, await stdoutTask, await stderrTask);
    }

    private static IReadOnlyList<TreeEntry> CaptureTree(string root) =>
        Directory.EnumerateFiles(root, "*", SearchOption.AllDirectories)
            .OrderBy(path => Relative(root, path), StringComparer.Ordinal)
            .Select(path => new FileInfo(path))
            .Select(info => new TreeEntry(
                Relative(root, info.FullName),
                info.Length,
                info.LastWriteTimeUtc.Ticks,
                info.Attributes,
                Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(info.FullName))).ToLowerInvariant()))
            .ToArray();

    private static string ComputeTreeHash(string root)
    {
        using var incremental = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
        foreach (var file in Directory.EnumerateFiles(root, "*", SearchOption.AllDirectories)
                     .OrderBy(path => Relative(root, path), StringComparer.Ordinal))
        {
            incremental.AppendData(Encoding.UTF8.GetBytes(Relative(root, file) + "\n"));
            incremental.AppendData(File.ReadAllBytes(file));
        }
        return Convert.ToHexString(incremental.GetHashAndReset()).ToLowerInvariant();
    }

    private static string Relative(string root, string path) => Path.GetRelativePath(root, path).Replace('\\', '/');

    private static void Assert(bool condition, string message)
    {
        if (!condition)
        {
            throw new InvalidOperationException(message);
        }
    }

    private sealed record TreeEntry(
        string Path,
        long Length,
        long LastWriteTicks,
        FileAttributes Attributes,
        string Sha256);

    private sealed record ProcessResult(int ExitCode, string StandardOutput, string StandardError);
}
