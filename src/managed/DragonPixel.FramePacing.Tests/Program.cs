using DragonPixel.Runtime;

namespace DragonPixel.FramePacing.Tests;

internal static class Program
{
    private static int Main()
    {
        try
        {
            VerifyOnTimeSchedule();
            VerifyOneMissedSlot();
            VerifyMultipleMissedSlots();
            VerifyOriginScheduleDoesNotDriftOrReanchor();
            VerifyCounters();
            VerifyWorkerDiagnostics();
            Console.WriteLine("Dragon Pixel frame pacing tests passed.");
            return 0;
        }
        catch (Exception exception)
        {
            Console.Error.WriteLine(exception);
            return 1;
        }
    }

    private static void VerifyOnTimeSchedule()
    {
        var clock = new FakeTickSource(frequency: 1_000, timestamp: 2_000);
        var pacer = new FramePacer(clock, targetFrameRate: 10);
        var workStartedAt = clock.GetTimestamp();
        clock.Timestamp = 2_025;

        var decision = pacer.CompleteIteration(workStartedAt, frameProduced: true);

        Assert(decision.DeadlineTicks == 2_100, "On-time work did not retain the first origin-derived deadline.");
        Assert(decision.DeadlineSlot == 1, "On-time work advanced to the wrong deadline slot.");
        Assert(!decision.Late && decision.DroppedFrames == 0 && !decision.Overrun,
            "On-time work reported a pacing fault.");
        Assert(pacer.Counters == FramePacingCounters.Empty, "On-time work changed pacing counters.");
    }

    private static void VerifyOneMissedSlot()
    {
        var clock = new FakeTickSource(frequency: 1_000, timestamp: 1_000);
        var pacer = new FramePacer(clock, targetFrameRate: 10);
        clock.Timestamp = 1_050;
        var workStartedAt = clock.GetTimestamp();
        clock.Timestamp = 1_101;

        var decision = pacer.CompleteIteration(workStartedAt, frameProduced: true);

        Assert(decision.Late, "A missed deadline was not reported as late.");
        Assert(decision.DroppedFrames == 1, "Exactly one missed frame slot was not counted.");
        Assert(decision.DeadlineTicks == 1_200 && decision.DeadlineSlot == 2,
            "A one-slot miss did not select the first future origin-derived deadline.");
        Assert(!decision.Overrun, "Scheduler lateness was incorrectly classified as a work overrun.");
    }

    private static void VerifyMultipleMissedSlots()
    {
        var clock = new FakeTickSource(frequency: 1_000, timestamp: 0);
        var pacer = new FramePacer(clock, targetFrameRate: 10);
        var workStartedAt = clock.GetTimestamp();
        clock.Timestamp = 350;

        var decision = pacer.CompleteIteration(workStartedAt, frameProduced: true);

        Assert(decision.Late, "A multi-slot miss was not reported as late.");
        Assert(decision.DroppedFrames == 3, "All missed frame slots were not counted.");
        Assert(decision.DeadlineTicks == 400 && decision.DeadlineSlot == 4,
            "A multi-slot miss did not skip directly to the first future deadline.");
        Assert(decision.Overrun, "Work exceeding a frame interval was not reported as an overrun.");
    }

    private static void VerifyOriginScheduleDoesNotDriftOrReanchor()
    {
        const long origin = 123;
        var clock = new FakeTickSource(frequency: 1_000, timestamp: origin);
        var pacer = new FramePacer(clock, targetFrameRate: 60);

        Assert(FramePacer.DeadlineForSlot(origin, 1_000, 60, 60) == origin + 1_000,
            "Fractional frame intervals accumulated truncation drift over one second.");

        var firstStart = clock.GetTimestamp();
        clock.Timestamp = origin + 40;
        var lateDecision = pacer.CompleteIteration(firstStart, frameProduced: true);
        Assert(lateDecision.DeadlineSlot == 3 && lateDecision.DeadlineTicks == origin + 50,
            "Late pacing did not remain on the original 60 Hz slot timeline.");

        clock.Timestamp = lateDecision.DeadlineTicks;
        var resumedStart = clock.GetTimestamp();
        clock.Timestamp = origin + 55;
        var resumedDecision = pacer.CompleteIteration(resumedStart, frameProduced: true);
        Assert(resumedDecision.DeadlineSlot == 4 && resumedDecision.DeadlineTicks == origin + 66,
            "The schedule re-anchored to completion time after a missed deadline.");
    }

    private static void VerifyCounters()
    {
        var clock = new FakeTickSource(frequency: 1_000, timestamp: 0);
        var pacer = new FramePacer(clock, targetFrameRate: 10);

        clock.Timestamp = 50;
        var firstStart = clock.GetTimestamp();
        clock.Timestamp = 101;
        pacer.CompleteIteration(firstStart, frameProduced: true);

        clock.Timestamp = 200;
        var secondStart = clock.GetTimestamp();
        clock.Timestamp = 301;
        pacer.CompleteIteration(secondStart, frameProduced: true);

        clock.Timestamp = 350;
        var idleStart = clock.GetTimestamp();
        clock.Timestamp = 701;
        pacer.CompleteIteration(idleStart, frameProduced: false);

        Assert(pacer.Counters == new FramePacingCounters(2, 2, 1),
            "Late, dropped, and overrun counters did not reflect produced frames exactly.");
    }

    private static void VerifyWorkerDiagnostics()
    {
        var diagnostics = WorkerHost.CreateFramePacingDiagnostics(new FramePacingCounters(3, 5, 2));
        Assert(diagnostics["strategy"]!.GetValue<string>() == "origin-derived-skip-missed",
            "Worker diagnostics did not identify the absolute pacing strategy.");
        Assert(diagnostics["targetFramesPerSecond"]!.GetValue<int>() == 64,
            "Worker diagnostics did not expose the target frame rate.");
        Assert(diagnostics["clockFrequency"]!.GetValue<long>() > 0,
            "Worker diagnostics did not expose the monotonic clock frequency.");
        Assert(diagnostics["lateFrames"]!.GetValue<long>() == 3
            && diagnostics["droppedFrames"]!.GetValue<long>() == 5
            && diagnostics["overrunFrames"]!.GetValue<long>() == 2,
            "Worker diagnostics did not expose the pacing counters unchanged.");
    }

    private static void Assert(bool condition, string message)
    {
        if (!condition)
        {
            throw new InvalidOperationException(message);
        }
    }

    private sealed class FakeTickSource(long frequency, long timestamp) : IMonotonicTickSource
    {
        public long Frequency { get; } = frequency;

        public long Timestamp { get; set; } = timestamp;

        public long GetTimestamp() => Timestamp;
    }
}
