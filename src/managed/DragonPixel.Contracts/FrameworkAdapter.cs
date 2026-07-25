namespace DragonPixel.Contracts;

[Flags]
public enum FrameworkCapabilities
{
    None = 0,
    Sprite2D = 1 << 0,
    StaticMesh3D = 1 << 1,
    BasicLighting = 1 << 2,
    Bgra8SharedFrames = 1 << 3,
}

public enum RuntimeState
{
    Uninitialized,
    Stopped,
    Running,
    Paused,
    Faulted,
    Shutdown,
}

public sealed class AdapterInitialization
{
    public int Width { get; set; }
    public int Height { get; set; }
    public string FrameTransport { get; set; } = string.Empty;
}

public interface IFrameworkAdapter : IAsyncDisposable
{
    string Name { get; }
    string Version { get; }
    bool Experimental { get; }
    FrameworkCapabilities Capabilities { get; }
    RuntimeState State { get; }

    ValueTask InitializeAsync(AdapterInitialization initialization, CancellationToken cancellationToken);
    ValueTask LoadSnapshotAsync(SceneSnapshot snapshot, CancellationToken cancellationToken);
    ValueTask UpdateAsync(TimeSpan elapsed, CancellationToken cancellationToken);
    ValueTask RenderAsync(CancellationToken cancellationToken);
    ValueTask PauseAsync(CancellationToken cancellationToken);
    ValueTask ResumeAsync(CancellationToken cancellationToken);
    ValueTask StopAsync(CancellationToken cancellationToken);
    ValueTask<IReadOnlyList<Diagnostic>> GetDiagnosticsAsync(CancellationToken cancellationToken);
    ValueTask ShutdownAsync(CancellationToken cancellationToken);
}
