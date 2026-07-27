using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace DragonPixel.NativeInterop;

public sealed class NativeApiSession
{
    private const uint AbiMajor = 1;
    private const uint AbiMinor = 1;
    private static readonly object ResolverLock = new();
    private static string? _libraryPath;
    private static nint _libraryHandle;
    private readonly DpeApiV1 _api;

    private NativeApiSession(DpeApiV1 api)
    {
        _api = api;
    }

    public uint NegotiatedMajor => _api.AbiMajor;
    public uint NegotiatedMinor => _api.AbiMinor;
    public DpeCapabilities Capabilities => (DpeCapabilities)_api.Capabilities;

    public static NativeApiSession Open(string nativeLibraryPath)
    {
        ConfigureResolver(nativeLibraryPath);
        var status = NativeMethods.GetApiV1(AbiMajor, AbiMinor, out var api, (nuint)Marshal.SizeOf<DpeApiV1>());
        if (status != DpeStatus.Ok)
        {
            throw new InvalidOperationException($"dpe_get_api_v1 failed with {status}.");
        }
        if (api.StructSize != Marshal.SizeOf<DpeApiV1>() || api.AbiMajor != AbiMajor || api.AbiMinor < AbiMinor)
        {
            throw new InvalidOperationException("Native API returned an incompatible function table.");
        }
        return new NativeApiSession(api);
    }

    public unsafe NativeRuntimeHandle CreateRuntime()
    {
        ulong runtime = 0;
        var create = (delegate* unmanaged[Cdecl]<ulong*, DpeStatus>)_api.CreateRuntime;
        var status = create(&runtime);
        if (status != DpeStatus.Ok || runtime == 0)
        {
            throw new InvalidOperationException($"Native runtime creation failed with {status}.");
        }
        return new NativeRuntimeHandle(this, runtime, _api.DestroyRuntime);
    }

    internal unsafe NativePhysicsWorldHandle CreatePhysicsWorld(
        NativeRuntimeHandle runtime,
        DragonPixel.Contracts.ScenePhysicsSettings settings)
    {
        ArgumentNullException.ThrowIfNull(runtime);
        ArgumentNullException.ThrowIfNull(settings);
        if (!Capabilities.HasFlag(DpeCapabilities.PhysicsV1) || _api.AcquirePhysicsApi == 0)
        {
            throw new NotSupportedException("The native runtime did not negotiate the physics v1 capability.");
        }

        var runtimeReference = false;
        runtime.DangerousAddRef(ref runtimeReference);
        try
        {
            var acquire = (delegate* unmanaged[Cdecl]<ulong, uint, uint, DpePhysicsApiV1*, nuint, DpeStatus>)_api.AcquirePhysicsApi;
            DpePhysicsApiV1 physicsApi = default;
            var status = acquire(runtime.Value, 1, 0, &physicsApi, (nuint)sizeof(DpePhysicsApiV1));
            ThrowIfFailed(status, "acquire physics API");
            if (physicsApi.StructSize < sizeof(DpePhysicsApiV1) || physicsApi.AbiMajor != 1)
            {
                throw new InvalidOperationException("Native physics API returned an incompatible function table.");
            }

            DpePhysicsWorldSettingsV1 nativeSettings = default;
            nativeSettings.StructSize = (uint)sizeof(DpePhysicsWorldSettingsV1);
            nativeSettings.MaximumCatchUpTicks = checked((uint)settings.MaximumCatchUpTicks);
            nativeSettings.Box2DSolverSubsteps = checked((uint)settings.Box2DSolverSubsteps);
            nativeSettings.JoltCollisionSteps = checked((uint)settings.JoltCollisionSteps);
            nativeSettings.FixedTimeStepSeconds = settings.FixedTimeStepSeconds;
            nativeSettings.Gravity2D[0] = settings.Gravity2D.X;
            nativeSettings.Gravity2D[1] = settings.Gravity2D.Y;
            nativeSettings.Gravity3D[0] = settings.Gravity3D.X;
            nativeSettings.Gravity3D[1] = settings.Gravity3D.Y;
            nativeSettings.Gravity3D[2] = settings.Gravity3D.Z;

            ulong world = 0;
            var create = (delegate* unmanaged[Cdecl]<ulong, DpePhysicsWorldSettingsV1*, ulong*, DpeStatus>)physicsApi.CreateWorld;
            status = create(runtime.Value, &nativeSettings, &world);
            ThrowIfFailed(status, "create physics world");
            try
            {
                return new NativePhysicsWorldHandle(physicsApi, runtime, world);
            }
            catch
            {
                var destroy = (delegate* unmanaged[Cdecl]<ulong, DpeStatus>)physicsApi.DestroyWorld;
                _ = destroy(world);
                throw;
            }
        }
        finally
        {
            if (runtimeReference)
            {
                runtime.DangerousRelease();
            }
        }
    }

    private static void ThrowIfFailed(DpeStatus status, string operation)
    {
        if (status != DpeStatus.Ok)
        {
            throw new InvalidOperationException($"Failed to {operation}: {status}.");
        }
    }

    private static void ConfigureResolver(string nativeLibraryPath)
    {
        nativeLibraryPath = Path.GetFullPath(nativeLibraryPath);
        lock (ResolverLock)
        {
            if (_libraryPath is not null)
            {
                if (!string.Equals(_libraryPath, nativeLibraryPath, StringComparison.OrdinalIgnoreCase))
                {
                    throw new InvalidOperationException("The native library resolver is already bound to another path.");
                }
                return;
            }
            _libraryPath = nativeLibraryPath;
            NativeLibrary.SetDllImportResolver(typeof(NativeApiSession).Assembly, ResolveLibrary);
        }
    }

    private static nint ResolveLibrary(string libraryName, Assembly assembly, DllImportSearchPath? searchPath)
    {
        _ = assembly;
        _ = searchPath;
        if (!string.Equals(libraryName, "dragonpixel", StringComparison.Ordinal))
        {
            return 0;
        }
        lock (ResolverLock)
        {
            if (_libraryHandle == 0)
            {
                _libraryHandle = NativeLibrary.Load(_libraryPath
                    ?? throw new InvalidOperationException("Native library path was not configured."));
            }
            return _libraryHandle;
        }
    }
}

public sealed class NativeRuntimeHandle : SafeHandle
{
    private readonly NativeApiSession _session;
    private readonly nint _destroy;

    internal NativeRuntimeHandle(NativeApiSession session, ulong value, nint destroy) : base(IntPtr.Zero, ownsHandle: true)
    {
        _session = session;
        _destroy = destroy;
        SetHandle(unchecked((nint)(long)value));
    }

    public override bool IsInvalid => handle == IntPtr.Zero;
    internal ulong Value => unchecked((ulong)handle.ToInt64());

    public NativePhysicsWorldHandle CreatePhysicsWorld(DragonPixel.Contracts.ScenePhysicsSettings? settings = null) =>
        _session.CreatePhysicsWorld(this, settings ?? new DragonPixel.Contracts.ScenePhysicsSettings());

    protected override unsafe bool ReleaseHandle()
    {
        var destroy = (delegate* unmanaged[Cdecl]<ulong, DpeStatus>)_destroy;
        return destroy(Value) == DpeStatus.Ok;
    }
}

internal static partial class NativeMethods
{
    [LibraryImport("dragonpixel", EntryPoint = "dpe_get_api_v1")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial DpeStatus GetApiV1(
        uint requestedMajor,
        uint requestedMinor,
        out DpeApiV1 api,
        nuint apiSize);
}
