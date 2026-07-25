namespace DragonPixel.Contracts;

public sealed class CommandEnvelope<TCommand>
{
    public string ProtocolVersion { get; set; } = "1.0";
    public DpeId RequestId { get; set; }
    public string CapabilityToken { get; set; } = string.Empty;
    public TCommand? Command { get; set; }
}

public sealed class CommandResult<TResult>
{
    public DpeId RequestId { get; set; }
    public bool Succeeded { get; set; }
    public TResult? Result { get; set; }
    public IReadOnlyList<Diagnostic> Diagnostics { get; set; } = Array.Empty<Diagnostic>();
}

public enum DiagnosticSeverity
{
    Info,
    Warning,
    Error,
}

public sealed class Diagnostic
{
    public DiagnosticSeverity Severity { get; set; }
    public string Code { get; set; } = string.Empty;
    public string Message { get; set; } = string.Empty;
    public string Context { get; set; } = string.Empty;
}
