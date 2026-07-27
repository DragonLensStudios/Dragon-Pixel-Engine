using System.IO.MemoryMappedFiles;

namespace DragonPixel.Runtime;

public static class FrameLayout
{
    public const int Version1HeaderSize = 64;
    public const int Version2HeaderSize = 96;
    public const int Magic = 0x46504544; // DPEF in little-endian bytes.
    public const int Version1 = 1;
    public const int Version2 = 2;
    public const int PixelFormatBgra8 = 1;
    public const int ContentSprite = 1;
    public const int ContentStaticMesh = 2;
    public const int ContentTilemap = 4;

    public static int HeaderSize(int version) => version switch
    {
        Version1 => Version1HeaderSize,
        Version2 => Version2HeaderSize,
        _ => throw new ArgumentOutOfRangeException(nameof(version)),
    };
}

internal sealed unsafe class SharedFrameBuffer : IDisposable
{
    private readonly FileStream _file;
    private readonly MemoryMappedFile _mapping;
    private readonly MemoryMappedViewAccessor _view;
    private readonly byte* _viewPointer;
    private readonly int _headerSize;
    private long _sequence;

    public SharedFrameBuffer(string path, int maximumWidth, int maximumHeight, int version)
    {
        MaximumWidth = maximumWidth;
        MaximumHeight = maximumHeight;
        Version = version;
        _headerSize = FrameLayout.HeaderSize(version);
        var maximumStride = checked(maximumWidth * 4);
        var size = checked((long)_headerSize + ((long)maximumStride * maximumHeight));
        Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(path))!);
        _file = new FileStream(path, FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.ReadWrite | FileShare.Delete);
        _file.SetLength(size);
        _mapping = MemoryMappedFile.CreateFromFile(
            _file,
            null,
            size,
            MemoryMappedFileAccess.ReadWrite,
            HandleInheritability.None,
            leaveOpen: true);
        _view = _mapping.CreateViewAccessor(0, size, MemoryMappedFileAccess.ReadWrite);
        WriteStaticHeader(maximumWidth, maximumHeight);
        byte* viewPointer = null;
        _view.SafeMemoryMappedViewHandle.AcquirePointer(ref viewPointer);
        _viewPointer = viewPointer + _view.PointerOffset;
    }

    public int MaximumWidth { get; }
    public int MaximumHeight { get; }
    public int Version { get; }
    public int HeaderSize => _headerSize;

    public void Publish(FrameworkFrame frame, long timestampTicks, int adapterHash)
    {
        if (frame.Width is <= 0 || frame.Width > MaximumWidth
            || frame.Height is <= 0 || frame.Height > MaximumHeight)
        {
            throw new ArgumentException("Frame dimensions exceed the negotiated shared buffer.", nameof(frame));
        }
        var stride = checked(frame.Width * 4);
        if (frame.Bgra8Pixels.Length != checked(stride * frame.Height))
        {
            throw new ArgumentException("Pixel buffer size does not match the published frame.", nameof(frame));
        }

        var writingSequence = Interlocked.Add(ref _sequence, 2) - 1;
        _view.Write(24, writingSequence);
        Thread.MemoryBarrier();
        _view.Write(8, frame.Width);
        _view.Write(12, frame.Height);
        _view.Write(16, stride);
        frame.Bgra8Pixels.AsSpan().CopyTo(new Span<byte>(_viewPointer + _headerSize, frame.Bgra8Pixels.Length));
        _view.Write(32, timestampTicks);
        _view.Write(40, frame.ContentFlags);
        _view.Write(44, adapterHash);
        // The v1 reserved bytes retain the original input-to-present correlation marker.
        _view.Write(48, frame.InputRevision);
        if (Version >= FrameLayout.Version2)
        {
            _view.Write(56, frame.FrameRevision);
            _view.Write(64, frame.SnapshotRevision);
            _view.Write(72, frame.CameraRevision);
            _view.Write(80, frame.CommandRevision);
            _view.Write(88, 0L); // Reserved for a future negotiated v2 extension.
        }
        Thread.MemoryBarrier();
        _view.Write(24, writingSequence + 1);
    }

    public void Dispose()
    {
        _view.SafeMemoryMappedViewHandle.ReleasePointer();
        _view.Dispose();
        _mapping.Dispose();
        _file.Dispose();
    }

    private void WriteStaticHeader(int width, int height)
    {
        _view.Write(0, FrameLayout.Magic);
        _view.Write(4, Version);
        _view.Write(8, width);
        _view.Write(12, height);
        _view.Write(16, checked(width * 4));
        _view.Write(20, FrameLayout.PixelFormatBgra8);
        _view.Write(24, 0L);
        _view.Write(32, 0L);
        _view.Write(40, 0);
        _view.Write(44, 0);
        _view.Write(48, 0L);
        _view.Write(56, 0L);
        if (Version >= FrameLayout.Version2)
        {
            _view.Write(64, 0L);
            _view.Write(72, 0L);
            _view.Write(80, 0L);
            _view.Write(88, 0L);
        }
        _view.Flush();
    }
}
