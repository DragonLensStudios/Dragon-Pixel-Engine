using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using DragonPixel.PocA;

if (args.Length != 1 || !File.Exists(args[0]))
{
    Console.Error.WriteLine("Usage: DragonPixel.PocA.Worker <absolute-native-library-path>");
    return 2;
}

try
{
    Run(args[0]);
    Console.WriteLine("POC A managed/native ABI tests passed (10,000 ownership cycles).");
    return 0;
}
catch (Exception exception)
{
    Console.Error.WriteLine(exception);
    return 1;
}

static unsafe void Run(string nativeLibraryPath)
{
    var api = NativeApi.Load(nativeLibraryPath);
    Require(api.Major == NativeApi.AbiMajor && api.Minor == NativeApi.AbiMinor, "ABI version mismatch");
    Require(NativeApi.QueryVersion(2, 0) == DpeStatus.AbiVersionUnsupported, "unsupported ABI major did not fail");

    const DpeCapabilities requiredCapabilities =
        DpeCapabilities.StructuredErrors |
        DpeCapabilities.TrackedAllocator |
        DpeCapabilities.LiveCounts |
        DpeCapabilities.NativeCounterComponent |
        DpeCapabilities.ManagedCallback;
    Require((api.Capabilities & requiredCapabilities) == requiredCapabilities, "required ABI capabilities are missing");

    Span<byte> loopUuid = stackalloc byte[16];
    for (var index = 0; index < 10_000; ++index)
    {
        using var runtime = api.CreateRuntime();
        using var world = api.CreateWorld(runtime);
        Guid.NewGuid().TryWriteBytes(loopUuid);
        var expectedName = $"entity-{index}-dragon";
        using var entity = api.CreateEntity(world, loopUuid, expectedName);
        using var component = api.AttachCounter(entity, index);
        Require(api.GetEntityName(entity) == expectedName, "UTF-8 entity name round trip failed");
        Require(api.GetCounter(component) == index, "native component initial value mismatch");
        api.SetCounter(component, index + 1L);
        Require(api.GetCounter(component) == index + 1L, "native component updated value mismatch");
    }

    var counts = api.GetLiveCounts();
    Require(counts.Runtimes == 0 && counts.Worlds == 0 && counts.Entities == 0 && counts.Components == 0, "SafeHandle ownership leaked native state");

    using (var runtime = api.CreateRuntime())
    using (var world = api.CreateWorld(runtime))
    {
        Span<byte> uuid = stackalloc byte[16];
        Guid.NewGuid().TryWriteBytes(uuid);
        ReadOnlySpan<byte> invalidUtf8 = [0xC3, 0x28];
        Require(api.TryCreateEntityRaw(world, uuid, invalidUtf8) == DpeStatus.InvalidUtf8, "invalid UTF-8 crossed the boundary");
    }

    var allocation = api.Allocate(128);
    Require(api.TryDeallocate(allocation) == DpeStatus.Ok, "paired native deallocation failed");
    Require(api.TryDeallocate(allocation) == DpeStatus.InvalidArgument, "double free was not rejected");

    Require(api.ForceNativeException() == DpeStatus.InternalError, "native exception escaped instead of becoming status data");
    var nativeError = api.GetLastError();
    Require(nativeError.Status == DpeStatus.InternalError && nativeError.Message.Contains("forced failure", StringComparison.Ordinal), "native structured error was not preserved");

    var callbackValue = 0;
    Require(api.InvokeManagedCallback(&SuccessfulManagedCallback, &callbackValue) == DpeStatus.Ok && callbackValue == 73, "managed callback success path failed");
    Require(api.InvokeManagedCallback(&ContainedThrowingManagedCallback, &callbackValue) == DpeStatus.CallbackFailure, "managed exception was not contained by its trampoline");

    counts = api.GetLiveCounts();
    Require(counts.Runtimes == 0 && counts.Worlds == 0 && counts.Entities == 0 && counts.Components == 0 && counts.Allocations == 0, "final native live counts are nonzero");
}

[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
static unsafe int SuccessfulManagedCallback(void* context, int* outValue)
{
    _ = context;
    *outValue = 73;
    return (int)DpeStatus.Ok;
}

[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
static unsafe int ContainedThrowingManagedCallback(void* context, int* outValue)
{
    _ = context;
    _ = outValue;
    try
    {
        throw new InvalidOperationException("forced managed callback failure");
    }
    catch
    {
        return (int)DpeStatus.CallbackFailure;
    }
}

static void Require(bool condition, string message)
{
    if (!condition)
    {
        throw new InvalidOperationException(message);
    }
}
