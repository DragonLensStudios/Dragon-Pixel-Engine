using System.Text;
using System.Text.Json;

namespace DragonPixel.PocD.Scanner;

internal static class ReportWriter
{
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
        WriteIndented = true,
    };

    public static void WriteJson(string path, ScanReport report)
    {
        EnsureOutputDirectory(path);
        var json = JsonSerializer.Serialize(report, JsonOptions).Replace("\r\n", "\n", StringComparison.Ordinal) + "\n";
        File.WriteAllText(path, json, new UTF8Encoding(encoderShouldEmitUTF8Identifier: false));
    }

    public static void WriteMarkdown(string path, ScanReport report)
    {
        EnsureOutputDirectory(path);
        var builder = new StringBuilder();
        builder.AppendLine("# Dragon Pixel Engine Read-Only Migration Scan")
            .AppendLine()
            .AppendLine($"> **Format:** `{report.Format}` v{report.FormatVersion}  ")
            .AppendLine($"> **Inspected tree SHA-256:** `{report.InspectedTreeHash}`")
            .AppendLine()
            .AppendLine("## Framework findings")
            .AppendLine();
        if (report.Frameworks.Count == 0)
        {
            builder.AppendLine("No MonoGame or KNI package references were detected.");
        }
        else
        {
            foreach (var framework in report.Frameworks)
            {
                builder.AppendLine($"- {framework}");
            }
        }

        builder.AppendLine().AppendLine("## Projects").AppendLine();
        foreach (var project in report.Projects)
        {
            builder.AppendLine($"### `{project.Path}`").AppendLine();
            builder.AppendLine($"Target frameworks: {string.Join(", ", project.TargetFrameworks.Select(value => $"`{value}`"))}").AppendLine();
            foreach (var package in project.Packages)
            {
                builder.AppendLine($"- `{package.Id}` {package.Version} — {package.FrameworkFamily}");
            }
            builder.AppendLine();
        }

        builder.AppendLine("## Content inventory").AppendLine();
        foreach (var content in report.Content)
        {
            builder.AppendLine($"- `{content.Path}` — {content.Kind}");
        }

        builder.AppendLine().AppendLine("## Source signals").AppendLine();
        foreach (var signal in report.SourceSignals)
        {
            builder.AppendLine($"- `{signal.Path}:{signal.Line}` — `{signal.Signal}`");
        }

        builder.AppendLine().AppendLine("## Warnings").AppendLine();
        foreach (var warning in report.Warnings)
        {
            builder.AppendLine($"- {warning}");
        }

        File.WriteAllText(
            path,
            builder.ToString().Replace("\r\n", "\n", StringComparison.Ordinal),
            new UTF8Encoding(encoderShouldEmitUTF8Identifier: false));
    }

    private static void EnsureOutputDirectory(string path)
    {
        var directory = Path.GetDirectoryName(Path.GetFullPath(path));
        if (directory is not null)
        {
            Directory.CreateDirectory(directory);
        }
    }
}
