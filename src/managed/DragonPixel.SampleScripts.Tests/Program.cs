using System.Numerics;
using DragonPixel.Contracts;
using DragonPixel.ProjectComponents;

const string EntityId = "798e6998-bb60-4837-83b9-57a15e3572d2";
const string TypeId = "6bafa5b1-ae76-4cb7-a5a7-ac71c95efb6e";

var transform = new Transform(Vector3.Zero, Vector3.Zero, Vector3.One);
var mover = new MyMover();
var component = (IProjectComponent)mover;
var lifecycle = (IProjectComponentLifecycle)mover;
var diagnostics = new List<string>();
component.Initialize(
    new ProjectComponentContext(
        EntityId,
        TypeId,
        (_, message) => diagnostics.Add(message),
        transform),
    """
    {
      "6bafa5b1-ae76-4cb7-a5a7-ac71c95efb6e.horizontal_action": "player.strafe",
      "6bafa5b1-ae76-4cb7-a5a7-ac71c95efb6e.vertical_action": "player.climb",
      "6bafa5b1-ae76-4cb7-a5a7-ac71c95efb6e.speed": 4.0
    }
    """);

Require(mover.Id == Guid.Parse(EntityId), "MyMover did not bind the GameObject GUID.");
Require(ReferenceEquals(mover.Transform, transform), "MyMover did not bind the shared GameObject Transform.");
Require(mover.HorizontalAction == "player.strafe" && mover.VerticalAction == "player.climb",
    "MyMover did not accept its Inspector-configured actions.");
Require(Math.Abs(mover.Speed - 4.0f) < 0.0001f, "MyMover did not accept its Inspector-configured speed.");

var actions = new Dictionary<string, RuntimeInputActionState>(StringComparer.Ordinal)
{
    ["player.strafe"] = new(RuntimeInputActionKind.Axis1D, 0.6f, 0.0f, 1, 0),
    ["player.climb"] = new(RuntimeInputActionKind.Axis1D, 0.8f, 0.0f, 1, 0),
};
var input = new RuntimeInputSnapshot(7, focused: true, captured: true, actions);
var update = new ProjectComponentUpdate(
    elapsedSeconds: 2.0,
    deltaSeconds: 0.5,
    inputActions: actions.ToDictionary(pair => pair.Key, pair => pair.Value.Value, StringComparer.Ordinal),
    inputState: input);

lifecycle.OnEnable();
component.Update(update);
Require(mover.IsEnabled, "MyMover did not enter its enabled lifecycle state.");
Require(Vector3.Distance(transform.Position, new Vector3(1.2f, 1.6f, 0.0f)) < 0.0001f,
    $"MyMover produced the wrong Transform displacement: {transform.Position}.");
lifecycle.OnDisable();
Require(!mover.IsEnabled, "MyMover did not leave its enabled lifecycle state.");

component.Shutdown();
Require(mover.Id == Guid.Empty && mover.Transform.Revision == 0,
    "MyMover did not release its disposable runtime binding on shutdown.");
Require(diagnostics.Count == 0, "MyMover emitted unexpected runtime diagnostics.");

Console.WriteLine("Exact MyMover script configurable-input runtime verification passed.");

static void Require(bool condition, string message)
{
    if (!condition) throw new InvalidOperationException(message);
}
