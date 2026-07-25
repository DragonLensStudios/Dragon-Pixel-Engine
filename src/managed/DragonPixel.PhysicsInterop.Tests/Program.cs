using DragonPixel.Contracts;
using DragonPixel.NativeInterop;

namespace DragonPixel.PhysicsInterop.Tests;

internal static class Program
{
    private static int Main(string[] args)
    {
        try
        {
            if (args.Length != 1)
            {
                throw new ArgumentException("Usage: physics managed tests <native library>");
            }

            var session = NativeApiSession.Open(Path.GetFullPath(args[0]));
            Assert(session.NegotiatedMinor >= 1, "Native ABI minor 1 was not negotiated.");
            Assert(session.Capabilities.HasFlag(DpeCapabilities.PhysicsV1), "Physics capability was not negotiated.");

            using var runtime = session.CreateRuntime();
            var settings = new ScenePhysicsSettings
            {
                MaximumCatchUpTicks = 4,
                Box2DSolverSubsteps = 4,
                JoltCollisionSteps = 1,
            };
            var world = runtime.CreatePhysicsWorld(settings);
            var ground2D = Id("71000000-0000-4000-8000-000000000001");
            var falling2D = Id("71000000-0000-4000-8000-000000000002");
            var ground3D = Id("71000000-0000-4000-8000-000000000003");
            var falling3D = Id("71000000-0000-4000-8000-000000000004");
            world.Rebuild([
                Body(ground2D, NativePhysicsDimension.TwoD, PhysicsBodyMode.Static, new PhysicsVector3(0, -1, 3),
                    new NativePhysicsCollider { Size = new PhysicsVector3(20, 1, 1) }),
                Body(falling2D, NativePhysicsDimension.TwoD, PhysicsBodyMode.Dynamic, new PhysicsVector3(0, 4, 7),
                    new NativePhysicsCollider { Shape = NativePhysicsShape.CircleOrSphere, Size = new PhysicsVector3(0.5, 0.5, 0.5) },
                    continuous: true),
                Body(ground3D, NativePhysicsDimension.ThreeD, PhysicsBodyMode.Static, new PhysicsVector3(0, -1, 0),
                    new NativePhysicsCollider { Size = new PhysicsVector3(20, 1, 20) }),
                Body(falling3D, NativePhysicsDimension.ThreeD, PhysicsBodyMode.Dynamic, new PhysicsVector3(0, 4, 0),
                    new NativePhysicsCollider { Shape = NativePhysicsShape.CircleOrSphere, Size = new PhysicsVector3(0.5, 0.5, 0.5) }),
            ]);

            NativePhysicsStepResult step = default;
            for (var index = 0; index < 240; ++index)
            {
                step = world.Step(1.0 / 60.0);
                Assert(step.Ticks == 1, "Managed fixed step did not execute exactly once.");
            }
            Assert(step.WorldTick == 240, "Managed world tick did not advance.");

            var transforms = world.CopyTransforms();
            var transform2D = transforms.Single(value => value.EntityId == falling2D);
            var transform3D = transforms.Single(value => value.EntityId == falling3D);
            Assert(transform2D.Position.Y is > -0.1 and < 0.2, "Managed Box2D body did not rest.");
            Assert(Math.Abs(transform2D.Position.Z - 7) < 0.0001, "Managed 2D mapping lost authoring Z.");
            Assert(transform3D.Position.Y is > -0.1 and < 0.2, "Managed Jolt body did not rest in Y-up space.");
            Assert(world.Raycast2D(new PhysicsVector2(0, 8), new PhysicsVector2(0, -1), 20).Hit,
                "Managed 2D raycast missed.");
            var hit3D = world.Raycast3D(new PhysicsVector3(0, 8, 0), new PhysicsVector3(0, -1, 0), 20);
            Assert(hit3D.Hit && Math.Abs(hit3D.Normal.Y) > 0.5, "Managed 3D raycast missed or lost its normal.");
            Assert(world.DrainContacts().Count > 0, "Managed contact drain was empty.");

            world.ApplyCommands([
                new PhysicsCommand
                {
                    Kind = PhysicsCommandKind.Teleport,
                    EntityId = falling2D,
                    Value = new PhysicsVector3(0, 5, 9),
                    Rotation = new PhysicsQuaternion(0, 0, 0, 1),
                },
            ]);
            world.Step(0);
            transform2D = world.CopyTransforms().Single(value => value.EntityId == falling2D);
            Assert(Math.Abs(transform2D.Position.Y - 5) < 0.001 && Math.Abs(transform2D.Position.Z - 9) < 0.001,
                "Managed teleport command did not update both mapped axes.");

            var catchUp = world.Step(1);
            Assert(catchUp.Ticks == 4 && catchUp.DroppedTime && catchUp.DroppedSeconds > 0.8,
                "Managed catch-up diagnostics were not surfaced.");

            world.Dispose();
            var staleRejected = false;
            try
            {
                world.Step(0);
            }
            catch (InvalidOperationException)
            {
                staleRejected = true;
            }
            Assert(staleRejected, "Disposed managed physics handle remained callable.");

            Console.WriteLine("Dragon Pixel managed physics interop POC G passed.");
            return 0;
        }
        catch (Exception exception)
        {
            Console.Error.WriteLine(exception);
            return 1;
        }
    }

    private static NativePhysicsBody Body(
        DpeId id,
        NativePhysicsDimension dimension,
        PhysicsBodyMode mode,
        PhysicsVector3 position,
        NativePhysicsCollider collider,
        bool continuous = false) =>
        new()
        {
            EntityId = id,
            Dimension = dimension,
            Mode = mode,
            Position = position,
            ContinuousCollision = continuous,
            Colliders = [collider],
        };

    private static DpeId Id(string value)
    {
        if (!DpeId.TryParse(value, out var id))
        {
            throw new InvalidOperationException($"Invalid test ID: {value}");
        }
        return id;
    }

    private static void Assert(bool condition, string message)
    {
        if (!condition)
        {
            throw new InvalidOperationException(message);
        }
    }
}
