using System.Security.Cryptography;
using System.Text;
using System.Xml;
using System.Xml.Linq;

namespace DragonPixel.PocD.Scanner;

internal static class ProjectScanner
{
    private static readonly string[] SourcePatterns =
    [
        "Microsoft.Xna.Framework",
        "GraphicsDevice",
        "SpriteBatch",
        "Content.Load<",
        "GameTime",
    ];

    public static ScanReport Scan(string root)
    {
        root = Path.GetFullPath(root);
        if (!Directory.Exists(root))
        {
            throw new DirectoryNotFoundException(root);
        }

        var files = Directory.EnumerateFiles(root, "*", SearchOption.AllDirectories)
            .OrderBy(path => Relative(root, path), StringComparer.Ordinal)
            .ToArray();
        var projects = files
            .Where(path => string.Equals(Path.GetExtension(path), ".csproj", StringComparison.OrdinalIgnoreCase))
            .Select(path => ReadProject(root, path))
            .ToArray();
        var content = files
            .Where(path => IsContent(path))
            .Select(path => new ContentFinding(Relative(root, path), ContentKind(path)))
            .ToArray();
        var sourceSignals = files
            .Where(path => string.Equals(Path.GetExtension(path), ".cs", StringComparison.OrdinalIgnoreCase))
            .SelectMany(path => ReadSourceSignals(root, path))
            .ToArray();
        var frameworks = projects.SelectMany(project => project.Packages)
            .Select(package => package.FrameworkFamily)
            .Where(family => family != "Other")
            .Distinct(StringComparer.Ordinal)
            .OrderBy(family => family, StringComparer.Ordinal)
            .ToArray();
        var warnings = new List<string>
        {
            "Read-only analysis does not prove behavioral compatibility or convert gameplay intent.",
        };
        if (frameworks.Contains("KNI", StringComparer.Ordinal))
        {
            warnings.Add("KNI remains experimental until the dedicated .NET 10 conformance matrix passes.");
        }
        if (projects.Length == 0)
        {
            warnings.Add("No C# project files were found.");
        }

        return new ScanReport(
            "dpe.migration-scan",
            1,
            ComputeTreeHash(root, files),
            projects,
            content,
            sourceSignals,
            frameworks,
            warnings);
    }

    private static ProjectFinding ReadProject(string root, string path)
    {
        var settings = new XmlReaderSettings
        {
            DtdProcessing = DtdProcessing.Prohibit,
            XmlResolver = null,
        };
        using var stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read);
        using var reader = XmlReader.Create(stream, settings);
        var document = XDocument.Load(reader, LoadOptions.None);
        var targetFrameworks = document.Descendants()
            .Where(element => element.Name.LocalName is "TargetFramework" or "TargetFrameworks")
            .SelectMany(element => element.Value.Split(';', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries))
            .Distinct(StringComparer.Ordinal)
            .OrderBy(value => value, StringComparer.Ordinal)
            .ToArray();
        var packages = document.Descendants()
            .Where(element => element.Name.LocalName == "PackageReference")
            .Select(element =>
            {
                var id = element.Attribute("Include")?.Value ?? element.Attribute("Update")?.Value ?? "unknown";
                var version = element.Attribute("Version")?.Value
                    ?? element.Elements().FirstOrDefault(child => child.Name.LocalName == "Version")?.Value
                    ?? "unspecified";
                return new PackageFinding(id, version, ClassifyPackage(id));
            })
            .OrderBy(package => package.Id, StringComparer.Ordinal)
            .ToArray();
        return new ProjectFinding(Relative(root, path), targetFrameworks, packages);
    }

    private static IEnumerable<SourceSignal> ReadSourceSignals(string root, string path)
    {
        using var stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read);
        using var reader = new StreamReader(stream, Encoding.UTF8, detectEncodingFromByteOrderMarks: true);
        var lineNumber = 0;
        while (reader.ReadLine() is { } line)
        {
            lineNumber++;
            foreach (var pattern in SourcePatterns)
            {
                if (line.Contains(pattern, StringComparison.Ordinal))
                {
                    yield return new SourceSignal(Relative(root, path), lineNumber, pattern);
                }
            }
        }
    }

    private static string ComputeTreeHash(string root, IEnumerable<string> files)
    {
        using var incremental = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
        foreach (var file in files)
        {
            var relativeBytes = Encoding.UTF8.GetBytes(Relative(root, file) + "\n");
            incremental.AppendData(relativeBytes);
            using var stream = new FileStream(file, FileMode.Open, FileAccess.Read, FileShare.Read);
            var buffer = new byte[64 * 1024];
            int read;
            while ((read = stream.Read(buffer, 0, buffer.Length)) > 0)
            {
                incremental.AppendData(buffer, 0, read);
            }
        }

        return Convert.ToHexString(incremental.GetHashAndReset()).ToLowerInvariant();
    }

    private static bool IsContent(string path) => Path.GetExtension(path).ToLowerInvariant() is
        ".mgcb" or ".png" or ".jpg" or ".jpeg" or ".wav" or ".ogg" or ".fbx" or ".gltf" or ".glb" or ".xnb";

    private static string ContentKind(string path) => Path.GetExtension(path).ToLowerInvariant() switch
    {
        ".mgcb" => "content-pipeline-project",
        ".png" or ".jpg" or ".jpeg" => "texture-source",
        ".wav" or ".ogg" => "audio-source",
        ".fbx" or ".gltf" or ".glb" => "model-source",
        ".xnb" => "compiled-content",
        _ => "unknown",
    };

    private static string ClassifyPackage(string packageId)
    {
        if (packageId.StartsWith("MonoGame.", StringComparison.OrdinalIgnoreCase))
        {
            return "MonoGame";
        }
        if (packageId.StartsWith("nkast.", StringComparison.OrdinalIgnoreCase)
            || packageId.Contains("KNI", StringComparison.OrdinalIgnoreCase))
        {
            return "KNI";
        }
        return "Other";
    }

    private static string Relative(string root, string path) =>
        Path.GetRelativePath(root, path).Replace('\\', '/');
}
