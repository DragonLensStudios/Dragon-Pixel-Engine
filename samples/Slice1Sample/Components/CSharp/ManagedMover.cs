using DragonPixel.Contracts;

namespace Sample;

[DpeComponent(
    "ee23acae-dd2f-449d-a308-19ea39c15106",
    "Sample.ManagedMoverComponent",
    "Managed Mover",
    1,
    ComponentOwner.Managed)]
public sealed class ManagedMoverComponent : IProjectComponent
{
    private ProjectComponentContext? _context;
    private string _propertiesJson = "{}";
    private float _lastMoveInput;

    public void Initialize(ProjectComponentContext context, string propertiesJson)
    {
        _context = context;
        _propertiesJson = propertiesJson;
        context.ReportDiagnostic(
            ProjectComponentDiagnosticSeverity.Info,
            "Managed Mover initialized in the isolated framework worker.");
    }

    public void PropertiesChanged(string propertiesJson) => _propertiesJson = propertiesJson;

    public void Update(ProjectComponentUpdate update)
    {
        _lastMoveInput = update.InputActions.TryGetValue("move.x", out var value) ? value : 0.0f;
        _ = _propertiesJson.Length;
    }

    public void Shutdown()
    {
        _context = null;
        _lastMoveInput = 0.0f;
    }
}

public sealed class ManagedMoverFactory : IProjectComponentFactory
{
    public string TypeId => "ee23acae-dd2f-449d-a308-19ea39c15106";
    public IProjectComponent Create() => new ManagedMoverComponent();
}
