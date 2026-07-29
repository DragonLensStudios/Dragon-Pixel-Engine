using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using DragonPixel.Contracts;

namespace DragonPixel.NativeInterop;

public enum NativePhysicsDimension
{
    TwoD,
    ThreeD,
}

public enum NativePhysicsShape
{
    Box,
    CircleOrSphere,
    Polygon2D,
}

public sealed class NativePhysicsCollider
{
    public NativePhysicsShape Shape { get; set; } = NativePhysicsShape.Box;
    public PhysicsVector3 Size { get; set; } = new(1, 1, 1);
    public PhysicsVector3 Offset { get; set; }
    public IReadOnlyList<PhysicsVector2> Vertices { get; set; } = [];
    public bool Sensor { get; set; }
    public double Density { get; set; } = 1;
    public double Friction { get; set; } = 0.5;
    public double Restitution { get; set; }
    public ushort Layer { get; set; }
    public ushort Mask { get; set; } = ushort.MaxValue;
}

public sealed class NativePhysicsBody
{
    public DpeId EntityId { get; set; }
    public NativePhysicsDimension Dimension { get; set; } = NativePhysicsDimension.ThreeD;
    public PhysicsBodyMode Mode { get; set; } = PhysicsBodyMode.Dynamic;
    public PhysicsVector3 Position { get; set; }
    public PhysicsQuaternion Rotation { get; set; } = new(0, 0, 0, 1);
    public PhysicsVector3 LinearVelocity { get; set; }
    public PhysicsVector3 AngularVelocity { get; set; }
    public double LinearDamping { get; set; }
    public double AngularDamping { get; set; }
    public double GravityScale { get; set; } = 1;
    public bool ContinuousCollision { get; set; }
    public IReadOnlyList<NativePhysicsCollider> Colliders { get; set; } = [];
}

public readonly record struct NativePhysicsStepResult(
    uint Ticks,
    ulong WorldTick,
    double DroppedSeconds,
    bool DroppedTime,
    int TransformCount,
    int PendingContactCount);

public sealed class NativePhysicsTransform
{
    public DpeId EntityId { get; init; }
    public PhysicsVector3 Position { get; init; }
    public PhysicsQuaternion Rotation { get; init; }
    public PhysicsVector3 LinearVelocity { get; init; }
}

public sealed class NativePhysicsWorldHandle : SafeHandle
{
    private const uint ContinuousCollision = 1U << 0;
    private const uint DroppedTime = 1U << 0;
    private readonly DpePhysicsApiV1 _api;
    private readonly NativeRuntimeHandle _runtime;
    private readonly bool _runtimeReferenceAdded;

    internal NativePhysicsWorldHandle(DpePhysicsApiV1 api, NativeRuntimeHandle runtime, ulong value)
        : base(nint.Zero, ownsHandle: true)
    {
        if (value == 0)
        {
            throw new ArgumentOutOfRangeException(nameof(value));
        }
        _api = api;
        var added = false;
        runtime.DangerousAddRef(ref added);
        _runtime = runtime;
        _runtimeReferenceAdded = added;
        SetHandle(unchecked((nint)(long)value));
    }

    public override bool IsInvalid => handle == nint.Zero;

    public unsafe void Rebuild(IReadOnlyList<NativePhysicsBody> bodies)
    {
        ArgumentNullException.ThrowIfNull(bodies);
        var nativeBodies = new DpePhysicsBodyV1[bodies.Count];
        var colliderCount = 0;
        foreach (var body in bodies)
        {
            ArgumentNullException.ThrowIfNull(body);
            ArgumentNullException.ThrowIfNull(body.Colliders);
            colliderCount = checked(colliderCount + body.Colliders.Count);
        }
        var nativeColliders = new DpePhysicsColliderV2[colliderCount];
        var colliderIndex = 0;
        for (var bodyIndex = 0; bodyIndex < bodies.Count; ++bodyIndex)
        {
            var body = bodies[bodyIndex];
            DpePhysicsBodyV1 native = default;
            native.StructSize = (uint)sizeof(DpePhysicsBodyV1);
            native.Dimension = (uint)body.Dimension;
            native.Mode = (uint)body.Mode;
            native.Flags = body.ContinuousCollision ? ContinuousCollision : 0;
            WriteId(body.EntityId, native.EntityUuid);
            WriteVector(body.Position, native.Position);
            WriteQuaternion(body.Rotation, native.Rotation);
            WriteVector(body.LinearVelocity, native.LinearVelocity);
            WriteVector(body.AngularVelocity, native.AngularVelocity);
            native.LinearDamping = body.LinearDamping;
            native.AngularDamping = body.AngularDamping;
            native.GravityScale = body.GravityScale;
            native.ColliderStart = checked((uint)colliderIndex);
            native.ColliderCount = checked((uint)body.Colliders.Count);
            foreach (var collider in body.Colliders)
            {
                ArgumentNullException.ThrowIfNull(collider);
                DpePhysicsColliderV2 nativeCollider = default;
                nativeCollider.StructSize = (uint)sizeof(DpePhysicsColliderV2);
                nativeCollider.Shape = (uint)collider.Shape;
                nativeCollider.Sensor = collider.Sensor ? 1U : 0U;
                WriteVector(collider.Size, nativeCollider.Size);
                WriteVector(collider.Offset, nativeCollider.Offset);
                nativeCollider.Density = collider.Density;
                nativeCollider.Friction = collider.Friction;
                nativeCollider.Restitution = collider.Restitution;
                nativeCollider.Layer = collider.Layer;
                nativeCollider.Mask = collider.Mask;
                ArgumentNullException.ThrowIfNull(collider.Vertices);
                if (collider.Vertices.Count > 8)
                {
                    throw new ArgumentOutOfRangeException(nameof(collider.Vertices),
                        "Native 2D polygon colliders support at most eight convex vertices.");
                }
                nativeCollider.VertexCount = checked((uint)collider.Vertices.Count);
                for (var vertexIndex = 0; vertexIndex < collider.Vertices.Count; ++vertexIndex)
                {
                    nativeCollider.Vertices[vertexIndex * 2] = collider.Vertices[vertexIndex].X;
                    nativeCollider.Vertices[(vertexIndex * 2) + 1] = collider.Vertices[vertexIndex].Y;
                }
                nativeColliders[colliderIndex++] = nativeCollider;
            }
            nativeBodies[bodyIndex] = native;
        }

        fixed (DpePhysicsBodyV1* bodyPointer = nativeBodies)
        fixed (DpePhysicsColliderV2* colliderPointer = nativeColliders)
        {
            if (_api.RebuildV2 == 0)
            {
                if (bodies.SelectMany(static body => body.Colliders)
                    .Any(static collider => collider.Shape == NativePhysicsShape.Polygon2D))
                {
                    throw new NotSupportedException(
                        "The native runtime did not negotiate physics ABI minor 1 polygon colliders.");
                }
                RebuildLegacy(bodies);
                return;
            }
            var rebuild = (delegate* unmanaged[Cdecl]<ulong, DpePhysicsBodyV1*, nuint, DpePhysicsColliderV2*, nuint, DpeStatus>)_api.RebuildV2;
            ThrowIfFailed(rebuild(Value, bodyPointer, (nuint)nativeBodies.Length, colliderPointer,
                (nuint)nativeColliders.Length),
                "rebuild physics world");
        }
    }

    private unsafe void RebuildLegacy(IReadOnlyList<NativePhysicsBody> bodies)
    {
        var nativeBodies = new DpePhysicsBodyV1[bodies.Count];
        var nativeColliders = new List<DpePhysicsColliderV1>();
        for (var bodyIndex = 0; bodyIndex < bodies.Count; ++bodyIndex)
        {
            var body = bodies[bodyIndex];
            DpePhysicsBodyV1 native = default;
            native.StructSize = (uint)sizeof(DpePhysicsBodyV1);
            native.Dimension = (uint)body.Dimension;
            native.Mode = (uint)body.Mode;
            native.Flags = body.ContinuousCollision ? ContinuousCollision : 0;
            WriteId(body.EntityId, native.EntityUuid);
            WriteVector(body.Position, native.Position);
            WriteQuaternion(body.Rotation, native.Rotation);
            WriteVector(body.LinearVelocity, native.LinearVelocity);
            WriteVector(body.AngularVelocity, native.AngularVelocity);
            native.LinearDamping = body.LinearDamping;
            native.AngularDamping = body.AngularDamping;
            native.GravityScale = body.GravityScale;
            native.ColliderStart = checked((uint)nativeColliders.Count);
            native.ColliderCount = checked((uint)body.Colliders.Count);
            foreach (var collider in body.Colliders)
            {
                DpePhysicsColliderV1 nativeCollider = default;
                nativeCollider.StructSize = (uint)sizeof(DpePhysicsColliderV1);
                nativeCollider.Shape = (uint)collider.Shape;
                nativeCollider.Sensor = collider.Sensor ? 1U : 0U;
                WriteVector(collider.Size, nativeCollider.Size);
                WriteVector(collider.Offset, nativeCollider.Offset);
                nativeCollider.Density = collider.Density;
                nativeCollider.Friction = collider.Friction;
                nativeCollider.Restitution = collider.Restitution;
                nativeCollider.Layer = collider.Layer;
                nativeCollider.Mask = collider.Mask;
                nativeColliders.Add(nativeCollider);
            }
            nativeBodies[bodyIndex] = native;
        }
        var colliderArray = nativeColliders.ToArray();
        fixed (DpePhysicsBodyV1* bodyPointer = nativeBodies)
        fixed (DpePhysicsColliderV1* colliderPointer = colliderArray)
        {
            var rebuild = (delegate* unmanaged[Cdecl]<ulong, DpePhysicsBodyV1*, nuint,
                DpePhysicsColliderV1*, nuint, DpeStatus>)_api.Rebuild;
            ThrowIfFailed(rebuild(Value, bodyPointer, (nuint)nativeBodies.Length,
                colliderPointer, (nuint)colliderArray.Length), "rebuild legacy physics world");
        }
    }

    public unsafe void ApplyCommands(IReadOnlyList<PhysicsCommand> commands)
    {
        ArgumentNullException.ThrowIfNull(commands);
        var nativeCommands = new DpePhysicsCommandV1[commands.Count];
        for (var index = 0; index < commands.Count; ++index)
        {
            var command = commands[index];
            ArgumentNullException.ThrowIfNull(command);
            DpePhysicsCommandV1 native = default;
            native.StructSize = (uint)sizeof(DpePhysicsCommandV1);
            native.Kind = (uint)command.Kind;
            WriteId(command.EntityId, native.EntityUuid);
            WriteVector(command.Value, native.Value);
            WriteQuaternion(command.Rotation, native.Rotation);
            nativeCommands[index] = native;
        }
        fixed (DpePhysicsCommandV1* pointer = nativeCommands)
        {
            var apply = (delegate* unmanaged[Cdecl]<ulong, DpePhysicsCommandV1*, nuint, DpeStatus>)_api.ApplyCommands;
            ThrowIfFailed(apply(Value, pointer, (nuint)nativeCommands.Length), "apply physics commands");
        }
    }

    public unsafe NativePhysicsStepResult Step(double elapsedSeconds)
    {
        DpePhysicsStepResultV1 result = default;
        result.StructSize = (uint)sizeof(DpePhysicsStepResultV1);
        var step = (delegate* unmanaged[Cdecl]<ulong, double, DpePhysicsStepResultV1*, DpeStatus>)_api.Step;
        ThrowIfFailed(step(Value, elapsedSeconds, &result), "step physics world");
        return new NativePhysicsStepResult(
            result.Ticks,
            result.WorldTick,
            result.DroppedSeconds,
            (result.Flags & DroppedTime) != 0,
            checked((int)result.TransformCount),
            checked((int)result.PendingContactCount));
    }

    public unsafe IReadOnlyList<NativePhysicsTransform> CopyTransforms()
    {
        var copy = (delegate* unmanaged[Cdecl]<ulong, DpePhysicsTransformV1*, nuint, nuint*, DpeStatus>)_api.CopyTransforms;
        nuint required = 0;
        var status = copy(Value, null, 0, &required);
        if (status != DpeStatus.BufferTooSmall && status != DpeStatus.Ok)
        {
            ThrowIfFailed(status, "query physics transforms");
        }
        if (required == 0)
        {
            return [];
        }
        var native = new DpePhysicsTransformV1[checked((int)required)];
        fixed (DpePhysicsTransformV1* pointer = native)
        {
            ThrowIfFailed(copy(Value, pointer, (nuint)native.Length, &required), "copy physics transforms");
        }
        var result = new NativePhysicsTransform[checked((int)required)];
        for (var index = 0; index < result.Length; ++index)
        {
            var item = native[index];
            result[index] = new NativePhysicsTransform
            {
                EntityId = ReadId(item.EntityUuid),
                Position = ReadVector(item.Position),
                Rotation = ReadQuaternion(item.Rotation),
                LinearVelocity = ReadVector(item.LinearVelocity),
            };
        }
        return result;
    }

    public unsafe IReadOnlyList<PhysicsContact> DrainContacts()
    {
        var drain = (delegate* unmanaged[Cdecl]<ulong, DpePhysicsContactV1*, nuint, nuint*, DpeStatus>)_api.DrainContacts;
        nuint required = 0;
        var status = drain(Value, null, 0, &required);
        if (status != DpeStatus.BufferTooSmall && status != DpeStatus.Ok)
        {
            ThrowIfFailed(status, "query physics contacts");
        }
        if (required == 0)
        {
            return [];
        }
        var native = new DpePhysicsContactV1[checked((int)required)];
        fixed (DpePhysicsContactV1* pointer = native)
        {
            ThrowIfFailed(drain(Value, pointer, (nuint)native.Length, &required), "drain physics contacts");
        }
        var result = new PhysicsContact[checked((int)required)];
        for (var index = 0; index < result.Length; ++index)
        {
            var item = native[index];
            result[index] = new PhysicsContact
            {
                Tick = checked((long)item.Tick),
                Kind = (PhysicsContactKind)item.Kind,
                EntityA = ReadId(item.EntityAUuid),
                EntityB = ReadId(item.EntityBUuid),
                Point = ReadVector(item.Point),
                Normal = ReadVector(item.Normal),
            };
        }
        return result;
    }

    public PhysicsRaycastResult Raycast2D(PhysicsVector2 origin, PhysicsVector2 direction, double distance) =>
        Raycast(NativePhysicsDimension.TwoD, new PhysicsVector3(origin.X, origin.Y, 0), new PhysicsVector3(direction.X, direction.Y, 0), distance);

    public PhysicsRaycastResult Raycast3D(PhysicsVector3 origin, PhysicsVector3 direction, double distance) =>
        Raycast(NativePhysicsDimension.ThreeD, origin, direction, distance);

    protected override unsafe bool ReleaseHandle()
    {
        try
        {
            var destroy = (delegate* unmanaged[Cdecl]<ulong, DpeStatus>)_api.DestroyWorld;
            return destroy(Value) == DpeStatus.Ok;
        }
        finally
        {
            if (_runtimeReferenceAdded)
            {
                _runtime.DangerousRelease();
            }
        }
    }

    private ulong Value => unchecked((ulong)handle.ToInt64());

    private unsafe PhysicsRaycastResult Raycast(
        NativePhysicsDimension dimension,
        PhysicsVector3 origin,
        PhysicsVector3 direction,
        double distance)
    {
        DpePhysicsRaycastV1 ray = default;
        ray.StructSize = (uint)sizeof(DpePhysicsRaycastV1);
        ray.Dimension = (uint)dimension;
        WriteVector(origin, ray.Origin);
        WriteVector(direction, ray.Direction);
        ray.Distance = distance;
        DpePhysicsRaycastHitV1 hit = default;
        hit.StructSize = (uint)sizeof(DpePhysicsRaycastHitV1);
        var cast = (delegate* unmanaged[Cdecl]<ulong, DpePhysicsRaycastV1*, DpePhysicsRaycastHitV1*, DpeStatus>)_api.Raycast;
        ThrowIfFailed(cast(Value, &ray, &hit), "raycast physics world");
        return new PhysicsRaycastResult
        {
            Hit = hit.Hit != 0,
            EntityId = hit.Hit != 0 ? ReadId(hit.EntityUuid) : null,
            Fraction = hit.Fraction,
            Point = ReadVector(hit.Point),
            Normal = ReadVector(hit.Normal),
        };
    }

    private static void ThrowIfFailed(DpeStatus status, string operation)
    {
        if (status != DpeStatus.Ok)
        {
            throw new InvalidOperationException($"Failed to {operation}: {status}.");
        }
    }

    private static unsafe void WriteId(DpeId id, byte* destination)
    {
        Span<byte> bytes = stackalloc byte[16];
        if (!id.ToGuid().TryWriteBytes(bytes, bigEndian: true, out var written) || written != bytes.Length)
        {
            throw new InvalidOperationException("Could not encode physics entity ID.");
        }
        for (var index = 0; index < bytes.Length; ++index)
        {
            destination[index] = bytes[index];
        }
    }

    private static unsafe DpeId ReadId(byte* source) =>
        new(new Guid(new ReadOnlySpan<byte>(source, 16), bigEndian: true));

    private static unsafe void WriteVector(PhysicsVector3 value, double* destination)
    {
        destination[0] = value.X;
        destination[1] = value.Y;
        destination[2] = value.Z;
    }

    private static unsafe PhysicsVector3 ReadVector(double* source) => new(source[0], source[1], source[2]);

    private static unsafe void WriteQuaternion(PhysicsQuaternion value, double* destination)
    {
        destination[0] = value.X;
        destination[1] = value.Y;
        destination[2] = value.Z;
        destination[3] = value.W;
    }

    private static unsafe PhysicsQuaternion ReadQuaternion(double* source) =>
        new(source[0], source[1], source[2], source[3]);
}
