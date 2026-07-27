using System.Text.Json;
using Microsoft.Xna.Framework;
using Microsoft.Xna.Framework.Graphics;

namespace DragonPixel.Runtime.FrameworkWorker;

internal sealed class GraphicsConformanceProbe : Game
{
    private readonly GraphicsDeviceManager _graphics;
    private RenderTarget2D? _target;
    private Texture2D? _sprite;
    private SpriteBatch? _spriteBatch;
    private BasicEffect? _effect;
    private bool _completed;
    private int _distinctColors;

    private GraphicsConformanceProbe()
    {
        _graphics = new GraphicsDeviceManager(this)
        {
            PreferredBackBufferWidth = 128,
            PreferredBackBufferHeight = 128,
            SynchronizeWithVerticalRetrace = false,
        };
        IsFixedTimeStep = false;
        IsMouseVisible = false;
    }

    public static int Execute(string adapterName)
    {
        try
        {
            using var probe = new GraphicsConformanceProbe();
            probe.RunOneFrame();
            if (!probe._completed)
            {
                probe.RunOneFrame();
            }
            if (!probe._completed || probe._distinctColors < 3)
            {
                throw new InvalidOperationException("GraphicsDevice probe did not produce the sprite/static-mesh fixture.");
            }
            Console.WriteLine(JsonSerializer.Serialize(new
            {
                succeeded = true,
                adapter = adapterName,
                graphicsDevice = probe.GraphicsDevice.GraphicsDeviceStatus.ToString(),
                distinctColors = probe._distinctColors,
                content = new[] { "sprite2D", "staticMesh3D" },
            }));
            return 0;
        }
        catch (Exception exception)
        {
            Console.Error.WriteLine(exception);
            return 1;
        }
    }

    protected override void LoadContent()
    {
        _target = new RenderTarget2D(GraphicsDevice, 128, 128, false, SurfaceFormat.Color, DepthFormat.Depth24);
        _sprite = new Texture2D(GraphicsDevice, 2, 2);
        _sprite.SetData(new[] { Color.Magenta, Color.Purple, Color.Purple, Color.Magenta });
        _spriteBatch = new SpriteBatch(GraphicsDevice);
        _effect = new BasicEffect(GraphicsDevice)
        {
            VertexColorEnabled = true,
            LightingEnabled = false,
            World = Matrix.CreateRotationY(0.55f) * Matrix.CreateRotationX(-0.35f),
            View = Matrix.CreateLookAt(new Vector3(0, 0, 5), Vector3.Zero, Vector3.Up),
            Projection = Matrix.CreatePerspectiveFieldOfView(MathHelper.PiOver4, 1.0f, 0.1f, 100.0f),
        };
        base.LoadContent();
    }

    protected override void Draw(GameTime gameTime)
    {
        _ = gameTime;
        if (_completed || _target is null || _sprite is null || _spriteBatch is null || _effect is null)
        {
            return;
        }
        GraphicsDevice.SetRenderTarget(_target);
        GraphicsDevice.Clear(Color.CornflowerBlue);
        _spriteBatch.Begin(samplerState: SamplerState.PointClamp);
        _spriteBatch.Draw(_sprite, new Rectangle(4, 4, 42, 42), Color.White);
        _spriteBatch.End();

        var vertices = CreateCubeVertices();
        foreach (var pass in _effect.CurrentTechnique.Passes)
        {
            pass.Apply();
            GraphicsDevice.DrawUserPrimitives(PrimitiveType.TriangleList, vertices, 0, vertices.Length / 3);
        }
        GraphicsDevice.SetRenderTarget(null);

        var pixels = new Color[128 * 128];
        _target.GetData(pixels);
        _distinctColors = pixels.Distinct().Count();
        _completed = true;
        Exit();
    }

    protected override void UnloadContent()
    {
        _effect?.Dispose();
        _spriteBatch?.Dispose();
        _sprite?.Dispose();
        _target?.Dispose();
        base.UnloadContent();
    }

    private static VertexPositionColor[] CreateCubeVertices()
    {
        var positions = new[]
        {
            new Vector3(-1, -1, -1), new Vector3(1, -1, -1), new Vector3(1, 1, -1), new Vector3(-1, 1, -1),
            new Vector3(-1, -1, 1), new Vector3(1, -1, 1), new Vector3(1, 1, 1), new Vector3(-1, 1, 1),
        };
        var indices = new[]
        {
            0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7,
            0, 1, 5, 0, 5, 4, 2, 3, 7, 2, 7, 6,
            1, 2, 6, 1, 6, 5, 3, 0, 4, 3, 4, 7,
        };
        return indices.Select((index, order) => new VertexPositionColor(
            positions[index],
            order % 3 == 0 ? Color.OrangeRed : order % 3 == 1 ? Color.Gold : Color.White)).ToArray();
    }
}
