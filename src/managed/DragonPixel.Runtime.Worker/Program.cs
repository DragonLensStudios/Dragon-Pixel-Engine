using System.Security.Cryptography;
using System.Text.Json;
using DragonPixel.Contracts;
using DragonPixel.NativeInterop;

namespace DragonPixel.Runtime.Worker;

internal static class Program
{
    private static int Main(string[] args)
    {
        try
        {
            var options = Options.Parse(args);
            var beforeHash = HashFile(options.SnapshotPath);
            using var stream = new FileStream(options.SnapshotPath, FileMode.Open, FileAccess.Read, FileShare.Read);
            using var document = JsonDocument.Parse(stream);
            var root = document.RootElement;
            var formatVersion = root.GetProperty("formatVersion").GetInt32();
            if (root.GetProperty("format").GetString() != "dpe.scene"
                || formatVersion is not (1 or 2))
            {
                return WriteFailure("DPE.WORKER.UNSUPPORTED_SNAPSHOT", "Snapshot format or version was unsupported.");
            }
            if (formatVersion == 2
                && (!root.TryGetProperty("$schema", out var schema)
                    || schema.GetString() != "https://dragonpixel.dev/schemas/v2/scene.schema.json"
                    || !root.TryGetProperty("engineVersion", out var engineVersion)
                    || string.IsNullOrWhiteSpace(engineVersion.GetString())))
            {
                return WriteFailure(
                    "DPE.WORKER.INVALID_SNAPSHOT_ENVELOPE",
                    "Version 2 snapshots require the canonical schema URI and engineVersion.");
            }
            if (!DpeId.TryParse(root.GetProperty("sceneId").GetString(), out var sceneId))
            {
                return WriteFailure("DPE.WORKER.INVALID_SCENE_ID", "Snapshot sceneId was invalid.");
            }

            var nativeApi = NativeApiSession.Open(options.NativeLibraryPath);
            using var runtime = nativeApi.CreateRuntime();
            var entities = 0;
            var nativeComponents = 0;
            var managedComponents = 0;
            var opaqueComponents = 0;
            foreach (var entity in root.GetProperty("entities").EnumerateArray())
            {
                entities++;
                if (!DpeId.TryParse(entity.GetProperty("id").GetString(), out _))
                {
                    return WriteFailure("DPE.WORKER.INVALID_ENTITY_ID", "Snapshot entity ID was invalid.");
                }
                if (formatVersion >= 2 && !entity.TryGetProperty("enabled", out _))
                {
                    return WriteFailure("DPE.WORKER.INVALID_ENTITY_RECORD",
                        "Version 2 entity records require an enabled field.");
                }
                foreach (var component in entity.GetProperty("components").EnumerateArray())
                {
                    if (formatVersion >= 2
                        && (!component.TryGetProperty("qualifiedName", out _)
                            || !component.TryGetProperty("enabled", out _)))
                    {
                        return WriteFailure("DPE.WORKER.INVALID_COMPONENT_RECORD",
                            "Version 2 component records require qualifiedName and enabled fields.");
                    }
                    var owner = component.GetProperty("owner").GetString();
                    if (owner == "native")
                    {
                        nativeComponents++;
                    }
                    else if (owner == "managed")
                    {
                        managedComponents++;
                    }
                    else
                    {
                        opaqueComponents++;
                    }
                }
            }
            var afterHash = HashFile(options.SnapshotPath);
            if (!string.Equals(beforeHash, afterHash, StringComparison.Ordinal))
            {
                return WriteFailure("DPE.WORKER.SNAPSHOT_CHANGED", "Read-only snapshot changed during load.");
            }

            Console.WriteLine(JsonSerializer.Serialize(new
            {
                succeeded = true,
                sceneId = sceneId.ToString(),
                entities,
                nativeComponents,
                managedComponents,
                opaqueComponents,
                nativeAbi = $"{nativeApi.NegotiatedMajor}.{nativeApi.NegotiatedMinor}",
                snapshotSha256 = beforeHash,
            }));
            return 0;
        }
        catch (Exception exception)
        {
            return WriteFailure("DPE.WORKER.UNHANDLED", exception.Message);
        }
    }

    private static string HashFile(string path) =>
        Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(path))).ToLowerInvariant();

    private static int WriteFailure(string code, string message)
    {
        Console.WriteLine(JsonSerializer.Serialize(new
        {
            succeeded = false,
            diagnostics = new[]
            {
                new Diagnostic { Severity = DiagnosticSeverity.Error, Code = code, Message = message },
            },
        }));
        return 1;
    }

    private sealed record Options(string SnapshotPath, string NativeLibraryPath)
    {
        public static Options Parse(string[] args)
        {
            string? snapshot = null;
            string? native = null;
            for (var index = 0; index < args.Length; index++)
            {
                switch (args[index])
                {
                    case "--snapshot" when index + 1 < args.Length: snapshot = args[++index]; break;
                    case "--native" when index + 1 < args.Length: native = args[++index]; break;
                }
            }
            if (snapshot is null || native is null)
            {
                throw new ArgumentException("Usage: worker --snapshot <scene.json> --native <library>");
            }
            return new Options(Path.GetFullPath(snapshot), Path.GetFullPath(native));
        }
    }
}
