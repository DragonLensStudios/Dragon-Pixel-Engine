using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace DragonPixel.NativeInterop;

public sealed class NativeApiSession
{
    private const uint AbiMajor = 1;
    private const uint AbiMinor = 0;
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
        return new NativeRuntimeHandle(runtime, _api.DestroyRuntime);
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
    private readonly nint _destroy;

    internal NativeRuntimeHandle(ulong value, nint destroy) : base(IntPtr.Zero, ownsHandle: true)
    {
        _destroy = destroy;
        SetHandle(unchecked((nint)(long)value));
    }

    public override bool IsInvalid => handle == IntPtr.Zero;

    protected override unsafe bool ReleaseHandle()
    {
        var destroy = (delegate* unmanaged[Cdecl]<ulong, DpeStatus>)_destroy;
        var value = unchecked((ulong)handle.ToInt64());
        return destroy(value) == DpeStatus.Ok;
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
