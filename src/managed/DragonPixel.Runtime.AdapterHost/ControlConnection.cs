using System.IO.Pipes;
using System.Net.Sockets;

namespace DragonPixel.Runtime;

internal sealed class ControlConnection : IAsyncDisposable
{
    private readonly IDisposable _owner;
    private readonly string? _unixPath;

    private ControlConnection(Stream stream, IDisposable owner, string? unixPath)
    {
        Stream = stream;
        _owner = owner;
        _unixPath = unixPath;
    }

    public Stream Stream { get; }

    public static async Task<ControlConnection> AcceptAsync(string endpoint, CancellationToken cancellationToken)
    {
        if (OperatingSystem.IsWindows())
        {
            var pipe = new NamedPipeServerStream(
                endpoint,
                PipeDirection.InOut,
                1,
                PipeTransmissionMode.Byte,
                PipeOptions.Asynchronous);
            await pipe.WaitForConnectionAsync(cancellationToken).ConfigureAwait(false);
            return new ControlConnection(pipe, pipe, null);
        }

        endpoint = Path.GetFullPath(endpoint);
        if (File.Exists(endpoint))
        {
            File.Delete(endpoint);
        }
        Directory.CreateDirectory(Path.GetDirectoryName(endpoint)!);
        var listener = new Socket(AddressFamily.Unix, SocketType.Stream, ProtocolType.Unspecified);
        try
        {
            listener.Bind(new UnixDomainSocketEndPoint(endpoint));
            listener.Listen(1);
            var accepted = await listener.AcceptAsync(cancellationToken).ConfigureAwait(false);
            listener.Dispose();
            var stream = new NetworkStream(accepted, ownsSocket: true);
            return new ControlConnection(stream, stream, endpoint);
        }
        catch
        {
            listener.Dispose();
            if (File.Exists(endpoint))
            {
                File.Delete(endpoint);
            }
            throw;
        }
    }

    public async ValueTask DisposeAsync()
    {
        if (Stream is IAsyncDisposable asynchronous)
        {
            await asynchronous.DisposeAsync().ConfigureAwait(false);
        }
        else
        {
            _owner.Dispose();
        }
        if (_unixPath is not null && File.Exists(_unixPath))
        {
            File.Delete(_unixPath);
        }
    }
}
