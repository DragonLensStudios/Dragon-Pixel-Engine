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
}
