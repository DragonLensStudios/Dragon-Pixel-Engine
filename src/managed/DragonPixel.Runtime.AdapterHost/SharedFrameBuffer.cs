using System.IO.MemoryMappedFiles;

namespace DragonPixel.Runtime;

public static class FrameLayout
{
    public const int HeaderSize = 64;
    public const int Magic = 0x46504544; // DPEF in little-endian bytes.
    public const int Version = 1;
    public const int PixelFormatBgra8 = 1;
    public const int ContentSprite = 1;
    public const int ContentStaticMesh = 2;
}

internal sealed class SharedFrameBuffer : IDisposable
{
    private readonly FileStream _file;
    private readonly MemoryMappedFile _mapping;
    private readonly MemoryMappedViewAccessor _view;
    private long _sequence;

    public SharedFrameBuffer(string path, int width, int height)
    {
        Width = width;
        Height = height;
        Stride = checked(width * 4);
        var size = checked((long)FrameLayout.HeaderSize + ((long)Stride * height));
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
        WriteStaticHeader();
    }

    public int Width { get; }
    public int Height { get; }
    public int Stride { get; }

    public void Publish(byte[] pixels, long timestampTicks, int contentFlags, int adapterHash)
    {
        if (pixels.Length != checked(Stride * Height))
        {
            throw new ArgumentException("Pixel buffer size does not match the shared frame layout.", nameof(pixels));
        }

        var writingSequence = Interlocked.Add(ref _sequence, 2) - 1;
        _view.Write(24, writingSequence);
        Thread.MemoryBarrier();
        _view.WriteArray(FrameLayout.HeaderSize, pixels, 0, pixels.Length);
        _view.Write(32, timestampTicks);
        _view.Write(40, contentFlags);
        _view.Write(44, adapterHash);
        Thread.MemoryBarrier();
        _view.Write(24, writingSequence + 1);
    }

    public void Dispose()
    {
        _view.Dispose();
        _mapping.Dispose();
        _file.Dispose();
    }

    private void WriteStaticHeader()
    {
        _view.Write(0, FrameLayout.Magic);
        _view.Write(4, FrameLayout.Version);
        _view.Write(8, Width);
        _view.Write(12, Height);
        _view.Write(16, Stride);
        _view.Write(20, FrameLayout.PixelFormatBgra8);
        _view.Write(24, 0L);
        _view.Write(32, 0L);
        _view.Write(40, 0);
        _view.Write(44, 0);
        _view.Flush();
    }
}
