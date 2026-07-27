using System.Collections.Concurrent;
using System.Security.Cryptography;
using DragonPixel.Runtime;
using Microsoft.Xna.Framework;
using Microsoft.Xna.Framework.Graphics;

namespace DragonPixel.Runtime.FrameworkWorker;

internal sealed class FrameworkSceneAdapter : Game, IFrameworkSceneAdapter
{
    private const int MaximumPointLights = 4;
    private readonly GraphicsDeviceManager _graphics;
    private readonly ConcurrentQueue<PendingPick> _pendingPicks = new();
    private readonly Color[] _singlePickReadback = new Color[1];
    private readonly Dictionary<string, Matrix> _worldTransforms = new(StringComparer.Ordinal);
    private readonly HashSet<string> _resolvingTransforms = new(StringComparer.Ordinal);
    private readonly Dictionary<string, Texture2D> _tileTextures = new(StringComparer.Ordinal);
    private readonly HashSet<string> _failedTileTextures = new(StringComparer.Ordinal);
    private readonly Dictionary<string, Texture2D> _assetTextures = new(StringComparer.Ordinal);
    private readonly HashSet<string> _failedAssetTextures = new(StringComparer.Ordinal);
    private readonly Dictionary<string, Vector3> _runtimeInputOffsets = new(StringComparer.Ordinal);
    private readonly Dictionary<string, InputMotionState> _observedInputMotion = new(StringComparer.Ordinal);
    private FrameworkRenderRequest? _request;
    private FrameworkFrame? _completedFrame;
    private RenderTarget2D? _colorTarget;
    private RenderTarget2D? _idTarget;
    private SpriteBatch? _spriteBatch;
    private Texture2D? _checkerTexture;
    private Texture2D? _whiteTexture;
    private Texture2D? _circleTexture;
    private BasicEffect? _meshEffect;
    private BasicEffect? _idEffect;
    private BasicEffect? _overlayEffect;
    private VertexBuffer? _cubeVertexBuffer;
    private IndexBuffer? _cubeIndexBuffer;
    private Color[] _colorReadback = Array.Empty<Color>();
    private byte[] _bgraReadback = Array.Empty<byte>();
    private IReadOnlyDictionary<string, int> _retainedPickTokens = new Dictionary<string, int>();
    private long _retainedFrameRevision;
    private long _retainedSnapshotRevision;
    private long _retainedCameraRevision;
    private long _retainedCommandRevision;
    private long _idTargetSnapshotRevision = -1;
    private long _idTargetCameraRevision = -1;
    private bool _idTargetUseSceneCamera;
    private bool _idTargetValid;
    private bool _contentLoaded;
    private long _inputOffsetSnapshotRevision;
    private long _observedInputRevision;
    private long _reflectedInputRevision;
    private long _pendingInputRevision;
    private byte[]? _pendingInputPixelBaseline;
    private long _lastRenderedInputSnapshotRevision;
    private int _lastRenderedInputWidth;
    private int _lastRenderedInputHeight;
    private TimeSpan? _lastInputElapsed;
    private bool _disposed;

    public FrameworkSceneAdapter()
    {
        _graphics = new GraphicsDeviceManager(this)
        {
            PreferredBackBufferWidth = 64,
            PreferredBackBufferHeight = 64,
            SynchronizeWithVerticalRetrace = false,
            PreferMultiSampling = false,
            GraphicsProfile = GraphicsProfile.Reach,
        };
        IsFixedTimeStep = false;
        IsMouseVisible = false;
        Window.AllowUserResizing = false;
        Window.Title = $"Dragon Pixel {Name} Worker";
    }

#if DPE_MONOGAME
    public string Name => "MonoGame";
    public bool Experimental => false;
    public string Backend => "MonoGame DesktopGL";
#elif DPE_KNI
    public string Name => "KNI";
    public bool Experimental => true;
    public string Backend => "KNI SDL2 OpenGL";
#else
#error A framework symbol must be defined.
#endif

    public string Version => typeof(Matrix).Assembly.GetName().Version?.ToString() ?? "unknown";

    public string Device => _contentLoaded
        ? $"{GraphicsAdapter.DefaultAdapter.Description}; {GraphicsDevice.GraphicsProfile}; "
            + GraphicsDevice.GraphicsDeviceStatus
        : "not initialized";

    public FrameworkFrame RenderFrame(FrameworkRenderRequest request)
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        _request = request;
        _completedFrame = null;
        RunOneFrame();
        if (_completedFrame is null)
        {
            // Some framework/platform combinations spend their first tick only creating the device.
            RunOneFrame();
        }
        return _completedFrame
            ?? throw new InvalidOperationException($"{Name} did not complete an offscreen frame.");
    }

    public FrameworkPickResult Pick(int x, int y, long minimumFrameRevision)
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        var pending = new PendingPick(x, y, minimumFrameRevision);
        _pendingPicks.Enqueue(pending);
        try
        {
            return pending.Completion.Task
                .WaitAsync(TimeSpan.FromSeconds(2))
                .GetAwaiter()
                .GetResult();
        }
        catch (TimeoutException)
        {
            pending.Completion.TrySetCanceled();
            throw new TimeoutException("The graphics thread did not complete the pick request.");
        }
    }

    public void ServicePendingOperations()
    {
        while (_pendingPicks.TryPeek(out var pending))
        {
            if (pending.Completion.Task.IsCompleted)
            {
                _pendingPicks.TryDequeue(out _);
                continue;
            }
            if (_retainedFrameRevision < pending.MinimumFrameRevision)
            {
                break;
            }
            if (!_pendingPicks.TryDequeue(out pending))
            {
                continue;
            }

            string? entityId = null;
            if (_idTarget is not null
                && pending.X >= 0
                && pending.Y >= 0
                && pending.X < _idTarget.Width
                && pending.Y < _idTarget.Height)
            {
                _idTarget.GetData(
                    0,
                    new Rectangle(pending.X, pending.Y, 1, 1),
                    _singlePickReadback,
                    0,
                    1);
                var color = _singlePickReadback[0];
                var token = color.R | (color.G << 8) | (color.B << 16);
                entityId = _retainedPickTokens.FirstOrDefault(pair => pair.Value == token).Key;
            }

            pending.Completion.TrySetResult(
                new FrameworkPickResult(
                    entityId,
                    _retainedFrameRevision,
                    _retainedSnapshotRevision,
                    _retainedCameraRevision,
                    _retainedCommandRevision));
        }
    }

    protected override void LoadContent()
    {
        _spriteBatch = new SpriteBatch(GraphicsDevice);
        _checkerTexture = new Texture2D(GraphicsDevice, 8, 8, false, SurfaceFormat.Color);
        var checker = new Color[64];
        for (var y = 0; y < 8; y++)
        {
            for (var x = 0; x < 8; x++)
            {
                checker[(y * 8) + x] = ((x / 2) + (y / 2)) % 2 == 0
                    ? Color.White
                    : new Color(0.42f, 0.42f, 0.48f, 1.0f);
            }
        }
        _checkerTexture.SetData(checker);
        _whiteTexture = new Texture2D(GraphicsDevice, 1, 1, false, SurfaceFormat.Color);
        _whiteTexture.SetData([Color.White]);
        _circleTexture = new Texture2D(GraphicsDevice, 64, 64, false, SurfaceFormat.Color);
        var circle = new Color[64 * 64];
        var center = 31.5f;
        var radiusSquared = 31.0f * 31.0f;
        for (var y = 0; y < 64; y++)
        {
            for (var x = 0; x < 64; x++)
            {
                var deltaX = x - center;
                var deltaY = y - center;
                circle[(y * 64) + x] = (deltaX * deltaX) + (deltaY * deltaY) <= radiusSquared
                    ? Color.White
                    : Color.Transparent;
            }
        }
        _circleTexture.SetData(circle);

        _meshEffect = new BasicEffect(GraphicsDevice)
        {
            VertexColorEnabled = false,
            TextureEnabled = false,
            LightingEnabled = true,
            PreferPerPixelLighting = false,
        };
        _meshEffect.EnableDefaultLighting();
        _idEffect = new BasicEffect(GraphicsDevice)
        {
            VertexColorEnabled = false,
            TextureEnabled = false,
            LightingEnabled = false,
        };
        _overlayEffect = new BasicEffect(GraphicsDevice)
        {
            VertexColorEnabled = true,
            TextureEnabled = false,
            LightingEnabled = false,
        };
        CreateCubeBuffers();
        _contentLoaded = true;
        base.LoadContent();
    }

    protected override void Draw(GameTime gameTime)
    {
        _ = gameTime;
        var request = _request;
        if (request is null || !_contentLoaded)
        {
            return;
        }

        EnsureTargets(request.Width, request.Height);
        var entities = request.Scene.Entities.ToDictionary(static entity => entity.Id, StringComparer.Ordinal);
        var visible = request.Scene.Entities.Where(static entity => entity.Enabled).ToArray();
        var renderAffectingEntities = FindRenderAffectingEntities(visible, entities);
        var inputMotionChangedPixels = UpdateInputMotion(request, visible, renderAffectingEntities);
        _worldTransforms.Clear();
        _resolvingTransforms.Clear();
        var camera = CreateCamera(request, entities);
        var pickTokens = visible
            .Where(static entity => entity.Sprite is not null || entity.Mesh is not null || entity.Tilemap is not null)
            .Select((entity, index) => (entity.Id, Token: index + 1))
            .ToDictionary(static pair => pair.Id, static pair => pair.Token, StringComparer.Ordinal);
        var renderAffectingAnimation = visible.Any(entity =>
            Math.Abs(entity.RotatorDegreesPerSecond) > 0.000001f
            && (renderAffectingEntities.Contains(entity.Id) || request.UseSceneCamera));
        var contentFlags = 0;
        if (visible.Any(static entity => entity.Sprite is not null))
        {
            contentFlags |= FrameLayout.ContentSprite;
        }
        if (visible.Any(static entity => entity.Mesh is not null))
        {
            contentFlags |= FrameLayout.ContentStaticMesh;
        }
        if (visible.Any(static entity => entity.Tilemap is not null))
        {
            contentFlags |= FrameLayout.ContentTilemap;
        }

        DrawColorFrame(request, visible, entities, camera);
        // Retain the device-produced ID target across visually identical frames. KNI otherwise
        // serializes a second full-size render before every color readback even when no pickable
        // transform changed; PreserveContents keeps the retained target valid for later picks.
        if (!_idTargetValid
            || _idTargetSnapshotRevision != request.SnapshotRevision
            || _idTargetCameraRevision != request.CameraRevision
            || _idTargetUseSceneCamera != request.UseSceneCamera
            || inputMotionChangedPixels
            || renderAffectingAnimation)
        {
            DrawIdFrame(request, visible, entities, camera, pickTokens);
            _idTargetSnapshotRevision = request.SnapshotRevision;
            _idTargetCameraRevision = request.CameraRevision;
            _idTargetUseSceneCamera = request.UseSceneCamera;
            _idTargetValid = true;
        }
        _retainedPickTokens = pickTokens;
        _retainedFrameRevision = request.FrameRevision;
        _retainedSnapshotRevision = request.SnapshotRevision;
        _retainedCameraRevision = request.CameraRevision;
        _retainedCommandRevision = request.CommandRevision;
        ServicePendingOperations();

        _colorTarget!.GetData(_colorReadback);
        ConvertToBgra(_colorReadback, _bgraReadback);
        CompleteInputReflection(request, inputMotionChangedPixels);

        _completedFrame = new FrameworkFrame(
            _bgraReadback,
            request.Width,
            request.Height,
            contentFlags,
            Backend,
            Device,
            request.FrameRevision,
            request.SnapshotRevision,
            request.CameraRevision,
            request.CommandRevision,
            _reflectedInputRevision);
        base.Draw(gameTime);
    }

    protected override void UnloadContent()
    {
        DisposeTargets();
        _cubeIndexBuffer?.Dispose();
        _cubeVertexBuffer?.Dispose();
        _overlayEffect?.Dispose();
        _idEffect?.Dispose();
        _meshEffect?.Dispose();
        _circleTexture?.Dispose();
        _whiteTexture?.Dispose();
        _checkerTexture?.Dispose();
        foreach (var texture in _tileTextures.Values) texture.Dispose();
        _tileTextures.Clear();
        foreach (var texture in _assetTextures.Values) texture.Dispose();
        _assetTextures.Clear();
        _failedTileTextures.Clear();
        _spriteBatch?.Dispose();
        base.UnloadContent();
    }

    protected override void Dispose(bool disposing)
    {
        if (disposing)
        {
            _disposed = true;
            while (_pendingPicks.TryDequeue(out var pending))
            {
                pending.Completion.TrySetException(new ObjectDisposedException(nameof(FrameworkSceneAdapter)));
            }
        }
        base.Dispose(disposing);
    }

    private void DrawColorFrame(
        FrameworkRenderRequest request,
        IReadOnlyList<RenderEntity> visible,
        IReadOnlyDictionary<string, RenderEntity> entities,
        CameraMatrices camera)
    {
        GraphicsDevice.SetRenderTarget(_colorTarget);
        GraphicsDevice.Viewport = new Viewport(0, 0, request.Width, request.Height);
        GraphicsDevice.Clear(ClearOptions.Target | ClearOptions.DepthBuffer, new Color(27, 25, 34), 1, 0);
        DrawMeshes(request, visible, entities, camera, idTokens: null);
        DrawTilemaps(request, visible, entities, camera, idTokens: null);
        DrawSprites(request, visible, entities, camera, idTokens: null);
        if (!request.UseSceneCamera)
        {
            DrawColliderOverlays(request, visible, entities, camera);
        }
        GraphicsDevice.SetRenderTarget(null);
    }

    private void DrawIdFrame(
        FrameworkRenderRequest request,
        IReadOnlyList<RenderEntity> visible,
        IReadOnlyDictionary<string, RenderEntity> entities,
        CameraMatrices camera,
        IReadOnlyDictionary<string, int> pickTokens)
    {
        GraphicsDevice.SetRenderTarget(_idTarget);
        GraphicsDevice.Viewport = new Viewport(0, 0, request.Width, request.Height);
        GraphicsDevice.Clear(ClearOptions.Target | ClearOptions.DepthBuffer, Color.Black, 1, 0);
        DrawMeshes(request, visible, entities, camera, pickTokens);
        DrawTilemaps(request, visible, entities, camera, pickTokens);
        DrawSprites(request, visible, entities, camera, pickTokens);
        GraphicsDevice.SetRenderTarget(null);
    }

    private void DrawMeshes(
        FrameworkRenderRequest request,
        IReadOnlyList<RenderEntity> visible,
        IReadOnlyDictionary<string, RenderEntity> entities,
        CameraMatrices camera,
        IReadOnlyDictionary<string, int>? idTokens)
    {
        GraphicsDevice.BlendState = BlendState.Opaque;
        GraphicsDevice.DepthStencilState = DepthStencilState.Default;
        GraphicsDevice.RasterizerState = RasterizerState.CullCounterClockwise;
        GraphicsDevice.SetVertexBuffer(_cubeVertexBuffer);
        GraphicsDevice.Indices = _cubeIndexBuffer;
        foreach (var entity in visible.Where(static entity => entity.Mesh is not null))
        {
            var world = ResolveWorldTransform(entity, entities, request.Elapsed);
            var effect = idTokens is null ? _meshEffect! : _idEffect!;
            effect.World = world;
            effect.View = camera.View;
            effect.Projection = camera.Projection;
            if (idTokens is null)
            {
                ConfigureLighting(effect, entity, visible, entities, request.Elapsed);
            }
            else
            {
                effect.DiffuseColor = TokenColor(idTokens[entity.Id]).ToVector3();
                effect.EmissiveColor = Vector3.Zero;
                effect.Alpha = 1;
            }

            foreach (var pass in effect.CurrentTechnique.Passes)
            {
                pass.Apply();
                GraphicsDevice.DrawIndexedPrimitives(
                    PrimitiveType.TriangleList,
                    0,
                    0,
                    _cubeIndexBuffer!.IndexCount / 3);
            }
        }
    }

    private void DrawSprites(
        FrameworkRenderRequest request,
        IReadOnlyList<RenderEntity> visible,
        IReadOnlyDictionary<string, RenderEntity> entities,
        CameraMatrices camera,
        IReadOnlyDictionary<string, int>? idTokens)
    {
        var spriteEntities = visible
            .Where(static entity => entity.Sprite is not null)
            .OrderBy(static entity => entity.Sprite!.Layer);
        _spriteBatch!.Begin(
            SpriteSortMode.Deferred,
            BlendState.AlphaBlend,
            SamplerState.PointClamp,
            DepthStencilState.None,
            RasterizerState.CullNone);
        foreach (var entity in spriteEntities)
        {
            var world = ResolveWorldTransform(entity, entities, request.Elapsed);
            var position = GraphicsDevice.Viewport.Project(
                world.Translation,
                camera.Projection,
                camera.View,
                Matrix.Identity);
            if (position.Z is < 0 or > 1)
            {
                continue;
            }

            var scaleX = new Vector3(world.M11, world.M12, world.M13).Length();
            var scaleY = new Vector3(world.M21, world.M22, world.M23).Length();
            var baseSize = Math.Clamp(
                Math.Min(request.Width, request.Height) * 0.14f,
                12.0f,
                Math.Min(request.Width, request.Height));
            var color = idTokens is null
                ? ToXnaColor(entity.Sprite!.Color)
                : TokenColor(idTokens[entity.Id]);
            var primitive = entity.Sprite!.AssetId;
            var texture = primitive.Equals("builtin://circle", StringComparison.OrdinalIgnoreCase)
                ? _circleTexture!
                : primitive.Equals("builtin://square", StringComparison.OrdinalIgnoreCase)
                    ? _whiteTexture!
                    : idTokens is null
                        && request.Scene.Assets.TryGetValue(primitive, out var asset)
                        ? GetAssetTexture(asset) ?? _checkerTexture!
                        : idTokens is null ? _checkerTexture! : _whiteTexture!;
            var rotation = MathF.Atan2(world.M12, world.M11);
            _spriteBatch.Draw(
                texture,
                new Vector2(position.X, position.Y),
                null,
                color,
                rotation,
                new Vector2(texture.Width * 0.5f, texture.Height * 0.5f),
                new Vector2(
                    (baseSize * Math.Max(scaleX, 0.0001f)) / texture.Width,
                    (baseSize * Math.Max(scaleY, 0.0001f)) / texture.Height),
                SpriteEffects.None,
                0.0f);
        }
        _spriteBatch.End();
    }

    private void DrawTilemaps(
        FrameworkRenderRequest request,
        IReadOnlyList<RenderEntity> visible,
        IReadOnlyDictionary<string, RenderEntity> entities,
        CameraMatrices camera,
        IReadOnlyDictionary<string, int>? idTokens)
    {
        var batches = visible
            .Where(static entity => entity.Tilemap is not null)
            .SelectMany(entity => entity.Tilemap!.Layers
                .Where(static layer => layer.Visible)
                .SelectMany(layer => layer.Cells.Select(cell => new
                {
                    Entity = entity,
                    Tilemap = entity.Tilemap!,
                    Layer = layer,
                    Cell = cell,
                })))
            .OrderBy(static item => item.Tilemap.BaseLayer + item.Layer.Order)
            .ThenBy(static item => item.Tilemap.TextureAssetId, StringComparer.Ordinal)
            .ThenBy(static item => item.Cell.Y)
            .ThenBy(static item => item.Cell.X)
            .ToArray();
        if (batches.Length == 0)
        {
            return;
        }
        _spriteBatch!.Begin(
            SpriteSortMode.Deferred,
            idTokens is null ? BlendState.AlphaBlend : BlendState.Opaque,
            SamplerState.PointClamp,
            DepthStencilState.None,
            RasterizerState.CullNone);
        foreach (var item in batches)
        {
            var world = ResolveWorldTransform(item.Entity, entities, request.Elapsed);
            var worldPosition = Vector3.Transform(new Vector3(
                (item.Cell.X + 0.5f) * item.Tilemap.CellWidth,
                (item.Cell.Y + 0.5f) * item.Tilemap.CellHeight,
                0), world);
            var position = GraphicsDevice.Viewport.Project(
                worldPosition,
                camera.Projection,
                camera.View,
                Matrix.Identity);
            if (position.Z is < 0 or > 1)
            {
                continue;
            }
            var scaleX = new Vector3(world.M11, world.M12, world.M13).Length();
            var scaleY = new Vector3(world.M21, world.M22, world.M23).Length();
            var width = Math.Clamp(
                (int)MathF.Round(Math.Min(request.Width, request.Height) * 0.10f
                    * item.Tilemap.CellWidth * Math.Max(0.01f, scaleX)),
                2,
                Math.Min(request.Width, request.Height));
            var height = Math.Clamp(
                (int)MathF.Round(Math.Min(request.Width, request.Height) * 0.10f
                    * item.Tilemap.CellHeight * Math.Max(0.01f, scaleY)),
                2,
                Math.Min(request.Width, request.Height));
            var destination = new Rectangle(
                (int)MathF.Round(position.X) - (width / 2),
                (int)MathF.Round(position.Y) - (height / 2),
                width,
                height);
            var tileTexture = idTokens is null ? GetTileTexture(item.Tilemap) : null;
            Rectangle? source = null;
            if (tileTexture is not null
                && item.Cell.SourceX >= 0 && item.Cell.SourceY >= 0
                && item.Cell.SourceX + item.Cell.SourceWidth <= tileTexture.Width
                && item.Cell.SourceY + item.Cell.SourceHeight <= tileTexture.Height)
            {
                source = new Rectangle(item.Cell.SourceX, item.Cell.SourceY,
                    item.Cell.SourceWidth, item.Cell.SourceHeight);
            }
            else
            {
                tileTexture = null;
            }
            var color = idTokens is not null
                ? TokenColor(idTokens[item.Entity.Id])
                : tileTexture is not null
                    ? ToXnaColor(item.Tilemap.Tint)
                    : ToXnaColor(new RenderColor(
                        item.Cell.Color.R * item.Tilemap.Tint.R,
                        item.Cell.Color.G * item.Tilemap.Tint.G,
                        item.Cell.Color.B * item.Tilemap.Tint.B,
                        item.Cell.Color.A * item.Tilemap.Tint.A));
            var effects = (item.Cell.FlipX ? SpriteEffects.FlipHorizontally : SpriteEffects.None)
                | (item.Cell.FlipY ? SpriteEffects.FlipVertically : SpriteEffects.None);
            var rotation = item.Cell.RotationQuarterTurns * MathF.PI * 0.5f;
            var origin = source.HasValue
                ? new Vector2(source.Value.Width * 0.5f, source.Value.Height * 0.5f)
                : Vector2.Zero;
            _spriteBatch.Draw(tileTexture ?? _whiteTexture!, destination, source, color,
                rotation, origin, effects, 0.0f);
        }
        _spriteBatch.End();
    }

    private Texture2D? GetTileTexture(RenderTilemap tilemap)
    {
        if (tilemap.TexturePng.Length == 0) return null;
        var key = $"{tilemap.TextureAssetId}:{Convert.ToHexString(SHA256.HashData(tilemap.TexturePng))}";
        if (_tileTextures.TryGetValue(key, out var existing)) return existing;
        if (_failedTileTextures.Contains(key)) return null;
        try
        {
            using var stream = new MemoryStream(tilemap.TexturePng, writable: false);
            var texture = Texture2D.FromStream(GraphicsDevice, stream);
            _tileTextures.Add(key, texture);
            return texture;
        }
        catch
        {
            _failedTileTextures.Add(key);
            return null;
        }
    }

    private Texture2D? GetAssetTexture(RenderAsset asset)
    {
        if (asset.ImmutableBytes.Length == 0) return null;
        var key = $"{asset.AssetId}:{asset.ContentHash}";
        if (_assetTextures.TryGetValue(key, out var existing)) return existing;
        if (_failedAssetTextures.Contains(key)) return null;
        try
        {
            using var stream = new MemoryStream(asset.ImmutableBytes, writable: false);
            var texture = Texture2D.FromStream(GraphicsDevice, stream);
            _assetTextures.Add(key, texture);
            return texture;
        }
        catch (Exception)
        {
            _failedAssetTextures.Add(key);
            return null;
        }
    }

    private void DrawColliderOverlays(
        FrameworkRenderRequest request,
        IReadOnlyList<RenderEntity> visible,
        IReadOnlyDictionary<string, RenderEntity> entities,
        CameraMatrices camera)
    {
        GraphicsDevice.SetVertexBuffer(null);
        GraphicsDevice.Indices = null;
        GraphicsDevice.BlendState = BlendState.AlphaBlend;
        GraphicsDevice.DepthStencilState = DepthStencilState.None;
        GraphicsDevice.RasterizerState = RasterizerState.CullNone;

        var effect = _overlayEffect!;
        effect.View = camera.View;
        effect.Projection = camera.Projection;
        foreach (var entity in visible.Where(static entity => entity.Colliders.Count != 0))
        {
            effect.World = ResolveWorldTransform(entity, entities, request.Elapsed);
            foreach (var collider in entity.Colliders)
            {
                var color = collider.Sensor
                    ? new Color(255, 198, 64, 230)
                    : new Color(68, 210, 255, 230);
                var vertices = CreateColliderOutline(collider, color);
                if (vertices.Length == 0)
                {
                    continue;
                }

                foreach (var pass in effect.CurrentTechnique.Passes)
                {
                    pass.Apply();
                    GraphicsDevice.DrawUserPrimitives(
                        PrimitiveType.LineList,
                        vertices,
                        0,
                        vertices.Length / 2);
                }
            }
        }
    }

    private static VertexPositionColor[] CreateColliderOutline(RenderCollider collider, Color color)
    {
        var offset = ToVector3(collider.Offset);
        var halfSize = new Vector3(
            HalfExtent(collider.Size.X),
            HalfExtent(collider.Size.Y),
            HalfExtent(collider.Size.Z));
        return collider.Kind switch
        {
            RenderColliderKind.Box2D => CreateBox2DOutline(offset, halfSize, color),
            RenderColliderKind.Circle2D => CreateRingOutline(offset, halfSize.X, RingPlane.XY, color),
            RenderColliderKind.Box3D => CreateBox3DOutline(offset, halfSize, color),
            RenderColliderKind.Sphere3D => CreateSphereOutline(offset, halfSize.X, color),
            _ => Array.Empty<VertexPositionColor>(),
        };
    }

    private static VertexPositionColor[] CreateBox2DOutline(
        Vector3 offset,
        Vector3 halfSize,
        Color color)
    {
        var corners = new[]
        {
            offset + new Vector3(-halfSize.X, -halfSize.Y, 0),
            offset + new Vector3(halfSize.X, -halfSize.Y, 0),
            offset + new Vector3(halfSize.X, halfSize.Y, 0),
            offset + new Vector3(-halfSize.X, halfSize.Y, 0),
        };
        var vertices = new List<VertexPositionColor>(8);
        AddLine(vertices, corners[0], corners[1], color);
        AddLine(vertices, corners[1], corners[2], color);
        AddLine(vertices, corners[2], corners[3], color);
        AddLine(vertices, corners[3], corners[0], color);
        return vertices.ToArray();
    }

    private static VertexPositionColor[] CreateBox3DOutline(
        Vector3 offset,
        Vector3 halfSize,
        Color color)
    {
        var corners = new[]
        {
            offset + new Vector3(-halfSize.X, -halfSize.Y, -halfSize.Z),
            offset + new Vector3(halfSize.X, -halfSize.Y, -halfSize.Z),
            offset + new Vector3(halfSize.X, halfSize.Y, -halfSize.Z),
            offset + new Vector3(-halfSize.X, halfSize.Y, -halfSize.Z),
            offset + new Vector3(-halfSize.X, -halfSize.Y, halfSize.Z),
            offset + new Vector3(halfSize.X, -halfSize.Y, halfSize.Z),
            offset + new Vector3(halfSize.X, halfSize.Y, halfSize.Z),
            offset + new Vector3(-halfSize.X, halfSize.Y, halfSize.Z),
        };
        var vertices = new List<VertexPositionColor>(24);
        foreach (var (first, second) in new[]
                 {
                     (0, 1), (1, 2), (2, 3), (3, 0),
                     (4, 5), (5, 6), (6, 7), (7, 4),
                     (0, 4), (1, 5), (2, 6), (3, 7),
                 })
        {
            AddLine(vertices, corners[first], corners[second], color);
        }
        return vertices.ToArray();
    }

    private static VertexPositionColor[] CreateSphereOutline(
        Vector3 offset,
        float radius,
        Color color)
    {
        var vertices = new List<VertexPositionColor>(144);
        vertices.AddRange(CreateRingOutline(offset, radius, RingPlane.XY, color));
        vertices.AddRange(CreateRingOutline(offset, radius, RingPlane.XZ, color));
        vertices.AddRange(CreateRingOutline(offset, radius, RingPlane.YZ, color));
        return vertices.ToArray();
    }

    private static VertexPositionColor[] CreateRingOutline(
        Vector3 offset,
        float radius,
        RingPlane plane,
        Color color)
    {
        const int segmentCount = 24;
        var vertices = new List<VertexPositionColor>(segmentCount * 2);
        for (var segment = 0; segment < segmentCount; segment++)
        {
            var startAngle = MathHelper.TwoPi * segment / segmentCount;
            var endAngle = MathHelper.TwoPi * (segment + 1) / segmentCount;
            AddLine(
                vertices,
                offset + RingPoint(radius, startAngle, plane),
                offset + RingPoint(radius, endAngle, plane),
                color);
        }
        return vertices.ToArray();
    }

    private static Vector3 RingPoint(float radius, float angle, RingPlane plane)
    {
        var first = radius * MathF.Cos(angle);
        var second = radius * MathF.Sin(angle);
        return plane switch
        {
            RingPlane.XY => new Vector3(first, second, 0),
            RingPlane.XZ => new Vector3(first, 0, second),
            RingPlane.YZ => new Vector3(0, first, second),
            _ => Vector3.Zero,
        };
    }

    private static void AddLine(
        ICollection<VertexPositionColor> vertices,
        Vector3 start,
        Vector3 end,
        Color color)
    {
        vertices.Add(new VertexPositionColor(start, color));
        vertices.Add(new VertexPositionColor(end, color));
    }

    private static float HalfExtent(float size) => Math.Max(0.005f, MathF.Abs(size) * 0.5f);

    private void ConfigureLighting(
        BasicEffect effect,
        RenderEntity meshEntity,
        IReadOnlyList<RenderEntity> visible,
        IReadOnlyDictionary<string, RenderEntity> entities,
        TimeSpan elapsed)
    {
        var material = meshEntity.Mesh!.BaseColor;
        var ambient = new Vector3(0.12f);
        var directionals = visible.Where(static entity => entity.Light?.Kind == RenderLightKind.Directional)
            .Take(3)
            .ToArray();
        foreach (var lightEntity in visible.Where(static entity => entity.Light?.Kind == RenderLightKind.Ambient))
        {
            var light = lightEntity.Light!;
            ambient += ToVector3(light.Color) * Math.Min(light.Intensity, 4) * 0.25f;
        }

        var meshPosition = ResolveWorldTransform(meshEntity, entities, elapsed).Translation;
        var pointContribution = Vector3.Zero;
        foreach (var lightEntity in visible
                     .Where(static entity => entity.Light?.Kind == RenderLightKind.Point)
                     .Take(MaximumPointLights))
        {
            var light = lightEntity.Light!;
            var lightPosition = ResolveWorldTransform(lightEntity, entities, elapsed).Translation;
            var distance = Vector3.Distance(meshPosition, lightPosition);
            var attenuation = Math.Clamp(1.0f - (distance / light.Range), 0, 1);
            pointContribution += ToVector3(light.Color)
                * Math.Min(light.Intensity, 8)
                * attenuation
                * attenuation
                * 0.2f;
        }

        effect.LightingEnabled = true;
        effect.DiffuseColor = Vector3.Clamp(ToVector3(material) + pointContribution, Vector3.Zero, Vector3.One);
        effect.EmissiveColor = Vector3.Clamp(ToVector3(material) * ambient, Vector3.Zero, Vector3.One);
        effect.Alpha = material.A;
        var directionalSlots = new[] { effect.DirectionalLight0, effect.DirectionalLight1, effect.DirectionalLight2 };
        for (var index = 0; index < directionalSlots.Length; index++)
        {
            var slot = directionalSlots[index];
            if (index >= directionals.Length)
            {
                slot.Enabled = false;
                continue;
            }
            var lightEntity = directionals[index];
            var light = lightEntity.Light!;
            var lightWorld = ResolveWorldTransform(lightEntity, entities, elapsed);
            var direction = Vector3.TransformNormal(Vector3.Forward, lightWorld);
            if (direction.LengthSquared() < 0.000001f)
            {
                direction = new Vector3(-0.4f, -0.8f, -0.3f);
            }
            direction.Normalize();
            slot.Enabled = true;
            slot.Direction = direction;
            slot.DiffuseColor = Vector3.Clamp(
                ToVector3(light.Color) * Math.Min(light.Intensity, 4),
                Vector3.Zero,
                Vector3.One);
            slot.SpecularColor = slot.DiffuseColor * 0.2f;
        }
    }

    private CameraMatrices CreateCamera(
        FrameworkRenderRequest request,
        IReadOnlyDictionary<string, RenderEntity> entities)
    {
        var sceneCameraEntity = request.UseSceneCamera
            ? request.Scene.Entities.FirstOrDefault(
                static entity => entity.Enabled && entity.Camera is { Primary: true })
            : null;
        if (sceneCameraEntity is not null)
        {
            var world = ResolveWorldTransform(sceneCameraEntity, entities, request.Elapsed);
            var position = world.Translation;
            var forward = Vector3.TransformNormal(Vector3.Forward, world);
            var up = Vector3.TransformNormal(Vector3.Up, world);
            if (forward.LengthSquared() < 0.000001f)
            {
                forward = Vector3.Forward;
            }
            if (up.LengthSquared() < 0.000001f)
            {
                up = Vector3.Up;
            }
            return CreateCameraMatrices(
                position,
                position + forward,
                up,
                sceneCameraEntity.Camera!,
                request.Width,
                request.Height);
        }

        var editor = request.EditorCamera;
        var positionVector = ToVector3(editor.Position);
        var targetVector = ToVector3(editor.Target);
        if (Vector3.DistanceSquared(positionVector, targetVector) < 0.000001f)
        {
            targetVector = positionVector + Vector3.Forward;
        }
        return CreateCameraMatrices(
            positionVector,
            targetVector,
            Vector3.Up,
            new RenderCamera(
                true,
                editor.Orthographic,
                editor.FieldOfViewDegrees,
                editor.OrthographicSize,
                0.05f,
                2000),
            request.Width,
            request.Height);
    }

    private static CameraMatrices CreateCameraMatrices(
        Vector3 position,
        Vector3 target,
        Vector3 up,
        RenderCamera camera,
        int width,
        int height)
    {
        var aspect = width / (float)Math.Max(1, height);
        var view = Matrix.CreateLookAt(position, target, up);
        var projection = camera.Orthographic
            ? Matrix.CreateOrthographic(
                Math.Max(0.01f, camera.OrthographicSize) * aspect,
                Math.Max(0.01f, camera.OrthographicSize),
                Math.Max(0.001f, camera.NearPlane),
                Math.Max(camera.NearPlane + 0.01f, camera.FarPlane))
            : Matrix.CreatePerspectiveFieldOfView(
                MathHelper.ToRadians(Math.Clamp(camera.FieldOfViewDegrees, 1, 179)),
                aspect,
                Math.Max(0.001f, camera.NearPlane),
                Math.Max(camera.NearPlane + 0.01f, camera.FarPlane));
        return new CameraMatrices(view, projection);
    }

    private Matrix ResolveWorldTransform(
        RenderEntity entity,
        IReadOnlyDictionary<string, RenderEntity> entities,
        TimeSpan elapsed)
    {
        if (_worldTransforms.TryGetValue(entity.Id, out var cached))
        {
            return cached;
        }
        if (!_resolvingTransforms.Add(entity.Id))
        {
            return Matrix.Identity;
        }

        var transform = entity.Transform;
        var rotation = new Quaternion(
            transform.Rotation.X,
            transform.Rotation.Y,
            transform.Rotation.Z,
            transform.Rotation.W);
        if (Math.Abs(entity.RotatorDegreesPerSecond) > 0.0001f)
        {
            rotation *= Quaternion.CreateFromAxisAngle(
                Vector3.Up,
                MathHelper.ToRadians(entity.RotatorDegreesPerSecond * (float)elapsed.TotalSeconds));
        }
        var runtimePosition = ToVector3(transform.Position)
            + (_runtimeInputOffsets.TryGetValue(entity.Id, out var offset) ? offset : Vector3.Zero);
        var local = Matrix.CreateScale(ToVector3(transform.Scale))
            * Matrix.CreateFromQuaternion(rotation)
            * Matrix.CreateTranslation(runtimePosition);
        if (entity.ParentId is not null && entities.TryGetValue(entity.ParentId, out var parent))
        {
            local *= ResolveWorldTransform(parent, entities, elapsed);
        }
        _resolvingTransforms.Remove(entity.Id);
        _worldTransforms[entity.Id] = local;
        return local;
    }

    private bool UpdateInputMotion(
        FrameworkRenderRequest request,
        IReadOnlyList<RenderEntity> visible,
        IReadOnlySet<string> renderAffectingEntities)
    {
        if (_inputOffsetSnapshotRevision != request.SnapshotRevision)
        {
            _runtimeInputOffsets.Clear();
            _observedInputMotion.Clear();
            _lastInputElapsed = null;
            _inputOffsetSnapshotRevision = request.SnapshotRevision;
            _observedInputRevision = 0;
            _reflectedInputRevision = 0;
            _pendingInputRevision = 0;
            _pendingInputPixelBaseline = null;
            _lastRenderedInputSnapshotRevision = 0;
            _lastRenderedInputWidth = 0;
            _lastRenderedInputHeight = 0;
        }

        var currentInputMotion = new Dictionary<string, InputMotionState>(StringComparer.Ordinal);
        foreach (var entity in visible.Where(static entity => entity.InputMotion is not null))
        {
            var motion = entity.InputMotion!;
            if (!renderAffectingEntities.Contains(entity.Id) || Math.Abs(motion.Speed) <= 0.000001f)
            {
                continue;
            }
            var horizontal = request.InputActions.TryGetValue(motion.HorizontalAction, out var x) ? x : 0.0f;
            var vertical = request.InputActions.TryGetValue(motion.VerticalAction, out var y) ? y : 0.0f;
            if (!float.IsFinite(horizontal) || !float.IsFinite(vertical))
            {
                horizontal = 0.0f;
                vertical = 0.0f;
            }
            currentInputMotion.Add(entity.Id, new InputMotionState(horizontal, vertical));
        }
        ObserveInputRevision(request, currentInputMotion);

        var delta = _lastInputElapsed.HasValue
            ? Math.Clamp((float)(request.Elapsed - _lastInputElapsed.Value).TotalSeconds, 0.0f, 0.1f)
            : 0.0f;
        _lastInputElapsed = request.Elapsed;
        if (delta <= 0.0f)
        {
            return false;
        }

        var renderAffectingMotionChanged = false;
        foreach (var entity in visible.Where(static entity => entity.InputMotion is not null))
        {
            var motion = entity.InputMotion!;
            var horizontal = request.InputActions.TryGetValue(motion.HorizontalAction, out var x) ? x : 0.0f;
            var vertical = request.InputActions.TryGetValue(motion.VerticalAction, out var y) ? y : 0.0f;
            if (!float.IsFinite(horizontal) || !float.IsFinite(vertical)) continue;
            var offsetDelta = new Vector3(horizontal, vertical, 0.0f) * motion.Speed * delta;
            if (offsetDelta == Vector3.Zero) continue;
            _runtimeInputOffsets.TryGetValue(entity.Id, out var offset);
            offset += offsetDelta;
            _runtimeInputOffsets[entity.Id] = offset;
            renderAffectingMotionChanged |= renderAffectingEntities.Contains(entity.Id);
        }
        return renderAffectingMotionChanged;
    }

    private void ObserveInputRevision(
        FrameworkRenderRequest request,
        IReadOnlyDictionary<string, InputMotionState> currentInputMotion)
    {
        if (request.InputRevision <= _observedInputRevision)
        {
            return;
        }
        _observedInputRevision = request.InputRevision;
        if (InputMotionEquals(_observedInputMotion, currentInputMotion))
        {
            return;
        }

        var wasActive = _observedInputMotion.Values.Any(static state => state.Active);
        var isActive = currentInputMotion.Values.Any(static state => state.Active);
        _observedInputMotion.Clear();
        foreach (var (entityId, state) in currentInputMotion)
        {
            _observedInputMotion.Add(entityId, state);
        }

        if (wasActive && !isActive)
        {
            _reflectedInputRevision = request.InputRevision;
            _pendingInputRevision = 0;
            _pendingInputPixelBaseline = null;
            return;
        }
        if (!isActive)
        {
            return;
        }

        _pendingInputRevision = request.InputRevision;
        _pendingInputPixelBaseline = _lastRenderedInputSnapshotRevision == request.SnapshotRevision
            && _lastRenderedInputWidth == request.Width
            && _lastRenderedInputHeight == request.Height
            ? _bgraReadback.ToArray()
            : null;
    }

    private void CompleteInputReflection(FrameworkRenderRequest request, bool renderAffectingMotionChanged)
    {
        if (_pendingInputRevision != 0)
        {
            if (_pendingInputPixelBaseline is null
                || _pendingInputPixelBaseline.Length != _bgraReadback.Length)
            {
                _pendingInputPixelBaseline = _bgraReadback.ToArray();
            }
            else if (renderAffectingMotionChanged
                && !_bgraReadback.AsSpan().SequenceEqual(_pendingInputPixelBaseline))
            {
                _reflectedInputRevision = _pendingInputRevision;
                _pendingInputRevision = 0;
                _pendingInputPixelBaseline = null;
            }
        }

        _lastRenderedInputSnapshotRevision = request.SnapshotRevision;
        _lastRenderedInputWidth = request.Width;
        _lastRenderedInputHeight = request.Height;
    }

    private static HashSet<string> FindRenderAffectingEntities(
        IReadOnlyList<RenderEntity> visible,
        IReadOnlyDictionary<string, RenderEntity> entities)
    {
        var result = new HashSet<string>(StringComparer.Ordinal);
        foreach (var renderable in visible.Where(static entity =>
                     entity.Sprite is not null || entity.Mesh is not null || entity.Tilemap is not null))
        {
            RenderEntity? current = renderable;
            var visited = new HashSet<string>(StringComparer.Ordinal);
            while (current is not null && visited.Add(current.Id))
            {
                result.Add(current.Id);
                current = current.ParentId is not null && entities.TryGetValue(current.ParentId, out var parent)
                    ? parent
                    : null;
            }
        }
        return result;
    }

    private static bool InputMotionEquals(
        IReadOnlyDictionary<string, InputMotionState> left,
        IReadOnlyDictionary<string, InputMotionState> right)
    {
        if (left.Count != right.Count) return false;
        foreach (var (entityId, state) in left)
        {
            if (!right.TryGetValue(entityId, out var candidate) || candidate != state)
            {
                return false;
            }
        }
        return true;
    }

    private void EnsureTargets(int width, int height)
    {
        if (_colorTarget?.Width == width && _colorTarget.Height == height)
        {
            return;
        }
        DisposeTargets();
        _colorTarget = new RenderTarget2D(
            GraphicsDevice,
            width,
            height,
            false,
            SurfaceFormat.Color,
            DepthFormat.Depth24,
            0,
            RenderTargetUsage.PreserveContents);
        _idTarget = new RenderTarget2D(
            GraphicsDevice,
            width,
            height,
            false,
            SurfaceFormat.Color,
            DepthFormat.Depth24,
            0,
            RenderTargetUsage.PreserveContents);
        _colorReadback = new Color[checked(width * height)];
        _bgraReadback = new byte[checked(width * height * 4)];
    }

    private void DisposeTargets()
    {
        _idTargetValid = false;
        _idTarget?.Dispose();
        _idTarget = null;
        _colorTarget?.Dispose();
        _colorTarget = null;
    }

    private void CreateCubeBuffers()
    {
        var vertices = new[]
        {
            new VertexPositionNormalTexture(new Vector3(-0.5f, -0.5f, -0.5f), Vector3.Backward, Vector2.Zero),
            new VertexPositionNormalTexture(new Vector3(0.5f, -0.5f, -0.5f), Vector3.Backward, Vector2.Zero),
            new VertexPositionNormalTexture(new Vector3(0.5f, 0.5f, -0.5f), Vector3.Backward, Vector2.Zero),
            new VertexPositionNormalTexture(new Vector3(-0.5f, 0.5f, -0.5f), Vector3.Backward, Vector2.Zero),
            new VertexPositionNormalTexture(new Vector3(-0.5f, -0.5f, 0.5f), Vector3.Forward, Vector2.Zero),
            new VertexPositionNormalTexture(new Vector3(0.5f, -0.5f, 0.5f), Vector3.Forward, Vector2.Zero),
            new VertexPositionNormalTexture(new Vector3(0.5f, 0.5f, 0.5f), Vector3.Forward, Vector2.Zero),
            new VertexPositionNormalTexture(new Vector3(-0.5f, 0.5f, 0.5f), Vector3.Forward, Vector2.Zero),
        };
        var indices = new ushort[]
        {
            0, 2, 1, 0, 3, 2,
            4, 5, 6, 4, 6, 7,
            0, 1, 5, 0, 5, 4,
            2, 3, 7, 2, 7, 6,
            1, 2, 6, 1, 6, 5,
            3, 0, 4, 3, 4, 7,
        };
        _cubeVertexBuffer = new VertexBuffer(
            GraphicsDevice,
            VertexPositionNormalTexture.VertexDeclaration,
            vertices.Length,
            BufferUsage.WriteOnly);
        _cubeVertexBuffer.SetData(vertices);
        _cubeIndexBuffer = new IndexBuffer(
            GraphicsDevice,
            IndexElementSize.SixteenBits,
            indices.Length,
            BufferUsage.WriteOnly);
        _cubeIndexBuffer.SetData(indices);
    }

    private static void ConvertToBgra(IReadOnlyList<Color> source, byte[] destination)
    {
        for (var index = 0; index < source.Count; index++)
        {
            var color = source[index];
            var offset = index * 4;
            destination[offset] = color.B;
            destination[offset + 1] = color.G;
            destination[offset + 2] = color.R;
            destination[offset + 3] = color.A;
        }
    }

    private static Color TokenColor(int token) => new(
        (byte)(token & 0xff),
        (byte)((token >> 8) & 0xff),
        (byte)((token >> 16) & 0xff),
        byte.MaxValue);

    private static Color ToXnaColor(RenderColor color) => new(
        Math.Clamp(color.R, 0, 1),
        Math.Clamp(color.G, 0, 1),
        Math.Clamp(color.B, 0, 1),
        Math.Clamp(color.A, 0, 1));

    private static Vector3 ToVector3(RenderVector3 vector) => new(vector.X, vector.Y, vector.Z);
    private static Vector3 ToVector3(RenderColor color) => new(color.R, color.G, color.B);

    private enum RingPlane
    {
        XY,
        XZ,
        YZ,
    }

    private readonly record struct CameraMatrices(Matrix View, Matrix Projection);
    private readonly record struct InputMotionState(float Horizontal, float Vertical)
    {
        public bool Active => Horizontal != 0.0f || Vertical != 0.0f;
    }
    private sealed record PendingPick(int X, int Y, long MinimumFrameRevision)
    {
        public TaskCompletionSource<FrameworkPickResult> Completion { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);
    }
}
