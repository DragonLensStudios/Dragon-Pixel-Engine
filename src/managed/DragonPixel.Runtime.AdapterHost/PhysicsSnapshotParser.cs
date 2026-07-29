using System.Numerics;
using System.Text.Json;
using DragonPixel.Contracts;
using DragonPixel.NativeInterop;

namespace DragonPixel.Runtime;

internal sealed record ParsedPhysicsSnapshot(
    ScenePhysicsSettings Settings,
    IReadOnlyList<NativePhysicsBody> Bodies,
    IReadOnlyList<string> Diagnostics);

internal static class PhysicsSnapshotParser
{
    public static ParsedPhysicsSnapshot Parse(
        JsonElement root,
        RenderScene scene,
        int formatVersion)
    {
        var settings = ReadSettings(root, formatVersion);
        var diagnostics = new List<string>();
        var bodies = new List<NativePhysicsBody>();
        var worldTransforms = ResolveWorldTransforms(scene);

        foreach (var entityElement in root.GetProperty("entities").EnumerateArray())
        {
            if (!ReadBool(entityElement, "enabled", true))
            {
                continue;
            }

            var entityIdText = RequiredString(entityElement, "id");
            if (!DpeId.TryParse(entityIdText, out var entityId))
            {
                throw new InvalidDataException($"Invalid physics entity id {entityIdText}.");
            }

            var components = ReadEnabledPhysicsComponents(entityElement);
            var colliders2D = ReadColliders2D(components);
            var colliders3D = ReadColliders3D(components);
            var body2D = components.GetValueOrDefault(BuiltinComponentIds.RigidBody2D);
            var body3D = components.GetValueOrDefault(BuiltinComponentIds.RigidBody3D);
            var has2D = body2D.HasValue || colliders2D.Count != 0;
            var has3D = body3D.HasValue || colliders3D.Count != 0;

            if (has2D && has3D)
            {
                diagnostics.Add(
                    $"Entity {entityIdText} mixes 2D and 3D physics components; its physics components were not loaded.");
                continue;
            }
            if (!has2D && !has3D)
            {
                continue;
            }

            var colliders = has2D ? colliders2D : colliders3D;
            if (colliders.Count == 0)
            {
                diagnostics.Add(
                    $"Entity {entityIdText} has a rigid body without an enabled collider; its rigid body was not loaded.");
                continue;
            }

            var bodyProperties = has2D ? body2D : body3D;
            var transform = worldTransforms[entityIdText];
            colliders = ScaleColliders(colliders, transform.Scale, has2D);
            var mode = bodyProperties.HasValue
                ? ReadBodyMode(bodyProperties.Value, has2D ? "dpe.physics2d.body_mode" : "dpe.physics3d.body_mode")
                : PhysicsBodyMode.Static;
            var velocity = bodyProperties.HasValue
                ? (has2D
                    ? ReadVector2As3(bodyProperties.Value, "dpe.physics2d.initial_velocity", PhysicsVector3Zero)
                    : ReadVector3(bodyProperties.Value, "dpe.physics3d.initial_velocity", PhysicsVector3Zero))
                : PhysicsVector3Zero;

            bodies.Add(new NativePhysicsBody
            {
                EntityId = entityId,
                Dimension = has2D ? NativePhysicsDimension.TwoD : NativePhysicsDimension.ThreeD,
                Mode = mode,
                Position = new PhysicsVector3(
                    transform.Position.X,
                    transform.Position.Y,
                    transform.Position.Z),
                Rotation = new PhysicsQuaternion(
                    transform.Rotation.X,
                    transform.Rotation.Y,
                    transform.Rotation.Z,
                    transform.Rotation.W),
                LinearVelocity = velocity,
                AngularVelocity = PhysicsVector3Zero,
                LinearDamping = bodyProperties.HasValue
                    ? ReadNonNegative(
                        bodyProperties.Value,
                        has2D ? "dpe.physics2d.linear_damping" : "dpe.physics3d.linear_damping",
                        has2D ? 0 : 0.05)
                    : 0,
                AngularDamping = bodyProperties.HasValue
                    ? ReadNonNegative(
                        bodyProperties.Value,
                        has2D ? "dpe.physics2d.angular_damping" : "dpe.physics3d.angular_damping",
                        has2D ? 0 : 0.05)
                    : 0,
                GravityScale = bodyProperties.HasValue
                    ? ReadFinite(
                        bodyProperties.Value,
                        has2D ? "dpe.physics2d.gravity_scale" : "dpe.physics3d.gravity_scale",
                        1)
                    : 1,
                ContinuousCollision = bodyProperties.HasValue
                    && ReadBool(
                        bodyProperties.Value,
                        has2D ? "dpe.physics2d.ccd" : "dpe.physics3d.ccd",
                        false),
                Colliders = colliders,
            });
        }

        return new ParsedPhysicsSnapshot(settings, bodies, diagnostics);
    }

    private static IReadOnlyDictionary<string, JsonElement?> ReadEnabledPhysicsComponents(
        JsonElement entity)
    {
        var result = new Dictionary<string, JsonElement?>(StringComparer.OrdinalIgnoreCase);
        if (!entity.TryGetProperty("components", out var components)
            || components.ValueKind != JsonValueKind.Array)
        {
            return result;
        }

        foreach (var component in components.EnumerateArray())
        {
            if (!ReadBool(component, "enabled", true))
            {
                continue;
            }
            var typeId = component.TryGetProperty("typeId", out var typeValue)
                && typeValue.ValueKind == JsonValueKind.String
                    ? typeValue.GetString() ?? string.Empty
                    : string.Empty;
            if (!IsPhysicsType(typeId))
            {
                continue;
            }
            if (result.ContainsKey(typeId))
            {
                throw new InvalidDataException(
                    $"Entity {RequiredString(entity, "id")} contains duplicate physics component {typeId}.");
            }
            result[typeId] = ReadProperties(component);
        }
        return result;
    }

    private static bool IsPhysicsType(string typeId) =>
        typeId.Equals(BuiltinComponentIds.RigidBody2D, StringComparison.OrdinalIgnoreCase)
        || typeId.Equals(BuiltinComponentIds.BoxCollider2D, StringComparison.OrdinalIgnoreCase)
        || typeId.Equals(BuiltinComponentIds.CircleCollider2D, StringComparison.OrdinalIgnoreCase)
        || typeId.Equals(BuiltinComponentIds.PolygonCollider2D, StringComparison.OrdinalIgnoreCase)
        || typeId.Equals(BuiltinComponentIds.RigidBody3D, StringComparison.OrdinalIgnoreCase)
        || typeId.Equals(BuiltinComponentIds.BoxCollider3D, StringComparison.OrdinalIgnoreCase)
        || typeId.Equals(BuiltinComponentIds.SphereCollider3D, StringComparison.OrdinalIgnoreCase);

    private static IReadOnlyList<NativePhysicsCollider> ReadColliders2D(
        IReadOnlyDictionary<string, JsonElement?> components)
    {
        var result = new List<NativePhysicsCollider>();
        if (components.GetValueOrDefault(BuiltinComponentIds.BoxCollider2D) is JsonElement box)
        {
            var size = ReadVector2As3(box, "dpe.physics2d.size", new PhysicsVector3(1, 1, 1));
            result.Add(ReadCollider(
                box,
                NativePhysicsShape.Box,
                new PhysicsVector3(size.X, size.Y, 1),
                ReadVector2As3(box, "dpe.physics2d.offset", PhysicsVector3Zero)));
        }
        if (components.GetValueOrDefault(BuiltinComponentIds.CircleCollider2D) is JsonElement circle)
        {
            var radius = ReadPositive(circle, "dpe.physics2d.radius", 0.5);
            result.Add(ReadCollider(
                circle,
                NativePhysicsShape.CircleOrSphere,
                new PhysicsVector3(radius, radius, radius),
                ReadVector2As3(circle, "dpe.physics2d.offset", PhysicsVector3Zero)));
        }
        if (components.GetValueOrDefault(BuiltinComponentIds.PolygonCollider2D) is JsonElement polygon)
        {
            var collider = ReadCollider(
                polygon,
                NativePhysicsShape.Polygon2D,
                new PhysicsVector3(1, 1, 1),
                ReadVector2As3(polygon, "dpe.physics2d.offset", PhysicsVector3Zero));
            collider.Vertices = ReadPolygon(polygon, "dpe.physics2d.points");
            result.Add(collider);
        }
        return result;
    }

    private static IReadOnlyList<NativePhysicsCollider> ReadColliders3D(
        IReadOnlyDictionary<string, JsonElement?> components)
    {
        var result = new List<NativePhysicsCollider>();
        if (components.GetValueOrDefault(BuiltinComponentIds.BoxCollider3D) is JsonElement box)
        {
            result.Add(ReadCollider(
                box,
                NativePhysicsShape.Box,
                ReadPositiveVector3(box, "dpe.physics3d.size", new PhysicsVector3(1, 1, 1)),
                ReadVector3(box, "dpe.physics3d.offset", PhysicsVector3Zero)));
        }
        if (components.GetValueOrDefault(BuiltinComponentIds.SphereCollider3D) is JsonElement sphere)
        {
            var radius = ReadPositive(sphere, "dpe.physics3d.radius", 0.5);
            result.Add(ReadCollider(
                sphere,
                NativePhysicsShape.CircleOrSphere,
                new PhysicsVector3(radius, radius, radius),
                ReadVector3(sphere, "dpe.physics3d.offset", PhysicsVector3Zero)));
        }
        return result;
    }

    private static NativePhysicsCollider ReadCollider(
        JsonElement properties,
        NativePhysicsShape shape,
        PhysicsVector3 size,
        PhysicsVector3 offset) => new()
    {
        Shape = shape,
        Size = size,
        Offset = offset,
        Sensor = ReadBool(properties, "dpe.physics.sensor", false),
        Density = ReadPositive(properties, "dpe.physics.density", 1),
        Friction = ReadNonNegative(properties, "dpe.physics.friction", 0.5),
        Restitution = ReadNonNegative(properties, "dpe.physics.restitution", 0),
        Layer = ReadUShort(properties, "dpe.physics.layer", 0, 15),
        Mask = ReadUShort(properties, "dpe.physics.mask", ushort.MaxValue, ushort.MaxValue),
    };

    private static IReadOnlyList<PhysicsVector2> ReadPolygon(JsonElement properties, string name)
    {
        if (!properties.TryGetProperty(name, out var value)
            || value.ValueKind != JsonValueKind.Array
            || value.GetArrayLength() is < 3 or > 8)
        {
            throw new InvalidDataException(
                $"Physics property {name} must contain three to eight convex vector2 points.");
        }
        var result = new List<PhysicsVector2>(value.GetArrayLength());
        foreach (var point in value.EnumerateArray())
        {
            if (point.ValueKind != JsonValueKind.Object)
            {
                throw new InvalidDataException($"Physics property {name} contains a non-vector point.");
            }
            result.Add(new PhysicsVector2(
                RequiredFinite(point, "x"), RequiredFinite(point, "y")));
        }
        return result;
    }

    private static IReadOnlyList<NativePhysicsCollider> ScaleColliders(
        IReadOnlyList<NativePhysicsCollider> colliders,
        RenderVector3 scale,
        bool twoDimensional)
    {
        foreach (var collider in colliders)
        {
            collider.Offset = new PhysicsVector3(
                collider.Offset.X * scale.X,
                collider.Offset.Y * scale.Y,
                collider.Offset.Z * scale.Z);
            if (collider.Shape == NativePhysicsShape.CircleOrSphere)
            {
                var amount = Math.Max(Math.Abs(scale.X), Math.Abs(scale.Y));
                if (!twoDimensional)
                    amount = Math.Max(amount, Math.Abs(scale.Z));
                collider.Size = new PhysicsVector3(
                    collider.Size.X * amount,
                    collider.Size.Y * amount,
                    collider.Size.Z * amount);
            }
            else
            {
                collider.Size = new PhysicsVector3(
                    collider.Size.X * Math.Abs(scale.X),
                    collider.Size.Y * Math.Abs(scale.Y),
                    collider.Size.Z * Math.Abs(scale.Z));
            }
            if (collider.Shape == NativePhysicsShape.Polygon2D)
            {
                collider.Vertices = collider.Vertices.Select(point => new PhysicsVector2(
                    point.X * scale.X, point.Y * scale.Y)).ToArray();
            }
        }
        return colliders;
    }

    private static ScenePhysicsSettings ReadSettings(JsonElement root, int formatVersion)
    {
        if (formatVersion < 3)
        {
            return new ScenePhysicsSettings();
        }
        if (!root.TryGetProperty("physicsSettings", out var value)
            || value.ValueKind != JsonValueKind.Object)
        {
            throw new InvalidDataException("Scene v3 requires physicsSettings.");
        }

        var fixedStep = RequiredFinite(value, "fixedTimeStepSeconds");
        var maximumCatchUpTicks = RequiredPositiveInt(value, "maxCatchUpTicks");
        var box2DSubsteps = RequiredPositiveInt(value, "box2DSolverSubsteps");
        var joltSteps = RequiredPositiveInt(value, "joltCollisionSteps");
        if (fixedStep <= 0)
        {
            throw new InvalidDataException("fixedTimeStepSeconds must be positive.");
        }
        return new ScenePhysicsSettings
        {
            FixedTimeStepSeconds = fixedStep,
            MaximumCatchUpTicks = maximumCatchUpTicks,
            Box2DSolverSubsteps = box2DSubsteps,
            JoltCollisionSteps = joltSteps,
            Gravity2D = ReadRequiredVector2(value, "gravity2D"),
            Gravity3D = ReadRequiredVector3(value, "gravity3D"),
        };
    }

    private static IReadOnlyDictionary<string, RenderTransform> ResolveWorldTransforms(RenderScene scene)
    {
        var entities = scene.Entities.ToDictionary(static entity => entity.Id, StringComparer.Ordinal);
        var matrices = new Dictionary<string, Matrix4x4>(StringComparer.Ordinal);
        var result = new Dictionary<string, RenderTransform>(StringComparer.Ordinal);
        var resolving = new HashSet<string>(StringComparer.Ordinal);

        Matrix4x4 Resolve(RenderEntity entity)
        {
            if (matrices.TryGetValue(entity.Id, out var cached))
            {
                return cached;
            }
            if (!resolving.Add(entity.Id))
            {
                throw new InvalidDataException($"Entity hierarchy contains a cycle at {entity.Id}.");
            }

            var local = ToMatrix(entity.Transform);
            if (entity.ParentId is not null && entities.TryGetValue(entity.ParentId, out var parent))
            {
                local *= Resolve(parent);
            }
            resolving.Remove(entity.Id);
            matrices[entity.Id] = local;
            return local;
        }

        foreach (var entity in scene.Entities)
        {
            var matrix = Resolve(entity);
            if (!Matrix4x4.Decompose(matrix, out var scale, out var rotation, out var translation))
            {
                throw new InvalidDataException($"Entity {entity.Id} has a non-decomposable world transform.");
            }
            rotation = Quaternion.Normalize(rotation);
            result[entity.Id] = new RenderTransform(
                new RenderVector3(translation.X, translation.Y, translation.Z),
                new RenderQuaternion(rotation.X, rotation.Y, rotation.Z, rotation.W),
                new RenderVector3(scale.X, scale.Y, scale.Z));
        }
        return result;
    }

    internal static Matrix4x4 ToMatrix(RenderTransform transform) =>
        Matrix4x4.CreateScale(transform.Scale.X, transform.Scale.Y, transform.Scale.Z)
        * Matrix4x4.CreateFromQuaternion(new Quaternion(
            transform.Rotation.X,
            transform.Rotation.Y,
            transform.Rotation.Z,
            transform.Rotation.W))
        * Matrix4x4.CreateTranslation(
            transform.Position.X,
            transform.Position.Y,
            transform.Position.Z);

    private static JsonElement ReadProperties(JsonElement component)
    {
        if (component.TryGetProperty("properties", out var properties)
            && properties.ValueKind == JsonValueKind.Object)
        {
            return properties;
        }
        if (component.TryGetProperty("propertyPayloadJson", out var payload)
            && payload.ValueKind == JsonValueKind.String)
        {
            try
            {
                using var document = JsonDocument.Parse(payload.GetString() ?? "{}");
                if (document.RootElement.ValueKind != JsonValueKind.Object)
                {
                    throw new InvalidDataException("Physics component payload must be a JSON object.");
                }
                return document.RootElement.Clone();
            }
            catch (JsonException exception)
            {
                throw new InvalidDataException("Physics component payload was invalid JSON.", exception);
            }
        }
        using var empty = JsonDocument.Parse("{}");
        return empty.RootElement.Clone();
    }

    private static PhysicsBodyMode ReadBodyMode(JsonElement properties, string name)
    {
        var value = ReadString(properties, name, "dynamic");
        return value.ToLowerInvariant() switch
        {
            "static" => PhysicsBodyMode.Static,
            "kinematic" => PhysicsBodyMode.Kinematic,
            "dynamic" => PhysicsBodyMode.Dynamic,
            _ => throw new InvalidDataException($"Physics body mode {value} is invalid."),
        };
    }

    private static PhysicsVector3 ReadPositiveVector3(
        JsonElement properties,
        string name,
        PhysicsVector3 fallback)
    {
        var result = ReadVector3(properties, name, fallback);
        if (result.X <= 0 || result.Y <= 0 || result.Z <= 0)
        {
            throw new InvalidDataException($"Physics vector {name} must contain positive values.");
        }
        return result;
    }

    private static PhysicsVector3 ReadVector2As3(
        JsonElement properties,
        string name,
        PhysicsVector3 fallback)
    {
        if (!properties.TryGetProperty(name, out var value))
        {
            return fallback;
        }
        if (value.ValueKind != JsonValueKind.Object)
        {
            throw new InvalidDataException($"Physics property {name} must be a vector2 object.");
        }
        return new PhysicsVector3(
            ReadFinite(value, "x", fallback.X),
            ReadFinite(value, "y", fallback.Y),
            fallback.Z);
    }

    private static PhysicsVector3 ReadVector3(
        JsonElement properties,
        string name,
        PhysicsVector3 fallback)
    {
        if (!properties.TryGetProperty(name, out var value))
        {
            return fallback;
        }
        if (value.ValueKind != JsonValueKind.Object)
        {
            throw new InvalidDataException($"Physics property {name} must be a vector3 object.");
        }
        return new PhysicsVector3(
            ReadFinite(value, "x", fallback.X),
            ReadFinite(value, "y", fallback.Y),
            ReadFinite(value, "z", fallback.Z));
    }

    private static PhysicsVector2 ReadRequiredVector2(JsonElement properties, string name)
    {
        if (!properties.TryGetProperty(name, out var value) || value.ValueKind != JsonValueKind.Object)
        {
            throw new InvalidDataException($"Physics settings require vector2 property {name}.");
        }
        return new PhysicsVector2(RequiredFinite(value, "x"), RequiredFinite(value, "y"));
    }

    private static PhysicsVector3 ReadRequiredVector3(JsonElement properties, string name)
    {
        if (!properties.TryGetProperty(name, out var value) || value.ValueKind != JsonValueKind.Object)
        {
            throw new InvalidDataException($"Physics settings require vector3 property {name}.");
        }
        return new PhysicsVector3(
            RequiredFinite(value, "x"),
            RequiredFinite(value, "y"),
            RequiredFinite(value, "z"));
    }

    private static double ReadPositive(JsonElement properties, string name, double fallback)
    {
        var value = ReadFinite(properties, name, fallback);
        if (value <= 0)
        {
            throw new InvalidDataException($"Physics property {name} must be positive.");
        }
        return value;
    }

    private static double ReadNonNegative(JsonElement properties, string name, double fallback)
    {
        var value = ReadFinite(properties, name, fallback);
        if (value < 0)
        {
            throw new InvalidDataException($"Physics property {name} cannot be negative.");
        }
        return value;
    }

    private static ushort ReadUShort(
        JsonElement properties,
        string name,
        ushort fallback,
        ushort maximum)
    {
        if (!properties.TryGetProperty(name, out var value))
        {
            return fallback;
        }
        if (!value.TryGetInt32(out var parsed) || parsed < 0 || parsed > maximum)
        {
            throw new InvalidDataException($"Physics property {name} must be between 0 and {maximum}.");
        }
        return checked((ushort)parsed);
    }

    private static double ReadFinite(JsonElement properties, string name, double fallback)
    {
        if (!properties.TryGetProperty(name, out var value))
        {
            return fallback;
        }
        if (!value.TryGetDouble(out var result) || !double.IsFinite(result))
        {
            throw new InvalidDataException($"Physics property {name} must be a finite number.");
        }
        return result;
    }

    private static double RequiredFinite(JsonElement properties, string name)
    {
        if (!properties.TryGetProperty(name, out var value)
            || !value.TryGetDouble(out var result)
            || !double.IsFinite(result))
        {
            throw new InvalidDataException($"Physics settings require finite number {name}.");
        }
        return result;
    }

    private static int RequiredPositiveInt(JsonElement properties, string name)
    {
        if (!properties.TryGetProperty(name, out var value)
            || !value.TryGetInt32(out var result)
            || result <= 0)
        {
            throw new InvalidDataException($"Physics settings require positive integer {name}.");
        }
        return result;
    }

    private static bool ReadBool(JsonElement properties, string name, bool fallback) =>
        properties.TryGetProperty(name, out var value)
        && value.ValueKind is JsonValueKind.True or JsonValueKind.False
            ? value.GetBoolean()
            : fallback;

    private static string ReadString(JsonElement properties, string name, string fallback) =>
        properties.TryGetProperty(name, out var value) && value.ValueKind == JsonValueKind.String
            ? value.GetString() ?? fallback
            : fallback;

    private static string RequiredString(JsonElement properties, string name) =>
        properties.TryGetProperty(name, out var value) && value.ValueKind == JsonValueKind.String
            ? value.GetString()!
            : throw new InvalidDataException($"Snapshot requires string property {name}.");

    private static PhysicsVector3 PhysicsVector3Zero => new(0, 0, 0);
}
