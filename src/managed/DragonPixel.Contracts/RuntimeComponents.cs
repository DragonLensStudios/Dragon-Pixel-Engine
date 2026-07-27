using System.Collections.ObjectModel;
using System.Numerics;

namespace DragonPixel.Contracts;

public enum RuntimeInputActionKind
{
    Button,
    Axis1D,
    Axis2D,
}

public sealed class RuntimeInputActionState
{
    public RuntimeInputActionState(
        RuntimeInputActionKind kind,
        float x,
        float y,
        ulong pressCount,
        ulong releaseCount)
    {
        Kind = kind;
        X = x;
        Y = y;
        PressCount = pressCount;
        ReleaseCount = releaseCount;
    }

    public RuntimeInputActionKind Kind { get; }
    public float X { get; }
    public float Y { get; }
    public float Value => X;
    public ulong PressCount { get; }
    public ulong ReleaseCount { get; }
}

public sealed class RuntimeInputSnapshot
{
    private readonly IReadOnlyDictionary<string, RuntimeInputActionState> _actions;

    public RuntimeInputSnapshot(
        long revision,
        bool focused,
        bool captured,
        IReadOnlyDictionary<string, RuntimeInputActionState> actions)
    {
        Revision = revision;
        Focused = focused;
        Captured = captured;
        if (actions is null) throw new ArgumentNullException(nameof(actions));
        var copy = new Dictionary<string, RuntimeInputActionState>(StringComparer.Ordinal);
        foreach (var (name, state) in actions)
        {
            copy.Add(name, state);
        }
        _actions = new ReadOnlyDictionary<string, RuntimeInputActionState>(copy);
    }

    public static RuntimeInputSnapshot Neutral { get; } = new(
        0,
        false,
        false,
        new Dictionary<string, RuntimeInputActionState>(StringComparer.Ordinal));

    public long Revision { get; }
    public bool Focused { get; }
    public bool Captured { get; }
    public IReadOnlyDictionary<string, RuntimeInputActionState> Actions => _actions;
}

public enum ProjectComponentDiagnosticSeverity
{
    Info,
    Warning,
    Error,
}

/// <summary>
/// Worker-owned transform state shared by every managed controller on one GameObject.
/// Rotation is expressed as Euler angles in degrees.
/// </summary>
public sealed class Transform
{
    private readonly object _gate = new();
    private Vector3 _position;
    private Vector3 _rotation;
    private Vector3 _scale;
    private long _revision;

    public Transform()
        : this(Vector3.Zero, Vector3.Zero, Vector3.One)
    {
    }

    public Transform(Vector3 position, Vector3 rotation, Vector3 scale)
    {
        _position = position;
        _rotation = rotation;
        _scale = scale;
    }

    public Vector3 Position
    {
        get { lock (_gate) return _position; }
        set
        {
            lock (_gate)
            {
                if (_position == value) return;
                _position = value;
                _revision++;
            }
        }
    }

    public Vector3 Rotation
    {
        get { lock (_gate) return _rotation; }
        set
        {
            lock (_gate)
            {
                if (_rotation == value) return;
                _rotation = value;
                _revision++;
            }
        }
    }

    public Vector3 Scale
    {
        get { lock (_gate) return _scale; }
        set
        {
            lock (_gate)
            {
                if (_scale == value) return;
                _scale = value;
                _revision++;
            }
        }
    }

    /// <summary>Monotonically increases after an effective runtime mutation.</summary>
    public long Revision
    {
        get { lock (_gate) return _revision; }
    }

    internal (Vector3 Position, Vector3 Rotation, Vector3 Scale, long Revision) GetRuntimeState()
    {
        lock (_gate) return (_position, _rotation, _scale, _revision);
    }

    internal void ResetRuntimeState(Vector3 position, Vector3 rotation, Vector3 scale)
    {
        lock (_gate)
        {
            _position = position;
            _rotation = rotation;
            _scale = scale;
            _revision = 0;
        }
    }

    public void Translate(Vector3 displacement)
    {
        if (displacement == Vector3.Zero) return;
        lock (_gate)
        {
            _position += displacement;
            _revision++;
        }
    }
}

public sealed class ProjectComponentContext
{
    private readonly Action<ProjectComponentDiagnosticSeverity, string> _diagnostic;

    public ProjectComponentContext(
        string entityId,
        string componentTypeId,
        Action<ProjectComponentDiagnosticSeverity, string> diagnostic)
        : this(entityId, componentTypeId, diagnostic, new Transform())
    {
    }

    public ProjectComponentContext(
        string entityId,
        string componentTypeId,
        Action<ProjectComponentDiagnosticSeverity, string> diagnostic,
        Transform transform)
    {
        EntityId = entityId;
        ComponentTypeId = componentTypeId;
        _diagnostic = diagnostic ?? throw new ArgumentNullException(nameof(diagnostic));
        Transform = transform ?? throw new ArgumentNullException(nameof(transform));
    }

    public string EntityId { get; }
    public string ComponentTypeId { get; }
    public Transform Transform { get; }

    public void ReportDiagnostic(ProjectComponentDiagnosticSeverity severity, string message) =>
        _diagnostic(severity, message);
}

public sealed class ProjectComponentUpdate
{
    public ProjectComponentUpdate(
        double elapsedSeconds,
        double deltaSeconds,
        IReadOnlyDictionary<string, float> inputActions)
    {
        ElapsedSeconds = elapsedSeconds;
        DeltaSeconds = deltaSeconds;
        InputActions = CopyInputActions(inputActions);
        InputState = RuntimeInputSnapshot.Neutral;
    }

    public ProjectComponentUpdate(
        double elapsedSeconds,
        double deltaSeconds,
        IReadOnlyDictionary<string, float> inputActions,
        RuntimeInputSnapshot inputState)
    {
        ElapsedSeconds = elapsedSeconds;
        DeltaSeconds = deltaSeconds;
        InputActions = CopyInputActions(inputActions);
        InputState = inputState ?? throw new ArgumentNullException(nameof(inputState));
    }

    public double ElapsedSeconds { get; }
    public double DeltaSeconds { get; }
    public IReadOnlyDictionary<string, float> InputActions { get; }
    public RuntimeInputSnapshot InputState { get; }

    private static IReadOnlyDictionary<string, float> CopyInputActions(
        IReadOnlyDictionary<string, float> inputActions)
    {
        if (inputActions is null) throw new ArgumentNullException(nameof(inputActions));
        var copy = new Dictionary<string, float>(StringComparer.Ordinal);
        foreach (var (name, value) in inputActions)
        {
            copy.Add(name, value);
        }
        return new ReadOnlyDictionary<string, float>(copy);
    }
}

public interface IProjectComponent
{
    void Initialize(ProjectComponentContext context, string propertiesJson);
    void PropertiesChanged(string propertiesJson);
    void Update(ProjectComponentUpdate update);
    void Shutdown();
}

/// <summary>
/// Optional full worker lifecycle for project components. Initialize and Shutdown on
/// <see cref="IProjectComponent"/> remain the compatible Create and Destroy stages.
/// </summary>
public interface IProjectComponentLifecycle
{
    void OnEnable();
    void FixedUpdate(ProjectComponentUpdate update);
    void LateUpdate(ProjectComponentUpdate update);
    void SubmitRender(ProjectComponentUpdate update);
    void OnDisable();
}

/// <summary>
/// Concise author-facing lifespan implemented by managed GameObject scripts.
/// </summary>
public interface IGameObjectControllerLifecycle
{
    void Enabled();
    void Disabled();
    void Update();
    void FixedUpdate();
}

/// <summary>
/// Immutable input view for the currently dispatched controller stage.
/// </summary>
public sealed class GameObjectInput
{
    private IReadOnlyDictionary<string, float> _actions =
        new ReadOnlyDictionary<string, float>(new Dictionary<string, float>(StringComparer.Ordinal));

    internal void Set(ProjectComponentUpdate update)
    {
        _actions = update.InputActions;
        State = update.InputState;
    }

    internal void Reset()
    {
        _actions = new ReadOnlyDictionary<string, float>(
            new Dictionary<string, float>(StringComparer.Ordinal));
        State = RuntimeInputSnapshot.Neutral;
    }

    public RuntimeInputSnapshot State { get; private set; } = RuntimeInputSnapshot.Neutral;
    public bool Focused => State.Focused;
    public bool Captured => State.Captured;

    public float GetAction(string name)
    {
        if (string.IsNullOrWhiteSpace(name)) throw new ArgumentException("An input action name is required.", nameof(name));
        return _actions.TryGetValue(name, out var value) ? value : 0.0f;
    }

    public bool TryGetState(string name, out RuntimeInputActionState? state)
    {
        if (string.IsNullOrWhiteSpace(name)) throw new ArgumentException("An input action name is required.", nameof(name));
        return State.Actions.TryGetValue(name, out state);
    }
}

/// <summary>
/// Recommended base class for editable C# scripts attached to a GameObject.
/// The explicit legacy interfaces remain the worker transport and compatibility layer.
/// </summary>
public abstract class GameObjectController :
    IProjectComponent,
    IProjectComponentLifecycle,
    IGameObjectControllerLifecycle
{
    private ProjectComponentContext? _context;

    public Guid Id { get; private set; }
    public Transform Transform { get; private set; } = new();
    public GameObjectInput Input { get; } = new();
    public bool IsEnabled { get; private set; }
    public double ElapsedSeconds { get; private set; }
    public float DeltaTime { get; private set; }
    public float FixedDeltaTime { get; private set; }

    protected ProjectComponentContext Context => _context
        ?? throw new InvalidOperationException("The controller is not bound to a worker GameObject.");

    public virtual void Enabled() { }
    public virtual void Disabled() { }
    public virtual void Update() { }
    public virtual void FixedUpdate() { }
    public virtual void PropertiesChanged(string propertiesJson) { }

    void IProjectComponent.Initialize(ProjectComponentContext context, string propertiesJson)
    {
        _context = context ?? throw new ArgumentNullException(nameof(context));
        if (!Guid.TryParse(context.EntityId, out var id))
            throw new ArgumentException("A GameObjectController requires a valid GUID entity ID.", nameof(context));
        Id = id;
        Transform = context.Transform;
        PropertiesChanged(propertiesJson);
    }

    void IProjectComponent.Update(ProjectComponentUpdate update)
    {
        SetCurrentUpdate(update);
        Update();
    }

    void IProjectComponentLifecycle.FixedUpdate(ProjectComponentUpdate update)
    {
        SetCurrentUpdate(update);
        FixedDeltaTime = checked((float)update.DeltaSeconds);
        FixedUpdate();
    }

    void IProjectComponentLifecycle.OnEnable()
    {
        IsEnabled = true;
        Enabled();
    }

    void IProjectComponentLifecycle.OnDisable()
    {
        try
        {
            Disabled();
        }
        finally
        {
            IsEnabled = false;
        }
    }

    void IProjectComponentLifecycle.LateUpdate(ProjectComponentUpdate update) { }
    void IProjectComponentLifecycle.SubmitRender(ProjectComponentUpdate update) { }

    void IProjectComponent.Shutdown()
    {
        _context = null;
        Id = Guid.Empty;
        Transform = new Transform();
        Input.Reset();
        IsEnabled = false;
        ElapsedSeconds = 0.0;
        DeltaTime = 0.0f;
        FixedDeltaTime = 0.0f;
    }

    private void SetCurrentUpdate(ProjectComponentUpdate update)
    {
        if (update is null) throw new ArgumentNullException(nameof(update));
        ElapsedSeconds = update.ElapsedSeconds;
        DeltaTime = checked((float)update.DeltaSeconds);
        Input.Set(update);
    }
}

public interface IProjectComponentFactory
{
    string TypeId { get; }
    IProjectComponent Create();
}
