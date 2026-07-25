using DragonPixel.Runtime;
using DragonPixel.Runtime.FrameworkWorker;

if (args.Contains("--graphics-probe", StringComparer.Ordinal))
{
    return GraphicsConformanceProbe.Execute("MonoGame");
}
// Framework startup diagnostics must never corrupt the length-prefixed stdout protocol.
// WorkerHost writes to Console.OpenStandardOutput directly; ordinary Console output is diagnostic.
Console.SetOut(Console.Error);
return await WorkerHost.RunAsync(new FrameworkSceneAdapter(), args);
