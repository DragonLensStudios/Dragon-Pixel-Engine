namespace DragonPixel.PocD.Scanner;

internal static class Program
{
    private static int Main(string[] args)
    {
        try
        {
            var options = Options.Parse(args);
            RejectOutputWithinInput(options.Input, options.JsonOutput);
            RejectOutputWithinInput(options.Input, options.MarkdownOutput);
            var report = ProjectScanner.Scan(options.Input);
            ReportWriter.WriteJson(options.JsonOutput, report);
            ReportWriter.WriteMarkdown(options.MarkdownOutput, report);
            Console.WriteLine($"Scanned {report.Projects.Count} project(s); tree hash {report.InspectedTreeHash}.");
            return 0;
        }
        catch (Exception exception)
        {
            Console.Error.WriteLine(exception.Message);
            return 1;
        }
    }

    private static void RejectOutputWithinInput(string input, string output)
    {
        var root = Path.GetFullPath(input).TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar)
            + Path.DirectorySeparatorChar;
        var target = Path.GetFullPath(output);
        if (target.StartsWith(root, OperatingSystem.IsWindows() ? StringComparison.OrdinalIgnoreCase : StringComparison.Ordinal))
        {
            throw new InvalidOperationException("Reports must be written outside the inspected source tree.");
        }
    }

    private sealed record Options(string Input, string JsonOutput, string MarkdownOutput)
    {
        public static Options Parse(string[] args)
        {
            string? input = null;
            string? json = null;
            string? markdown = null;
            for (var index = 0; index < args.Length; index++)
            {
                switch (args[index])
                {
                    case "--input" when index + 1 < args.Length: input = args[++index]; break;
                    case "--json" when index + 1 < args.Length: json = args[++index]; break;
                    case "--markdown" when index + 1 < args.Length: markdown = args[++index]; break;
                }
            }

            if (input is null || json is null || markdown is null)
            {
                throw new ArgumentException("Usage: scanner --input <root> --json <report.json> --markdown <report.md>");
            }
            return new Options(input, json, markdown);
        }
    }
}
