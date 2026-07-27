using System.Numerics;
using System.Text.Json;
using DragonPixel.Contracts;

namespace DragonPixel.ProjectComponents;

[DpeComponent("6bafa5b1-ae76-4cb7-a5a7-ac71c95efb6e", "DragonPixel.ProjectComponents.MyMover", "MyMover", 1, ComponentOwner.Managed)]
public sealed class MyMover : GameObjectController
{
    private const string HorizontalActionPropertyId = "6bafa5b1-ae76-4cb7-a5a7-ac71c95efb6e.horizontal_action";
    private const string VerticalActionPropertyId = "6bafa5b1-ae76-4cb7-a5a7-ac71c95efb6e.vertical_action";
    private const string SpeedPropertyId = "6bafa5b1-ae76-4cb7-a5a7-ac71c95efb6e.speed";

    public string HorizontalAction { get; private set; } = "move.x";
    public string VerticalAction { get; private set; } = "move.y";
    public float Speed { get; private set; } = 5.0f;

    public override void Enabled() { }

    public override void Update()
    {
        var direction = new Vector3(
            Input.GetAction(HorizontalAction),
            Input.GetAction(VerticalAction),
            0.0f);
        if (direction.LengthSquared() > 1.0f)
            direction = Vector3.Normalize(direction);
        Transform.Translate(direction * Speed * DeltaTime);
    }

    public override void FixedUpdate() { }

    public override void Disabled() { }

    public override void PropertiesChanged(string propertiesJson)
    {
        using var document = JsonDocument.Parse(propertiesJson);
        if (document.RootElement.TryGetProperty(HorizontalActionPropertyId, out var horizontalAction)
            && horizontalAction.ValueKind == JsonValueKind.String
            && !string.IsNullOrWhiteSpace(horizontalAction.GetString()))
        {
            HorizontalAction = horizontalAction.GetString()!;
        }
        if (document.RootElement.TryGetProperty(VerticalActionPropertyId, out var verticalAction)
            && verticalAction.ValueKind == JsonValueKind.String
            && !string.IsNullOrWhiteSpace(verticalAction.GetString()))
        {
            VerticalAction = verticalAction.GetString()!;
        }
        if (document.RootElement.TryGetProperty(SpeedPropertyId, out var speed)
            && speed.ValueKind == JsonValueKind.Number
            && speed.TryGetSingle(out var value))
        {
            Speed = Math.Clamp(value, 0.0f, 1000.0f);
        }
    }
}

public sealed class MyMoverFactory : IProjectComponentFactory
{
    public string TypeId => "6bafa5b1-ae76-4cb7-a5a7-ac71c95efb6e";
    public IProjectComponent Create() => new MyMover();
}
