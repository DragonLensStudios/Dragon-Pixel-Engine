using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Reflection;

namespace DragonPixel.PocA;

internal enum DpeStatus : int
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
internal enum DpeCapabilities : ulong
{
    StructuredErrors = 1UL << 0,
    TrackedAllocator = 1UL << 1,
    LiveCounts = 1UL << 2,
    NativeCounterComponent = 1UL << 3,
    ManagedCallback = 1UL << 4,
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct DpeUtf8View
{
    internal byte* Data;
    internal nuint Length;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct DpeErrorInfo
{
    internal int Code;
    internal uint Subsystem;
    internal fixed byte CorrelationId[16];
}

[StructLayout(LayoutKind.Sequential)]
internal struct DpeLiveCounts
{
    internal ulong Runtimes;
    internal ulong Worlds;
    internal ulong Entities;
    internal ulong Components;
    internal ulong Allocations;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct DpeApiV1
{
    internal uint StructSize;
    internal uint AbiMajor;
    internal uint AbiMinor;
    internal uint Reserved;
    internal ulong Capabilities;

    internal delegate* unmanaged[Cdecl]<ulong*, int> CreateRuntime;
    internal delegate* unmanaged[Cdecl]<ulong, int> DestroyRuntime;
    internal delegate* unmanaged[Cdecl]<ulong, ulong*, int> CreateWorld;
    internal delegate* unmanaged[Cdecl]<ulong, int> DestroyWorld;
    internal delegate* unmanaged[Cdecl]<ulong, byte*, DpeUtf8View, ulong*, int> CreateEntity;
    internal delegate* unmanaged[Cdecl]<ulong, int> DestroyEntity;
    internal delegate* unmanaged[Cdecl]<ulong, byte*, nuint, nuint*, int> GetEntityName;
    internal delegate* unmanaged[Cdecl]<ulong, long, ulong*, int> AttachCounterComponent;
    internal delegate* unmanaged[Cdecl]<ulong, int> DestroyComponent;
    internal delegate* unmanaged[Cdecl]<ulong, long*, int> GetCounterValue;
    internal delegate* unmanaged[Cdecl]<ulong, long, int> SetCounterValue;
    internal delegate* unmanaged[Cdecl]<nuint, void**, int> Allocate;
    internal delegate* unmanaged[Cdecl]<void*, int> Deallocate;
    internal delegate* unmanaged[Cdecl]<DpeLiveCounts*, int> GetLiveCounts;
    internal delegate* unmanaged[Cdecl]<DpeErrorInfo*, byte*, nuint, nuint*, int> GetLastError;
    internal delegate* unmanaged[Cdecl]<int> ForceNativeException;
    internal delegate* unmanaged[Cdecl]<delegate* unmanaged[Cdecl]<void*, int*, int>, void*, int*, int> InvokeManagedCallback;
}

internal static partial class NativeEntryPoint
{
    private const string LibraryName = "dragonpixel_poc_a";
    private static string? s_nativeLibraryPath;

    static NativeEntryPoint()
    {
        NativeLibrary.SetDllImportResolver(typeof(NativeEntryPoint).Assembly, ResolveLibrary);
    }

    internal static void Configure(string nativeLibraryPath)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(nativeLibraryPath);
        s_nativeLibraryPath = Path.GetFullPath(nativeLibraryPath);
    }

    private static nint ResolveLibrary(string libraryName, Assembly assembly, DllImportSearchPath? searchPath)
    {
        _ = assembly;
        _ = searchPath;
        if (!string.Equals(libraryName, LibraryName, StringComparison.Ordinal))
        {
            return 0;
        }
        if (s_nativeLibraryPath is null)
        {
            throw new InvalidOperationException("The POC A native library path was not configured.");
        }
        return NativeLibrary.Load(s_nativeLibraryPath);
    }

    [LibraryImport(LibraryName, EntryPoint = "dpe_get_api_v1")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static unsafe partial int GetApi(uint requestedMajor, uint requestedMinor, DpeApiV1* outApi, nuint outApiSize);
}
