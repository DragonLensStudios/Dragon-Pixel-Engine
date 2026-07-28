using System.Runtime.InteropServices;

namespace DragonPixel.NativeInterop;

public enum DpeStatus
{
    Ok = 0,
    InvalidArgument = 1,
    AbiVersionUnsupported = 2,
    InvalidHandle = 3,
    BufferTooSmall = 4,
    OutOfMemory = 5,
    InternalError = 6,
    CallbackFailure = 7,
    InvalidUtf8 = 8,
}

[Flags]
public enum DpeCapabilities : ulong
{
    StructuredErrors = 1UL << 0,
    TrackedAllocator = 1UL << 1,
    LiveCounts = 1UL << 2,
    NativeCounterComponent = 1UL << 3,
    ManagedCallback = 1UL << 4,
    PhysicsV1 = 1UL << 5,
}

[StructLayout(LayoutKind.Sequential)]
internal struct DpeApiV1
{
    internal uint StructSize;
    internal uint AbiMajor;
    internal uint AbiMinor;
    internal uint Reserved;
    internal ulong Capabilities;
    internal nint CreateRuntime;
    internal nint DestroyRuntime;
    internal nint CreateWorld;
    internal nint DestroyWorld;
    internal nint CreateEntity;
    internal nint DestroyEntity;
    internal nint GetEntityName;
    internal nint AttachCounterComponent;
    internal nint DestroyComponent;
    internal nint GetCounterValue;
    internal nint SetCounterValue;
    internal nint Allocate;
    internal nint Deallocate;
    internal nint GetLiveCounts;
    internal nint GetLastError;
    internal nint ForceNativeException;
    internal nint InvokeManagedCallback;
    internal nint AcquirePhysicsApi;
}

internal enum DpePhysicsDimension : uint
{
    TwoD = 0,
    ThreeD = 1,
}

internal enum DpePhysicsShape : uint
{
    Box = 0,
    CircleOrSphere = 1,
    Polygon2D = 2,
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct DpePhysicsWorldSettingsV1
{
    internal uint StructSize;
    internal uint MaximumCatchUpTicks;
    internal uint Box2DSolverSubsteps;
    internal uint JoltCollisionSteps;
    internal double FixedTimeStepSeconds;
    internal fixed double Gravity2D[2];
    internal fixed double Gravity3D[3];
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct DpePhysicsColliderV1
{
    internal uint StructSize;
    internal uint Shape;
    internal uint Sensor;
    internal uint Reserved;
    internal fixed double Size[3];
    internal fixed double Offset[3];
    internal double Density;
    internal double Friction;
    internal double Restitution;
    internal ushort Layer;
    internal ushort Mask;
    internal uint Reserved2;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct DpePhysicsColliderV2
{
    internal uint StructSize;
    internal uint Shape;
    internal uint Sensor;
    internal uint Reserved;
    internal fixed double Size[3];
    internal fixed double Offset[3];
    internal double Density;
    internal double Friction;
    internal double Restitution;
    internal ushort Layer;
    internal ushort Mask;
    internal uint Reserved2;
    internal uint VertexCount;
    internal uint Reserved3;
    internal fixed double Vertices[16];
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct DpePhysicsBodyV1
{
    internal uint StructSize;
    internal uint Dimension;
    internal uint Mode;
    internal uint Flags;
    internal fixed byte EntityUuid[16];
    internal fixed double Position[3];
    internal fixed double Rotation[4];
    internal fixed double LinearVelocity[3];
    internal fixed double AngularVelocity[3];
    internal double LinearDamping;
    internal double AngularDamping;
    internal double GravityScale;
    internal uint ColliderStart;
    internal uint ColliderCount;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct DpePhysicsCommandV1
{
    internal uint StructSize;
    internal uint Kind;
    internal fixed uint Reserved[2];
    internal fixed byte EntityUuid[16];
    internal fixed double Value[3];
    internal fixed double Rotation[4];
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct DpePhysicsTransformV1
{
    internal uint StructSize;
    internal uint Reserved;
    internal fixed byte EntityUuid[16];
    internal fixed double Position[3];
    internal fixed double Rotation[4];
    internal fixed double LinearVelocity[3];
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct DpePhysicsContactV1
{
    internal uint StructSize;
    internal uint Kind;
    internal ulong Tick;
    internal fixed byte EntityAUuid[16];
    internal fixed byte EntityBUuid[16];
    internal fixed double Point[3];
    internal fixed double Normal[3];
}

[StructLayout(LayoutKind.Sequential)]
internal struct DpePhysicsStepResultV1
{
    internal uint StructSize;
    internal uint Ticks;
    internal uint Flags;
    internal uint Reserved;
    internal ulong WorldTick;
    internal double DroppedSeconds;
    internal nuint TransformCount;
    internal nuint PendingContactCount;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct DpePhysicsRaycastV1
{
    internal uint StructSize;
    internal uint Dimension;
    internal fixed double Origin[3];
    internal fixed double Direction[3];
    internal double Distance;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct DpePhysicsRaycastHitV1
{
    internal uint StructSize;
    internal uint Hit;
    internal fixed byte EntityUuid[16];
    internal double Fraction;
    internal fixed double Point[3];
    internal fixed double Normal[3];
}

[StructLayout(LayoutKind.Sequential)]
internal struct DpePhysicsApiV1
{
    internal uint StructSize;
    internal uint AbiMajor;
    internal uint AbiMinor;
    internal uint Reserved;
    internal nint CreateWorld;
    internal nint DestroyWorld;
    internal nint Rebuild;
    internal nint ApplyCommands;
    internal nint Step;
    internal nint CopyTransforms;
    internal nint DrainContacts;
    internal nint Raycast;
    internal nint RebuildV2;
}
