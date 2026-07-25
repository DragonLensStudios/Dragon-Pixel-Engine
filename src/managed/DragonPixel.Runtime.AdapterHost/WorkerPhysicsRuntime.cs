using System.Numerics;
using DragonPixel.Contracts;
using DragonPixel.NativeInterop;

namespace DragonPixel.Runtime;

internal sealed class WorkerPhysicsRuntime : IDisposable
{
    private readonly NativeRuntimeHandle _runtime;
    private bool _disposed;

    public WorkerPhysicsRuntime(string nativeLibraryPath)
    {
        var session = NativeApiSession.Open(nativeLibraryPath);
        if (!session.Capabilities.HasFlag(DpeCapabilities.PhysicsV1))
        {
            throw new NotSupportedException("The native engine did not negotiate the physics v1 capability.");
        }
        _runtime = session.CreateRuntime();
    }

    public WorkerPhysicsWorld CreateWorld(ParsedPhysicsSnapshot snapshot)
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        var handle = _runtime.CreatePhysicsWorld(snapshot.Settings);
        try
        {
            handle.Rebuild(snapshot.Bodies);
            return new WorkerPhysicsWorld(handle, snapshot.Bodies);
        }
        catch
        {
            handle.Dispose();
            throw;
        }
    }

    public void Dispose()
    {
        if (_disposed)
        {
            return;
        }
        _disposed = true;
        _runtime.Dispose();
    }
}

internal sealed class WorkerPhysicsWorld : IDisposable
{
    private readonly NativePhysicsWorldHandle _handle;
    private readonly HashSet<string> _bodyIds;
    private IReadOnlyList<NativePhysicsTransform> _transforms;
    private bool _disposed;

    public WorkerPhysicsWorld(
        NativePhysicsWorldHandle handle,
        IReadOnlyList<NativePhysicsBody> bodies)
    {
        _handle = handle;
        _bodyIds = bodies.Select(static body => body.EntityId.ToString())
            .ToHashSet(StringComparer.Ordinal);
        _transforms = handle.CopyTransforms();
    }

    public int BodyCount => _bodyIds.Count;
    public ulong WorldTick { get; private set; }
    public ulong TotalTicks { get; private set; }
    public double DroppedSeconds { get; private set; }
    public long ContactCount { get; private set; }
    public int LastContactCount { get; private set; }
    public IReadOnlyList<NativePhysicsTransform> Transforms => _transforms;

    public NativePhysicsStepResult Advance(TimeSpan elapsed)
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        var elapsedSeconds = Math.Max(0, elapsed.TotalSeconds);
        var result = _handle.Step(elapsedSeconds);
        _transforms = _handle.CopyTransforms();
        var contacts = _handle.DrainContacts();
        WorldTick = result.WorldTick;
        TotalTicks += result.Ticks;
        DroppedSeconds += result.DroppedSeconds;
        LastContactCount = contacts.Count;
        ContactCount += contacts.Count;
        return result;
    }

    public RenderScene ApplyTransforms(RenderScene authoringScene) =>
        PhysicsRenderOverlay.Apply(authoringScene, _transforms, _bodyIds);

    public void Dispose()
    {
        if (_disposed)
        {
            return;
        }
        _disposed = true;
        _handle.Dispose();
    }
}

internal static class PhysicsRenderOverlay
{
    public static RenderScene Apply(
        RenderScene authoringScene,
        IReadOnlyList<NativePhysicsTransform> transforms,
        IReadOnlySet<string> bodyIds)
    {
        if (transforms.Count == 0)
        {
            return authoringScene;
        }

        var physics = transforms.ToDictionary(
            static transform => transform.EntityId.ToString(),
            StringComparer.Ordinal);
        var entities = authoringScene.Entities.ToDictionary(static entity => entity.Id, StringComparer.Ordinal);
        var authoringWorld = new Dictionary<string, Matrix4x4>(StringComparer.Ordinal);
        var targetWorld = new Dictionary<string, Matrix4x4>(StringComparer.Ordinal);
        var resolvingAuthoring = new HashSet<string>(StringComparer.Ordinal);
        var resolvingTarget = new HashSet<string>(StringComparer.Ordinal);

        Matrix4x4 ResolveAuthoringWorld(RenderEntity entity)
        {
            if (authoringWorld.TryGetValue(entity.Id, out var cached))
            {
                return cached;
            }
            if (!resolvingAuthoring.Add(entity.Id))
            {
                return Matrix4x4.Identity;
            }
            var result = PhysicsSnapshotParser.ToMatrix(entity.Transform);
            if (entity.ParentId is not null && entities.TryGetValue(entity.ParentId, out var parent))
            {
                result *= ResolveAuthoringWorld(parent);
            }
            resolvingAuthoring.Remove(entity.Id);
            authoringWorld[entity.Id] = result;
            return result;
        }

        Matrix4x4 ResolveTargetWorld(RenderEntity entity)
        {
            if (targetWorld.TryGetValue(entity.Id, out var cached))
            {
                return cached;
            }
            if (!resolvingTarget.Add(entity.Id))
            {
                return Matrix4x4.Identity;
            }

            Matrix4x4 result;
            if (physics.TryGetValue(entity.Id, out var runtimeTransform))
            {
                var baseWorld = ResolveAuthoringWorld(entity);
                if (!Matrix4x4.Decompose(baseWorld, out var worldScale, out _, out _))
                {
                    worldScale = Vector3.One;
                }
                var rotation = Quaternion.Normalize(new Quaternion(
                    (float)runtimeTransform.Rotation.X,
                    (float)runtimeTransform.Rotation.Y,
                    (float)runtimeTransform.Rotation.Z,
                    (float)runtimeTransform.Rotation.W));
                result = Matrix4x4.CreateScale(worldScale)
                    * Matrix4x4.CreateFromQuaternion(rotation)
                    * Matrix4x4.CreateTranslation(
                        (float)runtimeTransform.Position.X,
                        (float)runtimeTransform.Position.Y,
                        (float)runtimeTransform.Position.Z);
            }
            else
            {
                result = PhysicsSnapshotParser.ToMatrix(entity.Transform);
                if (entity.ParentId is not null && entities.TryGetValue(entity.ParentId, out var parent))
                {
                    result *= ResolveTargetWorld(parent);
                }
            }

            resolvingTarget.Remove(entity.Id);
            targetWorld[entity.Id] = result;
            return result;
        }

        foreach (var entity in authoringScene.Entities)
        {
            _ = ResolveTargetWorld(entity);
        }

        var overlaid = authoringScene.Entities.Select(entity =>
        {
            if (!bodyIds.Contains(entity.Id))
            {
                return entity;
            }

            var local = targetWorld[entity.Id];
            if (entity.ParentId is not null
                && entities.TryGetValue(entity.ParentId, out var parent)
                && Matrix4x4.Invert(targetWorld[parent.Id], out var inverseParent))
            {
                local *= inverseParent;
            }
            if (!Matrix4x4.Decompose(local, out _, out var rotation, out var translation))
            {
                return entity;
            }
            rotation = Quaternion.Normalize(rotation);
            return entity with
            {
                Transform = new RenderTransform(
                    new RenderVector3(translation.X, translation.Y, translation.Z),
                    new RenderQuaternion(rotation.X, rotation.Y, rotation.Z, rotation.W),
                    entity.Transform.Scale),
            };
        }).ToArray();

        return authoringScene with { Entities = overlaid };
    }
}
