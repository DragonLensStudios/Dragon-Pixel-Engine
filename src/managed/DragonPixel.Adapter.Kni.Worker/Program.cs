using DragonPixel.Runtime;
using DragonPixel.Runtime.FrameworkWorker;

if (args.Contains("--graphics-probe", StringComparer.Ordinal))
{
    return GraphicsConformanceProbe.Execute("KNI");
}
// KNI reports factory registration during device startup. Keep that text on stderr so
// stdout remains an unambiguous length-prefixed control channel.
Console.SetOut(Console.Error);
return await WorkerHost.RunAsync(new FrameworkSceneAdapter(), args);
