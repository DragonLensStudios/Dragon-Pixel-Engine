namespace DragonPixel.PocC;

[DpeComponent(PocComponentIds.ManagedRotator, "DragonPixel.PocC.RotatorComponent", "Rotator", 1, "managed")]
internal sealed class RotatorComponent
{
    [DpeProperty("dpe.poc.rotator.speed", "Degrees per second", "number", Order = 0)]
    public double Speed { get; set; }

    [DpeProperty("dpe.poc.rotator.target", "Target entity", "entity-reference", Order = 1)]
    public Guid Target { get; set; }
}
