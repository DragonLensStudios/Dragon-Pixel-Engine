using DragonPixel.Runtime;
using DragonPixel.Runtime.FrameworkWorker;

if (args.Contains("--graphics-probe", StringComparer.Ordinal))
{
    return GraphicsConformanceProbe.Execute("MonoGame");
}
return await WorkerHost.RunAsync(new FrameworkSceneAdapter(), args);
