namespace DragonPixel.PocD.Scanner;

internal sealed record ScanReport(
    string Format,
    int FormatVersion,
    string InspectedTreeHash,
    IReadOnlyList<ProjectFinding> Projects,
    IReadOnlyList<ContentFinding> Content,
    IReadOnlyList<SourceSignal> SourceSignals,
    IReadOnlyList<string> Frameworks,
    IReadOnlyList<string> Warnings);

internal sealed record ProjectFinding(
    string Path,
    IReadOnlyList<string> TargetFrameworks,
    IReadOnlyList<PackageFinding> Packages);

internal sealed record PackageFinding(string Id, string Version, string FrameworkFamily);

internal sealed record ContentFinding(string Path, string Kind);

internal sealed record SourceSignal(string Path, int Line, string Signal);
