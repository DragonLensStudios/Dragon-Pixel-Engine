namespace DragonPixel.Contracts;

public enum PhysicsBodyMode
{
    Static,
    Kinematic,
    Dynamic,
}

public readonly struct PhysicsVector2
{
    public PhysicsVector2(double x, double y) { X = x; Y = y; }
    public double X { get; }
    public double Y { get; }
}

public readonly struct PhysicsVector3
{
    public PhysicsVector3(double x, double y, double z) { X = x; Y = y; Z = z; }
    public double X { get; }
    public double Y { get; }
    public double Z { get; }
}

public readonly struct PhysicsQuaternion
{
    public PhysicsQuaternion(double x, double y, double z, double w) { X = x; Y = y; Z = z; W = w; }
    public double X { get; }
    public double Y { get; }
    public double Z { get; }
    public double W { get; }
}

public sealed class ScenePhysicsSettings
{
    public double FixedTimeStepSeconds { get; set; } = 1.0 / 60.0;
    public int MaximumCatchUpTicks { get; set; } = 4;
    public int Box2DSolverSubsteps { get; set; } = 4;
    public int JoltCollisionSteps { get; set; } = 1;
    public PhysicsVector2 Gravity2D { get; set; } = new(0, -9.81);
    public PhysicsVector3 Gravity3D { get; set; } = new(0, -9.81, 0);
}

public enum PhysicsCommandKind
{
    Force,
    Impulse,
    SetLinearVelocity,
    SetAngularVelocity,
    Teleport,
}

public sealed class PhysicsCommand
{
    public PhysicsCommandKind Kind { get; set; }
    public DpeId EntityId { get; set; }
    public PhysicsVector3 Value { get; set; }
    public PhysicsQuaternion Rotation { get; set; } = new(0, 0, 0, 1);
}

public sealed class PhysicsTransform
{
    public DpeId EntityId { get; set; }
    public PhysicsVector3 Position { get; set; }
    public PhysicsQuaternion Rotation { get; set; } = new(0, 0, 0, 1);
}

public enum PhysicsContactKind
{
    BeginContact,
    EndContact,
    BeginTrigger,
    EndTrigger,
}

public sealed class PhysicsContact
{
    public long Tick { get; set; }
    public PhysicsContactKind Kind { get; set; }
    public DpeId EntityA { get; set; }
    public DpeId EntityB { get; set; }
    public PhysicsVector3 Point { get; set; }
    public PhysicsVector3 Normal { get; set; }
}

public sealed class PhysicsRaycastResult
{
    public bool Hit { get; set; }
    public DpeId? EntityId { get; set; }
    public double Fraction { get; set; }
    public PhysicsVector3 Point { get; set; }
    public PhysicsVector3 Normal { get; set; }
}
