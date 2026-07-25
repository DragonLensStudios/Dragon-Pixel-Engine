using System.Diagnostics;
using System.Reflection;
using System.Runtime.Versioning;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json.Nodes;
using DragonPixel.Contracts;
using DragonPixel.NativeInterop;

namespace DragonPixel.S1.ManagedTests;

internal static class Program
{
    private static async Task<int> Main(string[] args)
    {
        try
        {
            if (args.Length != 3)
            {
                throw new ArgumentException("Usage: managed tests <native library> <worker DLL> <temporary directory>");
            }
            var nativeLibrary = Path.GetFullPath(args[0]);
            var workerDll = Path.GetFullPath(args[1]);
            var temporaryRoot = Path.GetFullPath(args[2]);
            Directory.CreateDirectory(temporaryRoot);

            VerifyContracts();
            VerifyNativeInterop(nativeLibrary);
            await VerifyHeadlessWorkerAsync(nativeLibrary, workerDll, temporaryRoot);
            Console.WriteLine("S1 managed contracts, native interop, and headless worker passed.");
            return 0;
        }
        catch (Exception exception)
        {
            Console.Error.WriteLine(exception);
            return 1;
        }
    }

    private static void VerifyContracts()
    {
        var framework = typeof(DpeId).Assembly.GetCustomAttribute<TargetFrameworkAttribute>()?.FrameworkName;
        Assert(framework?.Contains(".NETStandard,Version=v2.1", StringComparison.Ordinal) == true,
            "DragonPixel.Contracts did not target .NET Standard 2.1.");
        Assert(DpeId.TryParse("5e08f200-eab4-4b99-b1ac-06aab424047d", out var id),
            "Portable ID did not parse.");
        Assert(id.ToString() == "5e08f200-eab4-4b99-b1ac-06aab424047d", "Portable ID was not canonical.");

        var componentAttribute = new DpeComponentAttribute(
            BuiltinComponentIds.Rotator,
            "DragonPixel.Managed.RotatorComponent",
            "Rotator",
            1,
            ComponentOwner.Managed);
        Assert(componentAttribute.Owner == ComponentOwner.Managed && componentAttribute.SchemaVersion == 1,
            "Managed component metadata contract was invalid.");
        Assert((FrameworkCapabilities.Sprite2D | FrameworkCapabilities.StaticMesh3D).HasFlag(FrameworkCapabilities.StaticMesh3D),
            "Framework capabilities flags were invalid.");
    }

    private static void VerifyNativeInterop(string nativeLibrary)
    {
        var session = NativeApiSession.Open(nativeLibrary);
        Assert(session.NegotiatedMajor == 1 && session.NegotiatedMinor >= 0, "Native ABI negotiation failed.");
        Assert(session.Capabilities.HasFlag(DpeCapabilities.StructuredErrors), "Native ABI capabilities were missing.");
        using var runtime = session.CreateRuntime();
        Assert(!runtime.IsInvalid, "SafeHandle runtime was invalid.");
    }

    private static async Task VerifyHeadlessWorkerAsync(string nativeLibrary, string workerDll, string temporaryRoot)
    {
        var snapshotPath = Path.Combine(temporaryRoot, "worker.dpescene");
        await File.WriteAllTextAsync(snapshotPath, CreateSnapshot(), new UTF8Encoding(false));
        var before = Hash(snapshotPath);
        var success = await RunWorkerAsync(nativeLibrary, workerDll, snapshotPath);
        Assert(success.ExitCode == 0, $"Headless worker rejected valid snapshot: {success.StandardError}");
        var response = JsonNode.Parse(success.StandardOutput.Trim())!.AsObject();
        Assert(response["succeeded"]!.GetValue<bool>(), "Headless worker result was not successful.");
        Assert(response["entities"]!.GetValue<int>() == 1, "Headless worker entity count was wrong.");
        Assert(response["nativeComponents"]!.GetValue<int>() == 1, "Native component count was wrong.");
        Assert(response["managedComponents"]!.GetValue<int>() == 1, "Managed component count was wrong.");
        Assert(response["opaqueComponents"]!.GetValue<int>() == 1, "Opaque component count was wrong.");
        Assert(before == Hash(snapshotPath), "Headless worker modified its immutable snapshot.");

        var invalidPath = Path.Combine(temporaryRoot, "invalid.dpescene");
        await File.WriteAllTextAsync(invalidPath, "{\"format\":\"wrong\",\"formatVersion\":1}", new UTF8Encoding(false));
        var failure = await RunWorkerAsync(nativeLibrary, workerDll, invalidPath);
        Assert(failure.ExitCode != 0, "Headless worker accepted an invalid snapshot.");
        var failureResponse = JsonNode.Parse(failure.StandardOutput.Trim())!.AsObject();
        Assert(!failureResponse["succeeded"]!.GetValue<bool>(), "Invalid snapshot did not return structured failure.");
    }

    private static async Task<ProcessResult> RunWorkerAsync(string nativeLibrary, string workerDll, string snapshot)
    {
        var startInfo = new ProcessStartInfo
        {
            FileName = "dotnet",
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            UseShellExecute = false,
            CreateNoWindow = true,
        };
        foreach (var argument in new[] { workerDll, "--snapshot", snapshot, "--native", nativeLibrary })
        {
            startInfo.ArgumentList.Add(argument);
        }
        var asanRuntime = Environment.GetEnvironmentVariable("DPE_ASAN_RUNTIME");
        if (OperatingSystem.IsMacOS() && !string.IsNullOrWhiteSpace(asanRuntime))
        {
            startInfo.Environment["DYLD_INSERT_LIBRARIES"] = asanRuntime;
            startInfo.Environment["ASAN_OPTIONS"] = "detect_leaks=0";
        }
        using var process = Process.Start(startInfo) ?? throw new InvalidOperationException("Could not launch worker.");
        var stdout = process.StandardOutput.ReadToEndAsync();
        var stderr = process.StandardError.ReadToEndAsync();
        await process.WaitForExitAsync();
        return new ProcessResult(process.ExitCode, await stdout, await stderr);
    }

    private static string Hash(string path) =>
        Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(path))).ToLowerInvariant();

    private static string CreateSnapshot() => """
        {
          "$schema": "https://dragonpixel.dev/schemas/v2/scene.schema.json",
          "format": "dpe.scene",
          "formatVersion": 2,
          "engineVersion": "0.1.0-slice1",
          "sceneId": "a7b88e34-56ec-42bb-8894-fd3e8c48213c",
          "name": "Managed worker sample",
          "entities": [
            {
              "enabled": true,
              "id": "5e08f200-eab4-4b99-b1ac-06aab424047d",
              "name": "Dragon",
              "parentId": null,
              "components": [
                { "typeId": "52e52fbd-ea15-40c5-bd9a-7dd320f7cd1e", "qualifiedName": "DragonPixel.Native.TransformComponent", "schemaVersion": 2, "owner": "native", "enabled": true, "properties": {} },
                { "typeId": "ee40709b-2bfc-4d50-b729-8612fb60d478", "qualifiedName": "DragonPixel.Managed.RotatorComponent", "schemaVersion": 1, "owner": "managed", "enabled": false, "properties": {} },
                { "typeId": "e1a3d322-b5bc-40db-8a2a-b3041baa6402", "qualifiedName": "Vendor.Missing.SparkleComponent", "schemaVersion": 9, "owner": "unknown", "enabled": true, "properties": { "keep": true } }
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

    private sealed record ProcessResult(int ExitCode, string StandardOutput, string StandardError);
}
