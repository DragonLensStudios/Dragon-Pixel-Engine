namespace DragonPixel.Contracts;

[Flags]
public enum FrameworkCapabilities
{
    None = 0,
    Sprite2D = 1 << 0,
    StaticMesh3D = 1 << 1,
    BasicLighting = 1 << 2,
    Bgra8SharedFrames = 1 << 3,
    RevisionedFrames = 1 << 4,
    IdBufferPicking = 1 << 5,
    ViewportResize = 1 << 6,
    Physics2D = 1 << 7,
    Physics3D = 1 << 8,
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
    public long CameraRevision { get; set; }
}

public sealed class EditorCamera
{
    public bool Orthographic { get; set; }
    public float[] Position { get; set; } = [0, 2, 7];
    public float[] Target { get; set; } = [0, 0, 0];
    public float FieldOfViewDegrees { get; set; } = 60;
    public float OrthographicSize { get; set; } = 10;
}

public sealed class ViewportRequest
{
    public int Width { get; set; }
    public int Height { get; set; }
    public long SnapshotRevision { get; set; }
    public long CameraRevision { get; set; }
    public long CommandRevision { get; set; }
    public EditorCamera Camera { get; set; } = new();
    public IReadOnlyList<DpeId> Selection { get; set; } = Array.Empty<DpeId>();
}

public sealed class PickResult
{
    public DpeId? EntityId { get; set; }
    public long FrameRevision { get; set; }
    public long SnapshotRevision { get; set; }
    public long CameraRevision { get; set; }
    public long CommandRevision { get; set; }
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
