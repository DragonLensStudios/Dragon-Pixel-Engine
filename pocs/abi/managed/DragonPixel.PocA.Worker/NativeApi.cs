using System.Runtime.InteropServices;
using System.Text;

namespace DragonPixel.PocA;

internal sealed unsafe class NativeApi
{
    internal const uint AbiMajor = 1;
    internal const uint AbiMinor = 0;

    private readonly DpeApiV1 _api;

    private NativeApi(DpeApiV1 api)
    {
        _api = api;
    }

    internal uint Major => _api.AbiMajor;
    internal uint Minor => _api.AbiMinor;
    internal DpeCapabilities Capabilities => (DpeCapabilities)_api.Capabilities;

    internal static NativeApi Load(string nativeLibraryPath)
    {
        NativeEntryPoint.Configure(nativeLibraryPath);
        DpeApiV1 table = default;
        var status = (DpeStatus)NativeEntryPoint.GetApi(AbiMajor, AbiMinor, &table, (nuint)sizeof(DpeApiV1));
        if (status != DpeStatus.Ok)
        {
            throw new InvalidOperationException($"dpe_get_api_v1 failed with {status}.");
        }
        if (table.StructSize != sizeof(DpeApiV1) || table.AbiMajor != AbiMajor || table.AbiMinor < AbiMinor)
        {
            throw new InvalidOperationException("The native API table has an incompatible size or version.");
        }
        return new NativeApi(table);
    }

    internal static DpeStatus QueryVersion(uint major, uint minor)
    {
        DpeApiV1 table = default;
        return (DpeStatus)NativeEntryPoint.GetApi(major, minor, &table, (nuint)sizeof(DpeApiV1));
    }

    internal RuntimeHandle CreateRuntime()
    {
        ulong value = 0;
        ThrowIfFailed((DpeStatus)_api.CreateRuntime(&value), "create runtime");
        return new RuntimeHandle(this, value);
    }

    internal WorldHandle CreateWorld(RuntimeHandle runtime)
    {
        ulong value = 0;
        ThrowIfFailed((DpeStatus)_api.CreateWorld(runtime.Value, &value), "create world");
        return new WorldHandle(this, runtime, value);
    }

    internal EntityHandle CreateEntity(WorldHandle world, ReadOnlySpan<byte> uuid, string name)
    {
        if (uuid.Length != 16)
        {
            throw new ArgumentException("UUID must contain exactly 16 bytes.", nameof(uuid));
        }
        var nameBytes = Encoding.UTF8.GetBytes(name);
        ulong value = 0;
        fixed (byte* uuidPointer = uuid)
        fixed (byte* namePointer = nameBytes)
        {
            var view = new DpeUtf8View { Data = namePointer, Length = (nuint)nameBytes.Length };
            ThrowIfFailed((DpeStatus)_api.CreateEntity(world.Value, uuidPointer, view, &value), "create entity");
        }
        return new EntityHandle(this, world, value);
    }

    internal DpeStatus TryCreateEntityRaw(WorldHandle world, ReadOnlySpan<byte> uuid, ReadOnlySpan<byte> name)
    {
        if (uuid.Length != 16)
        {
            throw new ArgumentException("UUID must contain exactly 16 bytes.", nameof(uuid));
        }
        ulong value = 0;
        fixed (byte* uuidPointer = uuid)
        fixed (byte* namePointer = name)
        {
            var view = new DpeUtf8View { Data = namePointer, Length = (nuint)name.Length };
            return (DpeStatus)_api.CreateEntity(world.Value, uuidPointer, view, &value);
        }
    }

    internal CounterComponentHandle AttachCounter(EntityHandle entity, long initialValue)
    {
        ulong value = 0;
        ThrowIfFailed((DpeStatus)_api.AttachCounterComponent(entity.Value, initialValue, &value), "attach counter component");
        return new CounterComponentHandle(this, entity, value);
    }

    internal string GetEntityName(EntityHandle entity)
    {
        nuint required = 0;
        var status = (DpeStatus)_api.GetEntityName(entity.Value, null, 0, &required);
        if (status != DpeStatus.BufferTooSmall && status != DpeStatus.Ok)
        {
            ThrowIfFailed(status, "query entity name size");
        }
        if (required == 0)
        {
            return string.Empty;
        }
        var bytes = new byte[checked((int)required)];
        fixed (byte* pointer = bytes)
        {
            ThrowIfFailed((DpeStatus)_api.GetEntityName(entity.Value, pointer, (nuint)bytes.Length, &required), "copy entity name");
        }
        return Encoding.UTF8.GetString(bytes);
    }

    internal long GetCounter(CounterComponentHandle component)
    {
        long value = 0;
        ThrowIfFailed((DpeStatus)_api.GetCounterValue(component.Value, &value), "read counter component");
        return value;
    }

    internal void SetCounter(CounterComponentHandle component, long value) =>
        ThrowIfFailed((DpeStatus)_api.SetCounterValue(component.Value, value), "write counter component");

    internal nint Allocate(nuint size)
    {
        void* memory = null;
        ThrowIfFailed((DpeStatus)_api.Allocate(size, &memory), "allocate native memory");
        return (nint)memory;
    }

    internal DpeStatus TryDeallocate(nint memory) => (DpeStatus)_api.Deallocate((void*)memory);

    internal DpeLiveCounts GetLiveCounts()
    {
        DpeLiveCounts counts = default;
        ThrowIfFailed((DpeStatus)_api.GetLiveCounts(&counts), "query live counts");
        return counts;
    }

    internal DpeStatus ForceNativeException() => (DpeStatus)_api.ForceNativeException();

    internal DpeStatus InvokeManagedCallback(delegate* unmanaged[Cdecl]<void*, int*, int> callback, int* outValue) =>
        (DpeStatus)_api.InvokeManagedCallback(callback, null, outValue);

    internal NativeError GetLastError()
    {
        DpeErrorInfo info = default;
        nuint required = 0;
        var status = (DpeStatus)_api.GetLastError(&info, null, 0, &required);
        if (status != DpeStatus.BufferTooSmall && status != DpeStatus.Ok)
        {
            return new NativeError((DpeStatus)info.Code, info.Subsystem, "Unable to retrieve native error text.");
        }
        var bytes = new byte[checked((int)required)];
        if (bytes.Length > 0)
        {
            fixed (byte* pointer = bytes)
            {
                status = (DpeStatus)_api.GetLastError(&info, pointer, (nuint)bytes.Length, &required);
            }
            if (status != DpeStatus.Ok)
            {
                return new NativeError((DpeStatus)info.Code, info.Subsystem, "Unable to copy native error text.");
            }
        }
        return new NativeError((DpeStatus)info.Code, info.Subsystem, Encoding.UTF8.GetString(bytes));
    }

    internal bool DestroyRuntime(ulong handle) => (DpeStatus)_api.DestroyRuntime(handle) == DpeStatus.Ok;
    internal bool DestroyWorld(ulong handle) => (DpeStatus)_api.DestroyWorld(handle) == DpeStatus.Ok;
    internal bool DestroyEntity(ulong handle) => (DpeStatus)_api.DestroyEntity(handle) == DpeStatus.Ok;
    internal bool DestroyComponent(ulong handle) => (DpeStatus)_api.DestroyComponent(handle) == DpeStatus.Ok;

    private void ThrowIfFailed(DpeStatus status, string operation)
    {
        if (status == DpeStatus.Ok)
        {
            return;
        }
        var error = GetLastError();
        throw new InvalidOperationException($"Failed to {operation}: {status} ({error.Subsystem}) {error.Message}");
    }
}

internal readonly record struct NativeError(DpeStatus Status, uint Subsystem, string Message);

internal abstract class DpeSafeHandle : SafeHandle
{
    private readonly SafeHandle? _parent;
    private readonly bool _parentReferenceAdded;

    protected DpeSafeHandle(ulong value, SafeHandle? parent = null)
        : base(nint.Zero, ownsHandle: true)
    {
        if (value == 0)
        {
            throw new ArgumentOutOfRangeException(nameof(value));
        }
        if (parent is not null)
        {
            var added = false;
            parent.DangerousAddRef(ref added);
            _parent = parent;
            _parentReferenceAdded = added;
        }
        SetHandle((nint)value);
    }

    public override bool IsInvalid => handle == nint.Zero;
    internal ulong Value => (ulong)(nuint)DangerousGetHandle();

    protected void ReleaseParent()
    {
        if (_parentReferenceAdded)
        {
            _parent!.DangerousRelease();
        }
    }
}

internal sealed class RuntimeHandle(NativeApi api, ulong value) : DpeSafeHandle(value)
{
    protected override bool ReleaseHandle() => api.DestroyRuntime(Value);
}

internal sealed class WorldHandle(NativeApi api, RuntimeHandle runtime, ulong value) : DpeSafeHandle(value, runtime)
{
    protected override bool ReleaseHandle()
    {
        try
        {
            return api.DestroyWorld(Value);
        }
        finally
        {
            ReleaseParent();
        }
    }
}

internal sealed class EntityHandle(NativeApi api, WorldHandle world, ulong value) : DpeSafeHandle(value, world)
{
    protected override bool ReleaseHandle()
    {
        try
        {
            return api.DestroyEntity(Value);
        }
        finally
        {
            ReleaseParent();
        }
    }
}

internal sealed class CounterComponentHandle(NativeApi api, EntityHandle entity, ulong value) : DpeSafeHandle(value, entity)
{
    protected override bool ReleaseHandle()
    {
        try
        {
            return api.DestroyComponent(Value);
        }
        finally
        {
            ReleaseParent();
        }
    }
}
