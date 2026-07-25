using System.Collections.Concurrent;
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
    private FrameworkRenderRequest? _request;
    private FrameworkFrame? _completedFrame;
    private RenderTarget2D? _colorTarget;
    private RenderTarget2D? _idTarget;
    private SpriteBatch? _spriteBatch;
    private Texture2D? _checkerTexture;
    private Texture2D? _whiteTexture;
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
    private bool _contentLoaded;
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
        _worldTransforms.Clear();
        _resolvingTransforms.Clear();
        var entities = request.Scene.Entities.ToDictionary(static entity => entity.Id, StringComparer.Ordinal);
        var camera = CreateCamera(request, entities);
        var visible = request.Scene.Entities.Where(static entity => entity.Enabled).ToArray();
        var pickTokens = visible
            .Where(static entity => entity.Sprite is not null || entity.Mesh is not null)
            .Select((entity, index) => (entity.Id, Token: index + 1))
            .ToDictionary(static pair => pair.Id, static pair => pair.Token, StringComparer.Ordinal);

        DrawColorFrame(request, visible, entities, camera);
        DrawIdFrame(request, visible, entities, camera, pickTokens);
        _retainedPickTokens = pickTokens;
        _retainedFrameRevision = request.FrameRevision;
        _retainedSnapshotRevision = request.SnapshotRevision;
        _retainedCameraRevision = request.CameraRevision;
        _retainedCommandRevision = request.CommandRevision;
        ServicePendingOperations();

        _colorTarget!.GetData(_colorReadback);
        ConvertToBgra(_colorReadback, _bgraReadback);

        var contentFlags = 0;
        if (visible.Any(static entity => entity.Sprite is not null))
        {
            contentFlags |= FrameLayout.ContentSprite;
        }
        if (visible.Any(static entity => entity.Mesh is not null))
        {
            contentFlags |= FrameLayout.ContentStaticMesh;
        }

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
            request.InputRevision);
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
        _whiteTexture?.Dispose();
        _checkerTexture?.Dispose();
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
            idTokens is null ? BlendState.AlphaBlend : BlendState.Opaque,
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
            var size = Math.Clamp(
                (int)MathF.Round(Math.Min(request.Width, request.Height) * 0.14f * Math.Max(scaleX, scaleY)),
                12,
                Math.Min(request.Width, request.Height));
            var destination = new Rectangle(
                (int)MathF.Round(position.X) - (size / 2),
                (int)MathF.Round(position.Y) - (size / 2),
                size,
                size);
            var color = idTokens is null
                ? ToXnaColor(entity.Sprite!.Color)
                : TokenColor(idTokens[entity.Id]);
            _spriteBatch.Draw(
                idTokens is null ? _checkerTexture! : _whiteTexture!,
                destination,
                color);
        }
        _spriteBatch.End();
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
        var local = Matrix.CreateScale(ToVector3(transform.Scale))
            * Matrix.CreateFromQuaternion(rotation)
            * Matrix.CreateTranslation(ToVector3(transform.Position));
        if (entity.ParentId is not null && entities.TryGetValue(entity.ParentId, out var parent))
        {
            local *= ResolveWorldTransform(parent, entities, elapsed);
        }
        _resolvingTransforms.Remove(entity.Id);
        _worldTransforms[entity.Id] = local;
        return local;
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
            RenderTargetUsage.DiscardContents);
        _idTarget = new RenderTarget2D(
            GraphicsDevice,
            width,
            height,
            false,
            SurfaceFormat.Color,
            DepthFormat.Depth24,
            0,
            RenderTargetUsage.DiscardContents);
        _colorReadback = new Color[checked(width * height)];
        _bgraReadback = new byte[checked(width * height * 4)];
    }

    private void DisposeTargets()
    {
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
    private sealed record PendingPick(int X, int Y, long MinimumFrameRevision)
    {
        public TaskCompletionSource<FrameworkPickResult> Completion { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);
    }
}
