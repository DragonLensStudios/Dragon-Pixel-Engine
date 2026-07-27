using System.Diagnostics;

namespace DragonPixel.Runtime;

internal interface IMonotonicTickSource
{
    long Frequency { get; }

    long GetTimestamp();
}

internal sealed class StopwatchTickSource : IMonotonicTickSource
{
    public static StopwatchTickSource Instance { get; } = new();

    private StopwatchTickSource()
    {
    }

    public long Frequency => Stopwatch.Frequency;

    public long GetTimestamp() => Stopwatch.GetTimestamp();
}

internal readonly record struct FramePacingDecision(
    long DeadlineTicks,
    long DeadlineSlot,
    long DroppedFrames,
    bool Late,
    bool Overrun);

internal sealed record FramePacingCounters(
    long LateFrames,
    long DroppedFrames,
    long OverrunFrames)
{
    public static FramePacingCounters Empty { get; } = new(0, 0, 0);
}

internal sealed class FramePacer
{
    private readonly IMonotonicTickSource _tickSource;
    private readonly int _targetFrameRate;
    private long _nextDeadlineSlot = 1;
    private long _lateFrames;
    private long _droppedFrames;
    private long _overrunFrames;

    public FramePacer(IMonotonicTickSource tickSource, int targetFrameRate)
    {
        ArgumentNullException.ThrowIfNull(tickSource);
        if (tickSource.Frequency <= 0)
        {
            throw new ArgumentOutOfRangeException(nameof(tickSource), "The monotonic clock frequency must be positive.");
        }
        if (targetFrameRate <= 0 || targetFrameRate > tickSource.Frequency)
        {
            throw new ArgumentOutOfRangeException(
                nameof(targetFrameRate),
                "The target frame rate must be positive and no greater than the clock frequency.");
        }

        _tickSource = tickSource;
        _targetFrameRate = targetFrameRate;
        OriginTicks = tickSource.GetTimestamp();
    }

    public long OriginTicks { get; }

    public long Frequency => _tickSource.Frequency;

    public int TargetFrameRate => _targetFrameRate;

    public long GetTimestamp() => _tickSource.GetTimestamp();

    public FramePacingCounters Counters => new(_lateFrames, _droppedFrames, _overrunFrames);

    public FramePacingDecision CompleteIteration(long workStartedAtTicks, bool frameProduced)
    {
        var completedAtTicks = _tickSource.GetTimestamp();
        var decision = DecideNextDeadline(
            OriginTicks,
            _tickSource.Frequency,
            _targetFrameRate,
            _nextDeadlineSlot,
            workStartedAtTicks,
            completedAtTicks);
        _nextDeadlineSlot = checked(decision.DeadlineSlot + 1);

        if (frameProduced)
        {
            if (decision.Late)
            {
                _lateFrames = checked(_lateFrames + 1);
            }
            _droppedFrames = checked(_droppedFrames + decision.DroppedFrames);
            if (decision.Overrun)
            {
                _overrunFrames = checked(_overrunFrames + 1);
            }
        }

        return decision;
    }

    internal static FramePacingDecision DecideNextDeadline(
        long originTicks,
        long frequency,
        int targetFrameRate,
        long nextDeadlineSlot,
        long workStartedAtTicks,
        long completedAtTicks)
    {
        if (frequency <= 0)
        {
            throw new ArgumentOutOfRangeException(nameof(frequency));
        }
        if (targetFrameRate <= 0 || targetFrameRate > frequency)
        {
            throw new ArgumentOutOfRangeException(nameof(targetFrameRate));
        }
        if (nextDeadlineSlot <= 0)
        {
            throw new ArgumentOutOfRangeException(nameof(nextDeadlineSlot));
        }
        if (workStartedAtTicks < originTicks || completedAtTicks < workStartedAtTicks)
        {
            throw new ArgumentOutOfRangeException(
                nameof(completedAtTicks),
                "Pacing timestamps must be monotonic and no earlier than the pacing origin.");
        }

        var expectedDeadline = DeadlineForSlot(originTicks, frequency, targetFrameRate, nextDeadlineSlot);
        var late = completedAtTicks > expectedDeadline;
        var deadlineSlot = late
            ? FirstDeadlineSlotAfter(originTicks, frequency, targetFrameRate, completedAtTicks)
            : nextDeadlineSlot;
        var droppedFrames = checked(deadlineSlot - nextDeadlineSlot);
        var workTicks = completedAtTicks - workStartedAtTicks;
        var overrun = workTicks > frequency / targetFrameRate;

        return new FramePacingDecision(
            DeadlineForSlot(originTicks, frequency, targetFrameRate, deadlineSlot),
            deadlineSlot,
            droppedFrames,
            late,
            overrun);
    }

    internal static long DeadlineForSlot(
        long originTicks,
        long frequency,
        int targetFrameRate,
        long deadlineSlot)
    {
        if (frequency <= 0)
        {
            throw new ArgumentOutOfRangeException(nameof(frequency));
        }
        if (targetFrameRate <= 0 || targetFrameRate > frequency)
        {
            throw new ArgumentOutOfRangeException(nameof(targetFrameRate));
        }
        if (deadlineSlot < 0)
        {
            throw new ArgumentOutOfRangeException(nameof(deadlineSlot));
        }

        var completeGroups = deadlineSlot / targetFrameRate;
        var remainingSlots = deadlineSlot % targetFrameRate;
        var completeTicks = checked(completeGroups * frequency);
        var remainingTicks = checked(remainingSlots * frequency) / targetFrameRate;
        return checked(originTicks + completeTicks + remainingTicks);
    }

    private static long FirstDeadlineSlotAfter(
        long originTicks,
        long frequency,
        int targetFrameRate,
        long timestampTicks)
    {
        var elapsedPlusOne = checked(timestampTicks - originTicks + 1);
        var completeGroups = elapsedPlusOne / frequency;
        var remainingTicks = elapsedPlusOne % frequency;
        var slot = checked(completeGroups * targetFrameRate);
        if (remainingTicks == 0)
        {
            return slot;
        }

        var partialNumerator = checked(remainingTicks * targetFrameRate);
        slot = checked(slot + partialNumerator / frequency);
        if (partialNumerator % frequency != 0)
        {
            slot = checked(slot + 1);
        }
        return slot;
    }
}
