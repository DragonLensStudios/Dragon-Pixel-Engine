using System.Reflection;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using DragonPixel.Contracts;
using NumericsQuaternion = System.Numerics.Quaternion;
using NumericsVector3 = System.Numerics.Vector3;

namespace DragonPixel.Runtime;

internal sealed record TileExtensionContext(
    int CellX,
    int CellY,
    int Elevation,
    uint Layout,
    ulong DeterministicSeed,
    double ElapsedSeconds,
    string MapId,
    string LayerId,
    string TileSetId,
    string TileId,
    string PayloadJson,
    string NeighborhoodJson);

internal sealed record TileExtensionEvaluationResult(
    bool Succeeded,
    string ResultJson,
    string ErrorCode,
    string ErrorMessage);

internal sealed record TileExtensionBrushProposal(
    bool Succeeded,
    string ResultJson,
    int CommandCount,
    string ErrorCode,
    string ErrorMessage);

internal sealed class ProjectComponentRuntime : IDisposable
{
    private const double FixedDeltaSeconds = 1.0 / 60.0;
    private const double FixedComparisonToleranceSeconds = 0.0000001;
    private const int MaximumFixedCatchUpSteps = 4;
    private static readonly HashSet<string> BuiltinComponentTypeIds = new(StringComparer.Ordinal)
    {
        BuiltinComponentIds.Transform,
        BuiltinComponentIds.Rotator,
        BuiltinComponentIds.Camera,
        BuiltinComponentIds.Sprite,
        BuiltinComponentIds.Mesh,
        BuiltinComponentIds.Material,
        BuiltinComponentIds.Light,
        BuiltinComponentIds.RigidBody2D,
        BuiltinComponentIds.BoxCollider2D,
        BuiltinComponentIds.CircleCollider2D,
        BuiltinComponentIds.PolygonCollider2D,
        BuiltinComponentIds.RigidBody3D,
        BuiltinComponentIds.BoxCollider3D,
        BuiltinComponentIds.SphereCollider3D,
        BuiltinComponentIds.Tilemap2D,
        BuiltinComponentIds.TilemapCollider2D,
        BuiltinComponentIds.InputMotion2D,
    };

    private readonly object _gate = new();
    private Dictionary<string, IProjectComponentFactory> _managedFactories = new(StringComparer.Ordinal);
    private Dictionary<string, NativePlugin> _nativePlugins = new(StringComparer.Ordinal);
    private Dictionary<string, TileExtensionPlugin> _tileExtensions = new(StringComparer.Ordinal);
    private readonly List<IRuntimeInstance> _instances = new();
    private Dictionary<string, DragonPixel.Contracts.Transform> _managedTransforms = new(StringComparer.Ordinal);
    private Dictionary<string, ManagedTransformOrigin> _managedTransformOrigins = new(StringComparer.Ordinal);
    private readonly List<string> _diagnostics = new();
    private double _fixedAccumulatorSeconds;
    private double _fixedElapsedSeconds;
    private bool _disposed;

    public ProjectComponentRuntime(string? manifestPath)
    {
        if (string.IsNullOrWhiteSpace(manifestPath))
        {
            return;
        }
        try
        {
            LoadModules(Path.GetFullPath(manifestPath));
        }
        catch (Exception exception)
        {
            AddDiagnostic(ProjectComponentDiagnosticSeverity.Error,
                $"Project runtime modules are unavailable; authoring records remain preserved: {exception.Message}");
        }
    }

    public IReadOnlyList<string> Diagnostics
    {
        get
        {
            lock (_gate)
            {
                return _diagnostics.ToArray();
            }
        }
    }

    public int FactoryCount
    {
        get
        {
            lock (_gate)
            {
                return _managedFactories.Count + _nativePlugins.Count;
            }
        }
    }

    public int InstanceCount
    {
        get
        {
            lock (_gate)
            {
                return _instances.Count;
            }
        }
    }

    public int TileExtensionCount
    {
        get
        {
            lock (_gate)
            {
                return _tileExtensions.Count;
            }
        }
    }

    public IReadOnlyList<TileExtensionEvaluationResult> EvaluateTiles(
        string pluginId,
        IReadOnlyList<TileExtensionContext> contexts)
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        TileExtensionPlugin? extension;
        lock (_gate) _tileExtensions.TryGetValue(pluginId, out extension);
        if (extension is null)
        {
            return contexts.Select(static _ => new TileExtensionEvaluationResult(
                false, string.Empty, "DPE-TILE-EXT-UNAVAILABLE",
                "The requested tile extension is missing or incompatible.")).ToArray();
        }
        try
        {
            return extension.Evaluate(contexts);
        }
        catch (Exception exception)
        {
            AddDiagnostic(ProjectComponentDiagnosticSeverity.Error,
                $"Tile extension {pluginId} evaluation was disabled for this request: {exception.Message}");
            return contexts.Select(_ => new TileExtensionEvaluationResult(
                false, string.Empty, "DPE-TILE-EXT-EVALUATION-FAILED", exception.Message)).ToArray();
        }
    }

    public TileExtensionBrushProposal ProposeBrush(
        string pluginId,
        TileExtensionContext context,
        string requestJson)
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        TileExtensionPlugin? extension;
        lock (_gate) _tileExtensions.TryGetValue(pluginId, out extension);
        if (extension is null)
        {
            return new TileExtensionBrushProposal(false, string.Empty, 0,
                "DPE-TILE-EXT-UNAVAILABLE",
                "The requested tile extension is missing or incompatible.");
        }
        try
        {
            return extension.ProposeBrush(context, requestJson);
        }
        catch (Exception exception)
        {
            AddDiagnostic(ProjectComponentDiagnosticSeverity.Error,
                $"Tile extension {pluginId} brush proposal was rejected: {exception.Message}");
            return new TileExtensionBrushProposal(false, string.Empty, 0,
                "DPE-TILE-EXT-PROPOSAL-FAILED", exception.Message);
        }
    }

    public void ReloadSnapshot(string snapshotPath, RenderScene? renderScene = null)
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        using var document = JsonDocument.Parse(File.ReadAllBytes(snapshotPath));
        var replacements = new List<IRuntimeInstance>();
        var replacementTransforms = new Dictionary<string, DragonPixel.Contracts.Transform>(StringComparer.Ordinal);
        var replacementOrigins = new Dictionary<string, ManagedTransformOrigin>(StringComparer.Ordinal);
        var renderEntities = renderScene?.Entities.ToDictionary(static entity => entity.Id, StringComparer.Ordinal)
            ?? new Dictionary<string, RenderEntity>(StringComparer.Ordinal);
        var unavailableTypes = new HashSet<string>(StringComparer.Ordinal);
        if (document.RootElement.TryGetProperty("entities", out var entities)
            && entities.ValueKind == JsonValueKind.Array)
        {
            foreach (var entity in entities.EnumerateArray())
            {
                var entityId = entity.TryGetProperty("id", out var idValue) ? idValue.GetString() ?? string.Empty : string.Empty;
                if (!entity.TryGetProperty("components", out var components) || components.ValueKind != JsonValueKind.Array)
                {
                    continue;
                }
                foreach (var component in components.EnumerateArray())
                {
                    if (component.TryGetProperty("enabled", out var enabled) && enabled.ValueKind == JsonValueKind.False)
                    {
                        continue;
                    }
                    var typeId = component.TryGetProperty("typeId", out var typeValue)
                        ? typeValue.GetString() ?? string.Empty : string.Empty;
                    var properties = component.TryGetProperty("properties", out var propertyValue)
                        ? propertyValue.GetRawText() : "{}";
                    try
                    {
                        if (_managedFactories.TryGetValue(typeId, out var factory))
                        {
                            if (!replacementTransforms.TryGetValue(entityId, out var transform))
                            {
                                var origin = renderEntities.TryGetValue(entityId, out var renderEntity)
                                    ? ManagedTransformOrigin.FromRenderTransform(renderEntity.Transform)
                                    : ManagedTransformOrigin.Identity;
                                transform = origin.CreateTransform();
                                replacementTransforms.Add(entityId, transform);
                                replacementOrigins.Add(entityId, origin);
                            }
                            replacements.Add(new ManagedInstance(
                                factory,
                                entityId,
                                typeId,
                                properties,
                                transform,
                                AddDiagnostic));
                        }
                        else if (_nativePlugins.TryGetValue(typeId, out var plugin))
                        {
                            replacements.Add(plugin.Create(entityId, properties, AddDiagnostic));
                        }
                        else if (!BuiltinComponentTypeIds.Contains(typeId)
                                 && unavailableTypes.Add(typeId))
                        {
                            AddDiagnostic(ProjectComponentDiagnosticSeverity.Warning,
                                $"Enabled component type {typeId} has no loaded project factory/plugin; "
                                + $"runtime activation is disabled and its authoring record on {entityId} remains preserved.");
                        }
                    }
                    catch (Exception exception)
                    {
                        AddDiagnostic(ProjectComponentDiagnosticSeverity.Error,
                            $"Could not create component {typeId} on {entityId}: {exception.Message}");
                    }
                }
            }
        }

        lock (_gate)
        {
            foreach (var instance in _instances)
            {
                instance.Dispose();
            }
            _instances.Clear();
            _instances.AddRange(replacements);
            _managedTransforms = replacementTransforms;
            _managedTransformOrigins = replacementOrigins;
            _fixedAccumulatorSeconds = 0.0;
            _fixedElapsedSeconds = 0.0;
            AddDiagnosticLocked($"Loaded {_instances.Count} project component instance(s) from the immutable runtime snapshot.");
        }
    }

    public RenderScene ApplyTransforms(RenderScene scene)
    {
        lock (_gate)
        {
            if (_managedTransforms.Count == 0) return scene;
            var changed = false;
            var entities = new RenderEntity[scene.Entities.Count];
            for (var index = 0; index < scene.Entities.Count; ++index)
            {
                var entity = scene.Entities[index];
                if (_managedTransforms.TryGetValue(entity.Id, out var transform))
                {
                    var state = transform.GetRuntimeState();
                    if (state.Revision != 0)
                    {
                        var quaternion = NumericsQuaternion.CreateFromYawPitchRoll(
                            DegreesToRadians(state.Rotation.Y),
                            DegreesToRadians(state.Rotation.X),
                            DegreesToRadians(state.Rotation.Z));
                        entity = entity with
                        {
                            Transform = new RenderTransform(
                                new RenderVector3(state.Position.X, state.Position.Y, state.Position.Z),
                                new RenderQuaternion(quaternion.X, quaternion.Y, quaternion.Z, quaternion.W),
                                new RenderVector3(state.Scale.X, state.Scale.Y, state.Scale.Z)),
                        };
                        changed = true;
                    }
                }
                entities[index] = entity;
            }
            return changed ? scene with { Entities = entities } : scene;
        }
    }

    public void ResetRuntimeTransforms()
    {
        lock (_gate)
        {
            foreach (var (entityId, transform) in _managedTransforms)
            {
                if (_managedTransformOrigins.TryGetValue(entityId, out var origin))
                    transform.ResetRuntimeState(origin.Position, origin.Rotation, origin.Scale);
            }
        }
    }

    public void Update(
        TimeSpan elapsed,
        TimeSpan delta,
        IReadOnlyDictionary<string, float> inputActions) =>
        Update(elapsed, delta, inputActions, RuntimeInputSnapshot.Neutral);

    public void Update(
        TimeSpan elapsed,
        TimeSpan delta,
        IReadOnlyDictionary<string, float> inputActions,
        RuntimeInputSnapshot inputState)
    {
        lock (_gate)
        {
            var deltaSeconds = Math.Max(0.0, delta.TotalSeconds);
            _fixedAccumulatorSeconds += deltaSeconds;
            var fixedSteps = 0;
            while (_fixedAccumulatorSeconds + FixedComparisonToleranceSeconds >= FixedDeltaSeconds
                   && fixedSteps < MaximumFixedCatchUpSteps)
            {
                _fixedElapsedSeconds += FixedDeltaSeconds;
                var fixedUpdate = new ProjectComponentUpdate(
                    _fixedElapsedSeconds,
                    FixedDeltaSeconds,
                    inputActions,
                    inputState);
                foreach (var instance in _instances)
                {
                    instance.FixedUpdate(fixedUpdate);
                }
                _fixedAccumulatorSeconds = Math.Max(
                    0.0,
                    _fixedAccumulatorSeconds - FixedDeltaSeconds);
                fixedSteps++;
            }
            if (_fixedAccumulatorSeconds + FixedComparisonToleranceSeconds >= FixedDeltaSeconds)
            {
                var retained = _fixedAccumulatorSeconds % FixedDeltaSeconds;
                var dropped = _fixedAccumulatorSeconds - retained;
                _fixedAccumulatorSeconds = retained;
                AddDiagnosticLocked(FormattableString.Invariant(
                    $"[Warning] Project component fixed update dropped {dropped:0.000000} second(s) after {MaximumFixedCatchUpSteps} catch-up steps."));
            }

            var update = new ProjectComponentUpdate(
                elapsed.TotalSeconds,
                deltaSeconds,
                inputActions,
                inputState);
            foreach (var instance in _instances)
            {
                instance.Update(update);
            }
            foreach (var instance in _instances)
            {
                instance.LateUpdate(update);
            }
            foreach (var instance in _instances)
            {
                instance.SubmitRender(update);
            }
        }
    }

    public void Dispose()
    {
        if (_disposed) return;
        lock (_gate)
        {
            foreach (var instance in _instances) instance.Dispose();
            _instances.Clear();
            foreach (var plugin in _nativePlugins.Values) plugin.Dispose();
            _nativePlugins = new Dictionary<string, NativePlugin>(StringComparer.Ordinal);
            _tileExtensions = new Dictionary<string, TileExtensionPlugin>(StringComparer.Ordinal);
            _managedFactories = new Dictionary<string, IProjectComponentFactory>(StringComparer.Ordinal);
            _disposed = true;
        }
    }

    private static float DegreesToRadians(float degrees) => degrees * MathF.PI / 180.0f;

    private static NumericsVector3 QuaternionToEulerDegrees(RenderQuaternion value)
    {
        var quaternion = new NumericsQuaternion(value.X, value.Y, value.Z, value.W);
        quaternion = quaternion.LengthSquared() > 0.0000001f
            ? NumericsQuaternion.Normalize(quaternion)
            : NumericsQuaternion.Identity;

        var sinXCosY = 2.0f * (quaternion.W * quaternion.X + quaternion.Y * quaternion.Z);
        var cosXCosY = 1.0f - 2.0f * (quaternion.X * quaternion.X + quaternion.Y * quaternion.Y);
        var x = MathF.Atan2(sinXCosY, cosXCosY);

        var sinY = 2.0f * (quaternion.W * quaternion.Y - quaternion.Z * quaternion.X);
        var y = MathF.Abs(sinY) >= 1.0f
            ? MathF.CopySign(MathF.PI / 2.0f, sinY)
            : MathF.Asin(sinY);

        var sinZCosY = 2.0f * (quaternion.W * quaternion.Z + quaternion.X * quaternion.Y);
        var cosZCosY = 1.0f - 2.0f * (quaternion.Y * quaternion.Y + quaternion.Z * quaternion.Z);
        var z = MathF.Atan2(sinZCosY, cosZCosY);
        const float radiansToDegrees = 180.0f / MathF.PI;
        return new NumericsVector3(x, y, z) * radiansToDegrees;
    }

    private sealed record ManagedTransformOrigin(
        NumericsVector3 Position,
        NumericsVector3 Rotation,
        NumericsVector3 Scale)
    {
        public static ManagedTransformOrigin Identity { get; } = new(
            NumericsVector3.Zero,
            NumericsVector3.Zero,
            NumericsVector3.One);

        public static ManagedTransformOrigin FromRenderTransform(RenderTransform transform) => new(
            new NumericsVector3(transform.Position.X, transform.Position.Y, transform.Position.Z),
            QuaternionToEulerDegrees(transform.Rotation),
            new NumericsVector3(transform.Scale.X, transform.Scale.Y, transform.Scale.Z));

        public DragonPixel.Contracts.Transform CreateTransform() => new(Position, Rotation, Scale);
    }

    private void LoadModules(string manifestPath)
    {
        var manifest = ValidateManifest(manifestPath);
        var managedFactories = new Dictionary<string, IProjectComponentFactory>(StringComparer.Ordinal);
        var nativePlugins = new Dictionary<string, NativePlugin>(StringComparer.Ordinal);
        var tileExtensions = new Dictionary<string, TileExtensionPlugin>(StringComparer.Ordinal);
        try
        {
            foreach (var module in manifest.ManagedModules)
            {
                var assemblyBytes = ReadVerifiedArtifactBytes(module.Path, module.Sha256, manifest.CacheRoot);
                var assembly = Assembly.Load(assemblyBytes);
                var expectedTypes = module.Components.Select(component => component.TypeId)
                    .ToHashSet(StringComparer.Ordinal);
                var actualTypes = new HashSet<string>(StringComparer.Ordinal);
                foreach (var type in assembly.GetTypes().Where(type => !type.IsAbstract
                             && typeof(IProjectComponentFactory).IsAssignableFrom(type)))
                {
                    if (Activator.CreateInstance(type) is not IProjectComponentFactory factory)
                    {
                        throw new InvalidDataException(
                            $"Managed component factory {type.FullName} is invalid, duplicated, or unlisted.");
                    }
                    var factoryTypeId = factory.TypeId;
                    if (!IsCanonicalUuidV4(factoryTypeId)
                        || !expectedTypes.Contains(factoryTypeId)
                        || !actualTypes.Add(factoryTypeId)
                        || !managedFactories.TryAdd(factoryTypeId, factory))
                    {
                        throw new InvalidDataException(
                            $"Managed component factory {type.FullName} is invalid, duplicated, or unlisted.");
                    }
                }
                if (!actualTypes.SetEquals(expectedTypes))
                {
                    throw new InvalidDataException(
                        $"Managed module {module.Path} does not expose exactly its declared component set.");
                }
                ValidateArtifactCurrent(module.Path, module.Sha256, manifest.CacheRoot);
            }

            foreach (var module in manifest.NativeModules)
            {
                using var artifactLease = OpenVerifiedArtifact(module.Path, module.Sha256, manifest.CacheRoot);
                var plugin = new NativePlugin(module.Path, module.Component.TypeId);
                if (!nativePlugins.TryAdd(module.Component.TypeId, plugin))
                {
                    plugin.Dispose();
                    throw new InvalidDataException(
                        $"Duplicate native component type ID {module.Component.TypeId}.");
                }
                if (module.TileExtension is { } declaredExtension)
                {
                    var extension = plugin.LoadTileExtension(declaredExtension);
                    if (!tileExtensions.TryAdd(declaredExtension.PluginId, extension))
                    {
                        throw new InvalidDataException(
                            $"Duplicate tile-extension plugin ID {declaredExtension.PluginId}.");
                    }
                }
                ValidateArtifactCurrent(module.Path, module.Sha256, manifest.CacheRoot);
            }

            lock (_gate)
            {
                ObjectDisposedException.ThrowIf(_disposed, this);
                _managedFactories = managedFactories;
                _nativePlugins = nativePlugins;
                _tileExtensions = tileExtensions;
            }
        }
        catch
        {
            foreach (var plugin in nativePlugins.Values)
            {
                try { plugin.Dispose(); }
                catch { /* Preserve the validation/load failure. */ }
            }
            throw;
        }
        AddDiagnostic(ProjectComponentDiagnosticSeverity.Info,
            $"Validated build {manifest.BuildHash} and loaded {manifest.ComponentCount} "
            + $"worker-only project component factory/factories plus {tileExtensions.Count} tile extension(s).");
    }

    private static ValidatedManifest ValidateManifest(string manifestPath)
    {
        var manifestFile = new FileInfo(Path.GetFullPath(manifestPath));
        var cacheRoot = FindCacheRoot(manifestFile);
        ValidateContainedRegularFile(cacheRoot, manifestFile.FullName, "runtime-module manifest");
        using var stream = new FileStream(manifestFile.FullName, FileMode.Open, FileAccess.Read, FileShare.Read);
        using var document = JsonDocument.Parse(stream, new JsonDocumentOptions
        {
            AllowTrailingCommas = false,
            CommentHandling = JsonCommentHandling.Disallow,
            MaxDepth = 64,
        });
        var root = document.RootElement;
        RequireExactProperties(root, "runtime-module manifest",
        [
            "format", "formatVersion", "buildHash", "platform", "architecture",
            "configuration", "generatorIdentity", "contractsSha256", "componentRoots",
            "toolIdentities", "managedModules", "nativeModules",
        ]);
        var formatVersion = RequireInt32(root, "formatVersion", "runtime-module manifest");
        if (RequireString(root, "format", "runtime-module manifest") != "dpe.runtime-modules"
            || formatVersion is not (1 or 2))
        {
            throw new InvalidDataException("Unsupported project runtime-module manifest.");
        }
        var buildHash = RequireSha256(root, "buildHash", "runtime-module manifest");
        var platform = RequireString(root, "platform", "runtime-module manifest");
        var architecture = RequireString(root, "architecture", "runtime-module manifest");
        if (platform != CurrentPlatform || architecture != CurrentArchitecture)
        {
            throw new InvalidDataException(
                $"Runtime-module platform/architecture {platform}/{architecture} does not match {CurrentPlatform}/{CurrentArchitecture}.");
        }
        var expectedBuildDirectory = Path.Combine(
            cacheRoot,
            buildHash,
            $"{platform}-{architecture}");
        ValidateManifestPlacement(manifestFile.FullName, cacheRoot, expectedBuildDirectory);
        var expectedGeneratorIdentity = formatVersion == 1
            ? "dpe-component-generator-v2" : "dpe-component-generator-v3";
        if (RequireString(root, "configuration", "runtime-module manifest") != "Release"
            || RequireString(root, "generatorIdentity", "runtime-module manifest")
                != expectedGeneratorIdentity)
        {
            throw new InvalidDataException("Runtime-module configuration or generator identity is unsupported.");
        }
        var contractsSha256 = RequireSha256(root, "contractsSha256", "runtime-module manifest");
        var loadedContractsPath = typeof(IProjectComponent).Assembly.Location;
        if (string.IsNullOrWhiteSpace(loadedContractsPath)
            || !StringComparer.Ordinal.Equals(ComputeSha256(loadedContractsPath), contractsSha256))
        {
            throw new InvalidDataException(
                "Runtime-module contracts identity does not match the worker's loaded DragonPixel.Contracts assembly.");
        }
        var componentRoots = ParseComponentRoots(root.GetProperty("componentRoots"));
        ValidateToolIdentities(root.GetProperty("toolIdentities"));

        var typeIds = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var moduleIds = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var sourcePaths = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var artifactPaths = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var managedModules = ParseManagedModules(root.GetProperty("managedModules"), cacheRoot,
            expectedBuildDirectory, componentRoots, typeIds, moduleIds, sourcePaths, artifactPaths);
        var nativeModules = ParseNativeModules(root.GetProperty("nativeModules"), formatVersion, cacheRoot,
            expectedBuildDirectory, componentRoots, typeIds, moduleIds, sourcePaths, artifactPaths);
        if (managedModules.Count == 0 && nativeModules.Count == 0)
        {
            throw new InvalidDataException("Runtime-module manifest declares no executable components.");
        }
        ValidateContainedRegularFile(cacheRoot, manifestFile.FullName, "runtime-module manifest");
        return new ValidatedManifest(cacheRoot, buildHash, managedModules, nativeModules);
    }

    private static IReadOnlyList<ValidatedManagedModule> ParseManagedModules(
        JsonElement value,
        string cacheRoot,
        string expectedBuildDirectory,
        IReadOnlyList<string> componentRoots,
        HashSet<string> typeIds,
        HashSet<string> moduleIds,
        HashSet<string> sourcePaths,
        HashSet<string> artifactPaths)
    {
        if (value.ValueKind != JsonValueKind.Array)
            throw new InvalidDataException("managedModules must be an array.");
        var modules = new List<ValidatedManagedModule>();
        foreach (var module in value.EnumerateArray())
        {
            RequireExactProperties(module, "managed module", ["path", "sha256", "components"]);
            var path = ParseArtifact(module, cacheRoot,
                Path.Combine(expectedBuildDirectory, "managed"), ".dll", artifactPaths, "managed module");
            var componentsValue = module.GetProperty("components");
            if (componentsValue.ValueKind != JsonValueKind.Array)
                throw new InvalidDataException("Managed module components must be an array.");
            var components = new List<ValidatedComponent>();
            foreach (var component in componentsValue.EnumerateArray())
            {
                components.Add(ParseComponent(component, ".cs", componentRoots,
                    typeIds, moduleIds, sourcePaths, "managed component"));
            }
            if (components.Count == 0)
                throw new InvalidDataException("A managed module must declare at least one component.");
            modules.Add(new ValidatedManagedModule(path, RequireSha256(module, "sha256", "managed module"), components));
        }
        return modules;
    }

    private static IReadOnlyList<ValidatedNativeModule> ParseNativeModules(
        JsonElement value,
        int formatVersion,
        string cacheRoot,
        string expectedBuildDirectory,
        IReadOnlyList<string> componentRoots,
        HashSet<string> typeIds,
        HashSet<string> moduleIds,
        HashSet<string> sourcePaths,
        HashSet<string> artifactPaths)
    {
        if (value.ValueKind != JsonValueKind.Array)
            throw new InvalidDataException("nativeModules must be an array.");
        var modules = new List<ValidatedNativeModule>();
        var extension = OperatingSystem.IsWindows() ? ".dll"
            : OperatingSystem.IsMacOS() ? ".dylib" : ".so";
        foreach (var module in value.EnumerateArray())
        {
            var hasTileExtension = module.TryGetProperty("tileExtension", out var tileExtensionValue);
            RequireExactProperties(module, "native module", hasTileExtension && formatVersion >= 2
                ? ["path", "sha256", "component", "tileExtension"]
                : ["path", "sha256", "component"]);
            if (hasTileExtension && formatVersion < 2)
                throw new InvalidDataException("Runtime-module manifest v1 cannot declare tile extensions.");
            var path = ParseArtifact(module, cacheRoot,
                Path.Combine(expectedBuildDirectory, "native"), extension, artifactPaths, "native module");
            var component = ParseComponent(module.GetProperty("component"), ".cpp", componentRoots,
                typeIds, moduleIds, sourcePaths, "native component");
            var tileExtension = hasTileExtension
                ? ParseTileExtension(tileExtensionValue) : null;
            modules.Add(new ValidatedNativeModule(path,
                RequireSha256(module, "sha256", "native module"), component, tileExtension));
        }
        return modules;
    }

    private static ValidatedTileExtension ParseTileExtension(JsonElement value)
    {
        RequireExactProperties(value, "tile extension", ["pluginId", "capabilities"]);
        var pluginId = RequireString(value, "pluginId", "tile extension");
        if (!IsStablePluginId(pluginId))
            throw new InvalidDataException("Tile-extension pluginId is not a stable portable identifier.");
        if (!value.TryGetProperty("capabilities", out var capabilitiesValue)
            || capabilitiesValue.ValueKind != JsonValueKind.Array)
            throw new InvalidDataException("Tile-extension capabilities must be an array.");
        uint capabilities = 0;
        var names = new HashSet<string>(StringComparer.Ordinal);
        foreach (var item in capabilitiesValue.EnumerateArray())
        {
            if (item.ValueKind != JsonValueKind.String || item.GetString() is not { } name
                || !names.Add(name))
                throw new InvalidDataException("Tile-extension capabilities contain a malformed duplicate.");
            capabilities |= name switch
            {
                "evaluate-tiles" => TileExtensionPlugin.EvaluateCapability,
                "propose-brush" => TileExtensionPlugin.BrushCapability,
                _ => throw new InvalidDataException($"Unsupported tile-extension capability {name}."),
            };
        }
        if (capabilities == 0)
            throw new InvalidDataException("Tile extension must declare at least one capability.");
        return new ValidatedTileExtension(pluginId, capabilities);
    }

    private static ValidatedComponent ParseComponent(
        JsonElement component,
        string expectedExtension,
        IReadOnlyList<string> componentRoots,
        HashSet<string> typeIds,
        HashSet<string> moduleIds,
        HashSet<string> sourcePaths,
        string context)
    {
        RequireExactProperties(component, context, ["typeId", "moduleId", "sourcePath"]);
        var typeId = RequireString(component, "typeId", context);
        var moduleId = RequireString(component, "moduleId", context);
        var sourcePath = RequireString(component, "sourcePath", context);
        if (!IsCanonicalUuidV4(typeId) || !IsCanonicalUuidV4(moduleId)
            || !typeIds.Add(typeId) || !moduleIds.Add(moduleId))
        {
            throw new InvalidDataException(
                $"{context} has a malformed, duplicate, or case-aliased type/module UUID-v4.");
        }
        ValidatePortableSourcePath(sourcePath, expectedExtension, componentRoots, context);
        if (!sourcePaths.Add(sourcePath))
            throw new InvalidDataException($"{context} sourcePath is duplicated or case-aliased.");
        return new ValidatedComponent(typeId, moduleId, sourcePath);
    }

    private static string ParseArtifact(
        JsonElement module,
        string cacheRoot,
        string expectedArtifactDirectory,
        string expectedExtension,
        HashSet<string> artifactPaths,
        string context)
    {
        var declaredPath = RequireString(module, "path", context);
        if (!Path.IsPathFullyQualified(declaredPath))
            throw new InvalidDataException($"{context} artifact path must be absolute.");
        var path = Path.GetFullPath(declaredPath);
        if (!StringComparer.OrdinalIgnoreCase.Equals(Path.GetExtension(path), expectedExtension)
            || !artifactPaths.Add(path))
        {
            throw new InvalidDataException($"{context} artifact path has an invalid extension or duplicate alias.");
        }
        var expectedHash = RequireSha256(module, "sha256", context);
        ValidateArtifactCurrent(path, expectedHash, cacheRoot);
        ValidatePathContained(expectedArtifactDirectory, path, context);
        return path;
    }

    private static IReadOnlyList<string> ParseComponentRoots(JsonElement value)
    {
        if (value.ValueKind != JsonValueKind.Array)
            throw new InvalidDataException("componentRoots must be an array.");
        var roots = new List<string>();
        var aliases = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var item in value.EnumerateArray())
        {
            if (item.ValueKind != JsonValueKind.String)
                throw new InvalidDataException("componentRoots entries must be strings.");
            var root = item.GetString() ?? string.Empty;
            if (!IsPortableRelativePath(root, allowCurrentDirectory: true) || !aliases.Add(root))
                throw new InvalidDataException("componentRoots contains an unsafe, empty, duplicate, or case-aliased path.");
            roots.Add(root);
        }
        if (roots.Count == 0)
            throw new InvalidDataException("componentRoots must contain at least one declared root.");
        return roots;
    }

    private static void ValidateToolIdentities(JsonElement value)
    {
        RequireExactProperties(value, "toolIdentities", ["dotnet", "cmake", "cxx"]);
        _ = RequireSha256(value, "dotnet", "toolIdentities");
        _ = RequireSha256(value, "cmake", "toolIdentities");
        _ = RequireSha256(value, "cxx", "toolIdentities");
    }

    private static void ValidateManifestPlacement(
        string manifestPath,
        string cacheRoot,
        string expectedBuildDirectory)
    {
        var activeManifest = Path.Combine(cacheRoot, "active-runtime-modules.json");
        var buildManifest = Path.Combine(expectedBuildDirectory, "runtime-modules.json");
        if (!PathNameComparer.Equals(Path.GetFullPath(manifestPath), Path.GetFullPath(activeManifest))
            && !PathNameComparer.Equals(Path.GetFullPath(manifestPath), Path.GetFullPath(buildManifest)))
        {
            throw new InvalidDataException(
                "Runtime-module manifest placement does not match its declared buildHash/platform/architecture.");
        }
    }

    private static void ValidatePathContained(string expectedRoot, string path, string context)
    {
        var relative = Path.GetRelativePath(Path.GetFullPath(expectedRoot), Path.GetFullPath(path));
        if (relative == "." || Path.IsPathFullyQualified(relative) || relative == ".."
            || relative.StartsWith(".." + Path.DirectorySeparatorChar, StringComparison.Ordinal)
            || relative.StartsWith(".." + Path.AltDirectorySeparatorChar, StringComparison.Ordinal))
        {
            throw new InvalidDataException(
                $"{context} artifact placement does not match the manifest buildHash/platform/architecture: {path}");
        }
    }

    private static void ValidatePortableSourcePath(
        string sourcePath,
        string expectedExtension,
        IReadOnlyList<string> componentRoots,
        string context)
    {
        if (!IsPortableRelativePath(sourcePath, allowCurrentDirectory: false)
            || !StringComparer.OrdinalIgnoreCase.Equals(Path.GetExtension(sourcePath), expectedExtension))
        {
            throw new InvalidDataException($"{context} sourcePath is unsafe or has the wrong language extension.");
        }
        var contained = componentRoots.Any(root => root == "."
            || sourcePath.Equals(root, StringComparison.Ordinal)
            || sourcePath.StartsWith(root + "/", StringComparison.Ordinal));
        if (!contained)
            throw new InvalidDataException($"{context} sourcePath is outside every declared component root.");
    }

    private static bool IsPortableRelativePath(string value, bool allowCurrentDirectory)
    {
        if (string.IsNullOrWhiteSpace(value) || value.Contains('\\') || Path.IsPathFullyQualified(value))
            return false;
        if (allowCurrentDirectory && value == ".") return true;
        var parts = value.Split('/');
        return parts.Length > 0 && parts.All(part => part.Length > 0 && part is not "." and not "..");
    }

    private static string FindCacheRoot(FileInfo manifest)
    {
        for (var directory = manifest.Directory; directory is not null; directory = directory.Parent)
        {
            if (PathNameComparer.Equals(directory.Name, "Cache")
                && directory.Parent is not null
                && PathNameComparer.Equals(directory.Parent.Name, ".dragonpixel"))
            {
                ValidateDirectoryNoReparse(directory.Parent.FullName, ".dragonpixel directory");
                ValidateDirectoryNoReparse(directory.FullName, "component cache root");
                return directory.FullName;
            }
        }
        throw new InvalidDataException(
            "Runtime-module manifest must be physically contained below a .dragonpixel/Cache directory.");
    }

    private static void ValidateArtifactCurrent(string path, string expectedSha256, string cacheRoot)
    {
        ValidateContainedRegularFile(cacheRoot, path, "runtime-module artifact");
        var actualSha256 = ComputeSha256(path);
        ValidateContainedRegularFile(cacheRoot, path, "runtime-module artifact");
        if (!StringComparer.Ordinal.Equals(actualSha256, expectedSha256))
            throw new InvalidDataException($"Runtime-module artifact SHA-256 mismatch: {path}");
    }

    private static FileStream OpenVerifiedArtifact(string path, string expectedSha256, string cacheRoot)
    {
        ValidateContainedRegularFile(cacheRoot, path, "runtime-module artifact");
        var stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read);
        try
        {
            var actualSha256 = Convert.ToHexString(SHA256.HashData(stream)).ToLowerInvariant();
            if (!StringComparer.Ordinal.Equals(actualSha256, expectedSha256))
                throw new InvalidDataException($"Runtime-module artifact SHA-256 mismatch: {path}");
            ValidateContainedRegularFile(cacheRoot, path, "runtime-module artifact");
            return stream;
        }
        catch
        {
            stream.Dispose();
            throw;
        }
    }

    private static byte[] ReadVerifiedArtifactBytes(string path, string expectedSha256, string cacheRoot)
    {
        ValidateContainedRegularFile(cacheRoot, path, "runtime-module artifact");
        var bytes = File.ReadAllBytes(path);
        var actualSha256 = Convert.ToHexString(SHA256.HashData(bytes)).ToLowerInvariant();
        ValidateContainedRegularFile(cacheRoot, path, "runtime-module artifact");
        if (!StringComparer.Ordinal.Equals(actualSha256, expectedSha256))
            throw new InvalidDataException($"Runtime-module artifact SHA-256 mismatch: {path}");
        return bytes;
    }

    private static void ValidateContainedRegularFile(string cacheRoot, string path, string context)
    {
        var fullCacheRoot = Path.GetFullPath(cacheRoot);
        var fullPath = Path.GetFullPath(path);
        var relative = Path.GetRelativePath(fullCacheRoot, fullPath);
        if (relative == "." || Path.IsPathFullyQualified(relative) || relative == ".."
            || relative.StartsWith(".." + Path.DirectorySeparatorChar, StringComparison.Ordinal)
            || relative.StartsWith(".." + Path.AltDirectorySeparatorChar, StringComparison.Ordinal))
        {
            throw new InvalidDataException($"{context} escapes the component cache root: {path}");
        }
        var current = fullCacheRoot;
        var parts = relative.Split([Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar],
            StringSplitOptions.RemoveEmptyEntries);
        for (var index = 0; index < parts.Length; ++index)
        {
            current = Path.Combine(current, parts[index]);
            var attributes = File.GetAttributes(current);
            if ((attributes & FileAttributes.ReparsePoint) != 0
                || (index + 1 < parts.Length && (attributes & FileAttributes.Directory) == 0)
                || (index + 1 == parts.Length && (attributes & FileAttributes.Directory) != 0))
            {
                throw new InvalidDataException($"{context} contains a link/reparse point or is not a regular file: {current}");
            }
        }
    }

    private static void ValidateDirectoryNoReparse(string path, string context)
    {
        var attributes = File.GetAttributes(path);
        if ((attributes & FileAttributes.Directory) == 0
            || (attributes & FileAttributes.ReparsePoint) != 0)
        {
            throw new InvalidDataException($"{context} is missing or is a link/reparse point: {path}");
        }
    }

    private static string ComputeSha256(string path)
    {
        using var stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read);
        return Convert.ToHexString(SHA256.HashData(stream)).ToLowerInvariant();
    }

    private static bool IsCanonicalUuidV4(string value)
    {
        return value.Length == 36 && value[14] == '4' && value[19] is '8' or '9' or 'a' or 'b'
            && Guid.TryParseExact(value, "D", out var parsed)
            && StringComparer.Ordinal.Equals(parsed.ToString("D"), value);
    }

    private static bool IsCanonicalSha256(string value) =>
        value.Length == 64 && value.All(character =>
            (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f'));

    private static bool IsStablePluginId(string value) => value.Length is >= 3 and <= 128
        && value[0] is >= 'a' and <= 'z'
        && value[^1] is not '.' and not '-'
        && value.All(character => character is >= 'a' and <= 'z'
            || character is >= '0' and <= '9' || character is '.' or '-');

    private static string RequireSha256(JsonElement value, string propertyName, string context)
    {
        var result = RequireString(value, propertyName, context);
        if (!IsCanonicalSha256(result))
            throw new InvalidDataException($"{context}.{propertyName} must be a canonical lowercase SHA-256.");
        return result;
    }

    private static string RequireString(JsonElement value, string propertyName, string context)
    {
        if (!value.TryGetProperty(propertyName, out var property)
            || property.ValueKind != JsonValueKind.String
            || property.GetString() is not { } result)
        {
            throw new InvalidDataException($"{context}.{propertyName} must be a string.");
        }
        return result;
    }

    private static int RequireInt32(JsonElement value, string propertyName, string context)
    {
        if (!value.TryGetProperty(propertyName, out var property)
            || property.ValueKind != JsonValueKind.Number || !property.TryGetInt32(out var result))
        {
            throw new InvalidDataException($"{context}.{propertyName} must be an integer.");
        }
        return result;
    }

    private static void RequireExactProperties(JsonElement value, string context, IReadOnlyCollection<string> expected)
    {
        if (value.ValueKind != JsonValueKind.Object)
            throw new InvalidDataException($"{context} must be an object.");
        var actual = new HashSet<string>(StringComparer.Ordinal);
        foreach (var property in value.EnumerateObject())
        {
            if (!actual.Add(property.Name))
                throw new InvalidDataException($"{context} contains duplicate JSON property {property.Name}.");
        }
        if (!actual.SetEquals(expected))
            throw new InvalidDataException($"{context} contains missing or unsupported properties.");
    }

    private static string CurrentPlatform => OperatingSystem.IsWindows() ? "windows"
        : OperatingSystem.IsMacOS() ? "macos" : "linux";

    private static string CurrentArchitecture => RuntimeInformation.ProcessArchitecture switch
    {
        Architecture.X64 => "x64",
        Architecture.Arm64 => "arm64",
        _ => RuntimeInformation.ProcessArchitecture.ToString().ToLowerInvariant(),
    };

    private static StringComparer PathNameComparer => OperatingSystem.IsWindows()
        ? StringComparer.OrdinalIgnoreCase : StringComparer.Ordinal;

    private sealed record ValidatedComponent(string TypeId, string ModuleId, string SourcePath);
    private sealed record ValidatedManagedModule(
        string Path, string Sha256, IReadOnlyList<ValidatedComponent> Components);
    private sealed record ValidatedNativeModule(
        string Path, string Sha256, ValidatedComponent Component, ValidatedTileExtension? TileExtension);
    private sealed record ValidatedTileExtension(string PluginId, uint Capabilities);
    private sealed record ValidatedManifest(
        string CacheRoot,
        string BuildHash,
        IReadOnlyList<ValidatedManagedModule> ManagedModules,
        IReadOnlyList<ValidatedNativeModule> NativeModules)
    {
        public int ComponentCount => ManagedModules.Sum(module => module.Components.Count) + NativeModules.Count;
    }

    private void AddDiagnostic(ProjectComponentDiagnosticSeverity severity, string message)
    {
        lock (_gate)
        {
            AddDiagnosticLocked($"[{severity}] {message}");
        }
    }

    private void AddDiagnosticLocked(string message)
    {
        _diagnostics.Add(message);
        if (_diagnostics.Count > 256) _diagnostics.RemoveRange(0, _diagnostics.Count - 256);
    }

    private interface IRuntimeInstance : IDisposable
    {
        void FixedUpdate(ProjectComponentUpdate update);
        void Update(ProjectComponentUpdate update);
        void LateUpdate(ProjectComponentUpdate update);
        void SubmitRender(ProjectComponentUpdate update);
    }

    private sealed class ManagedInstance : IRuntimeInstance
    {
        private readonly IProjectComponent _component;
        private readonly IProjectComponentLifecycle? _lifecycle;
        private readonly Action<ProjectComponentDiagnosticSeverity, string> _diagnostic;
        private readonly string _typeId;
        private bool _enabled;
        private bool _failed;
        private bool _disposed;

        public ManagedInstance(
            IProjectComponentFactory factory,
            string entityId,
            string typeId,
            string properties,
            DragonPixel.Contracts.Transform transform,
            Action<ProjectComponentDiagnosticSeverity, string> diagnostic)
        {
            _diagnostic = diagnostic;
            _typeId = typeId;
            _component = factory.Create() ?? throw new InvalidDataException($"Factory {typeId} returned null.");
            _lifecycle = _component as IProjectComponentLifecycle;
            try
            {
                _component.Initialize(new ProjectComponentContext(entityId, typeId, diagnostic, transform), properties);
                if (_lifecycle is not null)
                {
                    _lifecycle.OnEnable();
                    _enabled = true;
                }
            }
            catch
            {
                DisableBestEffort("failed initialization");
                try
                {
                    _component.Shutdown();
                }
                catch (Exception shutdownException)
                {
                    diagnostic(ProjectComponentDiagnosticSeverity.Error,
                        $"Managed component shutdown after failed initialization also failed: {shutdownException.Message}");
                }
                throw;
            }
        }

        public void FixedUpdate(ProjectComponentUpdate update) =>
            Invoke("fixed update", () => _lifecycle?.FixedUpdate(update));

        public void Update(ProjectComponentUpdate update) =>
            Invoke("variable update", () => _component.Update(update));

        public void LateUpdate(ProjectComponentUpdate update) =>
            Invoke("late update", () => _lifecycle?.LateUpdate(update));

        public void SubmitRender(ProjectComponentUpdate update) =>
            Invoke("render submission", () => _lifecycle?.SubmitRender(update));

        private void Invoke(string stage, Action action)
        {
            if (_failed || _disposed) return;
            try
            {
                action();
            }
            catch (Exception exception)
            {
                _failed = true;
                _diagnostic(ProjectComponentDiagnosticSeverity.Error,
                    $"Managed component {_typeId} {stage} failed and the instance was disabled: {exception}");
                DisableBestEffort(stage);
            }
        }

        private void DisableBestEffort(string reason)
        {
            if (!_enabled || _lifecycle is null) return;
            _enabled = false;
            try
            {
                _lifecycle.OnDisable();
            }
            catch (Exception exception)
            {
                _diagnostic(ProjectComponentDiagnosticSeverity.Error,
                    $"Managed component {_typeId} disable after {reason} failed: {exception.Message}");
            }
        }

        public void Dispose()
        {
            if (_disposed) return;
            _disposed = true;
            DisableBestEffort("disposal");
            try { _component.Shutdown(); }
            catch (Exception exception)
            {
                _diagnostic(ProjectComponentDiagnosticSeverity.Error,
                    $"Managed component shutdown failed: {exception.Message}");
            }
        }
    }

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate IntPtr GetApiDelegate();
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate IntPtr CreateDelegate(IntPtr entityId, NativeDiagnosticDelegate diagnostic, IntPtr user);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate void DestroyDelegate(IntPtr handle);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate int SetPropertiesDelegate(IntPtr handle, IntPtr json, nuint length);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate int UpdateDelegate(IntPtr handle, ref NativeUpdate update);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate int DispatchLifecycleDelegate(IntPtr handle, int phase, ref NativeUpdate update);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate IntPtr LastErrorDelegate(IntPtr handle);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate void NativeDiagnosticDelegate(IntPtr user, int severity, IntPtr message, nuint length);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate int EvaluateTilesDelegate(
        [In] NativeTileExtensionContext[] contexts,
        nuint contextCount,
        [In, Out] NativeTileExtensionResult[] results,
        nuint resultCount);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate int ProposeBrushDelegate(
        ref NativeTileExtensionContext context,
        NativeTileExtensionString request,
        out NativeTileExtensionResult result);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate void ReleaseTileExtensionStringDelegate(NativeTileExtensionString value);

    [StructLayout(LayoutKind.Sequential)]
    private struct NativeApi
    {
        public uint AbiVersion;
        public IntPtr ComponentTypeId;
        public IntPtr Create;
        public IntPtr Destroy;
        public IntPtr SetProperties;
        public IntPtr Update;
        public IntPtr LastError;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct NativeApiV2
    {
        public uint AbiVersion;
        public uint StructSize;
        public IntPtr ComponentTypeId;
        public IntPtr Create;
        public IntPtr Destroy;
        public IntPtr SetProperties;
        public IntPtr DispatchLifecycle;
        public IntPtr LastError;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct NativeTileExtensionString
    {
        public IntPtr Data;
        public nuint Length;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct NativeTileExtensionContext
    {
        public uint StructSize;
        public int CellX;
        public int CellY;
        public int Elevation;
        public uint Layout;
        public ulong DeterministicSeed;
        public double ElapsedSeconds;
        public NativeTileExtensionString MapId;
        public NativeTileExtensionString LayerId;
        public NativeTileExtensionString TileSetId;
        public NativeTileExtensionString TileId;
        public NativeTileExtensionString PayloadJson;
        public NativeTileExtensionString NeighborhoodJson;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct NativeTileExtensionResult
    {
        public uint StructSize;
        public int Status;
        public NativeTileExtensionString ResultJson;
        public NativeTileExtensionString ErrorCode;
        public NativeTileExtensionString ErrorMessage;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct NativeTileExtensionApi
    {
        public uint AbiVersion;
        public uint StructSize;
        public uint Capabilities;
        public uint Reserved;
        public NativeTileExtensionString PluginId;
        public IntPtr EvaluateTiles;
        public IntPtr ProposeBrush;
        public IntPtr ReleaseString;
    }

    private enum NativeLifecyclePhase
    {
        Enable = 1,
        FixedUpdate = 2,
        VariableUpdate = 3,
        LateUpdate = 4,
        RenderSubmission = 5,
        Disable = 6,
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct NativeUpdate
    {
        public double ElapsedSeconds;
        public double DeltaSeconds;
        public IntPtr InputJson;
        public nuint InputJsonLength;
    }

    private sealed class NativePlugin : IDisposable
    {
        private readonly IntPtr _library;
        private readonly CreateDelegate _create;
        private readonly DestroyDelegate _destroy;
        private readonly SetPropertiesDelegate _setProperties;
        private readonly UpdateDelegate? _update;
        private readonly DispatchLifecycleDelegate? _dispatchLifecycle;
        private readonly LastErrorDelegate _lastError;

        public bool IsV2 { get; }

        public NativePlugin(string path, string expectedType)
        {
            _library = NativeLibrary.Load(path);
            try
            {
                if (NativeLibrary.TryGetExport(
                        _library,
                        "dpe_component_plugin_get_v2",
                        out var getApiV2Pointer))
                {
                    var getApiV2 = Marshal.GetDelegateForFunctionPointer<GetApiDelegate>(getApiV2Pointer);
                    var pointer = getApiV2();
                    if (pointer == IntPtr.Zero)
                        throw new InvalidDataException("Native component v2 API pointer is null.");
                    var api = Marshal.PtrToStructure<NativeApiV2>(pointer);
                    var actualType = Marshal.PtrToStringUTF8(api.ComponentTypeId);
                    if (api.AbiVersion != 2
                        || api.StructSize < checked((uint)Marshal.SizeOf<NativeApiV2>())
                        || actualType is null
                        || !IsCanonicalUuidV4(actualType)
                        || !StringComparer.Ordinal.Equals(actualType, expectedType))
                    {
                        throw new InvalidDataException(
                            "Native component v2 ABI, size, or stable type ID does not match its manifest.");
                    }
                    _create = Marshal.GetDelegateForFunctionPointer<CreateDelegate>(api.Create);
                    _destroy = Marshal.GetDelegateForFunctionPointer<DestroyDelegate>(api.Destroy);
                    _setProperties = Marshal.GetDelegateForFunctionPointer<SetPropertiesDelegate>(api.SetProperties);
                    _dispatchLifecycle = Marshal.GetDelegateForFunctionPointer<DispatchLifecycleDelegate>(
                        api.DispatchLifecycle);
                    _lastError = Marshal.GetDelegateForFunctionPointer<LastErrorDelegate>(api.LastError);
                    IsV2 = true;
                }
                else
                {
                    var getApi = Marshal.GetDelegateForFunctionPointer<GetApiDelegate>(
                        NativeLibrary.GetExport(_library, "dpe_component_plugin_get_v1"));
                    var pointer = getApi();
                    if (pointer == IntPtr.Zero)
                        throw new InvalidDataException("Native component v1 API pointer is null.");
                    var api = Marshal.PtrToStructure<NativeApi>(pointer);
                    var actualType = Marshal.PtrToStringUTF8(api.ComponentTypeId);
                    if (api.AbiVersion != 1
                        || actualType is null
                        || !IsCanonicalUuidV4(actualType)
                        || !StringComparer.Ordinal.Equals(actualType, expectedType))
                    {
                        throw new InvalidDataException(
                            "Native component v1 ABI or stable type ID does not match its manifest.");
                    }
                    _create = Marshal.GetDelegateForFunctionPointer<CreateDelegate>(api.Create);
                    _destroy = Marshal.GetDelegateForFunctionPointer<DestroyDelegate>(api.Destroy);
                    _setProperties = Marshal.GetDelegateForFunctionPointer<SetPropertiesDelegate>(api.SetProperties);
                    _update = Marshal.GetDelegateForFunctionPointer<UpdateDelegate>(api.Update);
                    _lastError = Marshal.GetDelegateForFunctionPointer<LastErrorDelegate>(api.LastError);
                }
            }
            catch
            {
                NativeLibrary.Free(_library);
                throw;
            }
        }

        public NativeInstance Create(
            string entityId,
            string properties,
            Action<ProjectComponentDiagnosticSeverity, string> diagnostic) =>
            new(this, entityId, properties, diagnostic);

        public TileExtensionPlugin LoadTileExtension(ValidatedTileExtension expected)
        {
            if (!NativeLibrary.TryGetExport(
                    _library, "dpe_tile_extension_plugin_get_v1", out var getApiPointer))
                throw new InvalidDataException(
                    $"Native module declared tile extension {expected.PluginId} but exports no v1 API.");
            var getApi = Marshal.GetDelegateForFunctionPointer<GetApiDelegate>(getApiPointer);
            var pointer = getApi();
            if (pointer == IntPtr.Zero)
                throw new InvalidDataException("Tile-extension v1 API pointer is null.");
            var api = Marshal.PtrToStructure<NativeTileExtensionApi>(pointer);
            var pluginId = ReadNativeString(api.PluginId, 128, "tile-extension plugin ID");
            if (api.AbiVersion != 1
                || api.StructSize < checked((uint)Marshal.SizeOf<NativeTileExtensionApi>())
                || api.Reserved != 0
                || !StringComparer.Ordinal.Equals(pluginId, expected.PluginId)
                || api.Capabilities != expected.Capabilities
                || api.ReleaseString == IntPtr.Zero)
            {
                throw new InvalidDataException(
                    "Tile-extension ABI, size, identity, capabilities, or reserved fields do not match its manifest.");
            }
            return new TileExtensionPlugin(api, pluginId);
        }

        public void Dispose() => NativeLibrary.Free(_library);

        public IntPtr CreateHandle(IntPtr entity, NativeDiagnosticDelegate diagnostic) => _create(entity, diagnostic, IntPtr.Zero);
        public void DestroyHandle(IntPtr handle) => _destroy(handle);
        public int SetProperties(IntPtr handle, IntPtr json, nuint length) => _setProperties(handle, json, length);
        public int Dispatch(
            IntPtr handle,
            NativeLifecyclePhase phase,
            ref NativeUpdate update)
        {
            if (_dispatchLifecycle is not null)
                return _dispatchLifecycle(handle, (int)phase, ref update);
            return phase == NativeLifecyclePhase.VariableUpdate
                ? _update!(handle, ref update)
                : 0;
        }
        public string LastError(IntPtr handle) => Marshal.PtrToStringUTF8(_lastError(handle)) ?? "unknown native component error";
    }

    private sealed class TileExtensionPlugin
    {
        public const uint EvaluateCapability = 1U << 0;
        public const uint BrushCapability = 1U << 1;
        private const int MaximumBatch = 65_536;
        private const int MaximumCommands = 1_048_576;
        private const int MaximumInputBytes = 1024 * 1024;
        private const int MaximumResultBytes = 1024 * 1024;

        private readonly string _pluginId;
        private readonly uint _capabilities;
        private readonly EvaluateTilesDelegate? _evaluate;
        private readonly ProposeBrushDelegate? _proposeBrush;
        private readonly ReleaseTileExtensionStringDelegate _release;

        public TileExtensionPlugin(NativeTileExtensionApi api, string pluginId)
        {
            _pluginId = pluginId;
            _capabilities = api.Capabilities;
            if ((_capabilities & EvaluateCapability) != 0)
            {
                if (api.EvaluateTiles == IntPtr.Zero)
                    throw new InvalidDataException("Tile extension declared evaluation without an entry point.");
                _evaluate = Marshal.GetDelegateForFunctionPointer<EvaluateTilesDelegate>(api.EvaluateTiles);
            }
            if ((_capabilities & BrushCapability) != 0)
            {
                if (api.ProposeBrush == IntPtr.Zero)
                    throw new InvalidDataException("Tile extension declared brush proposals without an entry point.");
                _proposeBrush = Marshal.GetDelegateForFunctionPointer<ProposeBrushDelegate>(api.ProposeBrush);
            }
            _release = Marshal.GetDelegateForFunctionPointer<ReleaseTileExtensionStringDelegate>(api.ReleaseString);
        }

        public IReadOnlyList<TileExtensionEvaluationResult> Evaluate(
            IReadOnlyList<TileExtensionContext> contexts)
        {
            if (_evaluate is null)
                return contexts.Select(_ => Failure("DPE-TILE-EXT-CAPABILITY-DENIED",
                    $"Tile extension {_pluginId} does not declare tile evaluation.")).ToArray();
            if (contexts.Count == 0 || contexts.Count > MaximumBatch)
                throw new InvalidDataException($"Tile-extension batch must contain 1..{MaximumBatch} contexts.");
            var leases = new List<NativeUtf8Lease>(contexts.Count * 6);
            var nativeContexts = new NativeTileExtensionContext[contexts.Count];
            var nativeResults = new NativeTileExtensionResult[contexts.Count];
            try
            {
                for (var index = 0; index < contexts.Count; ++index)
                    nativeContexts[index] = ToNativeContext(contexts[index], leases);
                for (var index = 0; index < nativeResults.Length; ++index)
                    nativeResults[index].StructSize = checked((uint)Marshal.SizeOf<NativeTileExtensionResult>());
                var callStatus = _evaluate(nativeContexts, checked((nuint)nativeContexts.Length),
                    nativeResults, checked((nuint)nativeResults.Length));
                if (callStatus != 0)
                    throw new InvalidDataException($"Tile-extension evaluation call returned status {callStatus}.");
                var results = new TileExtensionEvaluationResult[nativeResults.Length];
                for (var index = 0; index < nativeResults.Length; ++index)
                    results[index] = ConsumeEvaluation(nativeResults[index]);
                return results;
            }
            finally
            {
                foreach (var result in nativeResults) ReleaseResult(result);
                foreach (var lease in leases) lease.Dispose();
            }
        }

        public TileExtensionBrushProposal ProposeBrush(
            TileExtensionContext context,
            string requestJson)
        {
            if (_proposeBrush is null)
                return new TileExtensionBrushProposal(false, string.Empty, 0,
                    "DPE-TILE-EXT-CAPABILITY-DENIED",
                    $"Tile extension {_pluginId} does not declare brush proposals.");
            ValidateJsonInput(requestJson, "brush request");
            var leases = new List<NativeUtf8Lease>(7);
            var result = new NativeTileExtensionResult
            {
                StructSize = checked((uint)Marshal.SizeOf<NativeTileExtensionResult>()),
            };
            try
            {
                var nativeContext = ToNativeContext(context, leases);
                var request = AddLease(requestJson, leases, "brush request");
                var status = _proposeBrush(ref nativeContext, request, out result);
                if (status != 0)
                    throw new InvalidDataException($"Tile-extension brush call returned status {status}.");
                if (result.StructSize < Marshal.SizeOf<NativeTileExtensionResult>())
                    throw new InvalidDataException("Tile-extension brush result has an invalid size tag.");
                if (result.Status != 0)
                {
                    return new TileExtensionBrushProposal(false, string.Empty, 0,
                        ReadResultString(result.ErrorCode, 256, "error code"),
                        ReadResultString(result.ErrorMessage, 4096, "error message"));
                }
                var json = ReadResultString(result.ResultJson, MaximumResultBytes, "brush result");
                if (!TryValidateBrushProposal(json, out var count))
                    return new TileExtensionBrushProposal(false, string.Empty, 0,
                        "DPE-TILE-EXT-MALFORMED-PROPOSAL",
                        "The tile extension returned an invalid or unbounded brush proposal.");
                return new TileExtensionBrushProposal(true, json, count, string.Empty, string.Empty);
            }
            finally
            {
                ReleaseResult(result);
                foreach (var lease in leases) lease.Dispose();
            }
        }

        private TileExtensionEvaluationResult ConsumeEvaluation(NativeTileExtensionResult result)
        {
            if (result.StructSize < Marshal.SizeOf<NativeTileExtensionResult>())
                return Failure("DPE-TILE-EXT-MALFORMED-RESULT", "The extension result size tag is invalid.");
            if (result.Status != 0)
            {
                return Failure(ReadResultString(result.ErrorCode, 256, "error code"),
                    ReadResultString(result.ErrorMessage, 4096, "error message"));
            }
            var json = ReadResultString(result.ResultJson, MaximumResultBytes, "evaluation result");
            if (!TryValidateEvaluation(json))
                return Failure("DPE-TILE-EXT-MALFORMED-RESULT",
                    "The tile extension returned malformed evaluation JSON.");
            return new TileExtensionEvaluationResult(true, json, string.Empty, string.Empty);
        }

        private static TileExtensionEvaluationResult Failure(string code, string message) =>
            new(false, string.Empty, code, message);

        private NativeTileExtensionContext ToNativeContext(
            TileExtensionContext context,
            List<NativeUtf8Lease> leases)
        {
            ValidateJsonInput(context.PayloadJson, "custom payload");
            ValidateJsonInput(context.NeighborhoodJson, "tile neighborhood");
            return new NativeTileExtensionContext
            {
                StructSize = checked((uint)Marshal.SizeOf<NativeTileExtensionContext>()),
                CellX = context.CellX,
                CellY = context.CellY,
                Elevation = context.Elevation,
                Layout = context.Layout,
                DeterministicSeed = context.DeterministicSeed,
                ElapsedSeconds = context.ElapsedSeconds,
                MapId = AddLease(context.MapId, leases, "map ID"),
                LayerId = AddLease(context.LayerId, leases, "layer ID"),
                TileSetId = AddLease(context.TileSetId, leases, "TileSet ID"),
                TileId = AddLease(context.TileId, leases, "tile ID"),
                PayloadJson = AddLease(context.PayloadJson, leases, "custom payload"),
                NeighborhoodJson = AddLease(context.NeighborhoodJson, leases, "tile neighborhood"),
            };
        }

        private static NativeTileExtensionString AddLease(
            string value,
            List<NativeUtf8Lease> leases,
            string context)
        {
            var byteCount = Encoding.UTF8.GetByteCount(value);
            if (byteCount > MaximumInputBytes)
                throw new InvalidDataException($"Tile-extension {context} exceeds {MaximumInputBytes} UTF-8 bytes.");
            var lease = new NativeUtf8Lease(value, byteCount);
            leases.Add(lease);
            return lease.Value;
        }

        private static void ValidateJsonInput(string value, string context)
        {
            if (Encoding.UTF8.GetByteCount(value) > MaximumInputBytes)
                throw new InvalidDataException($"Tile-extension {context} exceeds the input limit.");
            try { using var _ = JsonDocument.Parse(value, JsonOptions); }
            catch (JsonException exception)
            {
                throw new InvalidDataException($"Tile-extension {context} is malformed JSON.", exception);
            }
        }

        private static bool TryValidateEvaluation(string json)
        {
            try
            {
                using var document = JsonDocument.Parse(json, JsonOptions);
                var root = document.RootElement;
                if (root.ValueKind != JsonValueKind.Object) return false;
                var allowed = new HashSet<string>(
                    ["tileSetId", "tileId", "sprite", "tint", "flipX", "flipY",
                     "rotationDegrees", "scale", "offset", "elevation"], StringComparer.Ordinal);
                foreach (var property in root.EnumerateObject())
                    if (!allowed.Remove(property.Name)) return false;
                if (root.TryGetProperty("tileSetId", out var setId)
                    && (setId.ValueKind != JsonValueKind.String
                        || !IsCanonicalUuidV4(setId.GetString() ?? string.Empty))) return false;
                if (root.TryGetProperty("tileId", out var tileId)
                    && (tileId.ValueKind != JsonValueKind.String
                        || !IsCanonicalUuidV4(tileId.GetString() ?? string.Empty))) return false;
                return true;
            }
            catch (JsonException) { return false; }
        }

        private static bool TryValidateBrushProposal(string json, out int commandCount)
        {
            commandCount = 0;
            try
            {
                using var document = JsonDocument.Parse(json, JsonOptions);
                var root = document.RootElement;
                if (root.ValueKind != JsonValueKind.Object
                    || root.EnumerateObject().Any(property => property.Name != "commands")
                    || !root.TryGetProperty("commands", out var commands)
                    || commands.ValueKind != JsonValueKind.Array) return false;
                foreach (var command in commands.EnumerateArray())
                {
                    if (++commandCount > MaximumCommands || command.ValueKind != JsonValueKind.Object) return false;
                    var properties = command.EnumerateObject().Select(property => property.Name)
                        .ToHashSet(StringComparer.Ordinal);
                    if (!properties.SetEquals(["kind", "x", "y", "tileSetId", "tileId"])
                        || command.GetProperty("kind").GetString() != "paint"
                        || !command.GetProperty("x").TryGetInt32(out _)
                        || !command.GetProperty("y").TryGetInt32(out _)
                        || !IsCanonicalUuidV4(command.GetProperty("tileSetId").GetString() ?? string.Empty)
                        || !IsCanonicalUuidV4(command.GetProperty("tileId").GetString() ?? string.Empty)) return false;
                }
                return true;
            }
            catch (Exception exception) when (exception is JsonException or InvalidOperationException)
            {
                return false;
            }
        }

        private string ReadResultString(NativeTileExtensionString value, int maximumBytes, string context)
        {
            if (value.Length > checked((nuint)maximumBytes)
                || (value.Length != 0 && value.Data == IntPtr.Zero))
                throw new InvalidDataException($"Tile-extension {context} is missing or exceeds its bound.");
            return value.Length == 0 ? string.Empty
                : Marshal.PtrToStringUTF8(value.Data, checked((int)value.Length)) ?? string.Empty;
        }

        private void ReleaseResult(NativeTileExtensionResult result)
        {
            foreach (var value in new[] { result.ResultJson, result.ErrorCode, result.ErrorMessage })
                if (value.Data != IntPtr.Zero) _release(value);
        }

        private static readonly JsonDocumentOptions JsonOptions = new()
        {
            AllowTrailingCommas = false,
            CommentHandling = JsonCommentHandling.Disallow,
            MaxDepth = 64,
        };
    }

    private sealed class NativeUtf8Lease : IDisposable
    {
        private IntPtr _pointer;
        public NativeTileExtensionString Value { get; }

        public NativeUtf8Lease(string value, int byteCount)
        {
            _pointer = Marshal.StringToCoTaskMemUTF8(value);
            Value = new NativeTileExtensionString { Data = _pointer, Length = checked((nuint)byteCount) };
        }

        public void Dispose()
        {
            if (_pointer == IntPtr.Zero) return;
            Marshal.FreeCoTaskMem(_pointer);
            _pointer = IntPtr.Zero;
        }
    }

    private static string ReadNativeString(
        NativeTileExtensionString value,
        int maximumBytes,
        string context)
    {
        if (value.Length == 0 || value.Length > checked((nuint)maximumBytes) || value.Data == IntPtr.Zero)
            throw new InvalidDataException($"Native {context} is missing or exceeds its bound.");
        return Marshal.PtrToStringUTF8(value.Data, checked((int)value.Length))
            ?? throw new InvalidDataException($"Native {context} is not valid UTF-8.");
    }

    private sealed class NativeInstance : IRuntimeInstance
    {
        private readonly NativePlugin _plugin;
        private readonly Action<ProjectComponentDiagnosticSeverity, string> _diagnostic;
        private readonly NativeDiagnosticDelegate _nativeDiagnostic;
        private readonly string _entityId;
        private IntPtr _handle;
        private bool _enabled;
        private bool _failed;

        public NativeInstance(
            NativePlugin plugin,
            string entityId,
            string properties,
            Action<ProjectComponentDiagnosticSeverity, string> diagnostic)
        {
            _plugin = plugin;
            _diagnostic = diagnostic;
            _entityId = entityId;
            _nativeDiagnostic = (_, severity, message, length) =>
            {
                var text = message == IntPtr.Zero ? string.Empty
                    : Marshal.PtrToStringUTF8(message, checked((int)length)) ?? string.Empty;
                diagnostic((ProjectComponentDiagnosticSeverity)Math.Clamp(severity, 0, 2), text);
            };
            var entity = Marshal.StringToCoTaskMemUTF8(entityId);
            try { _handle = plugin.CreateHandle(entity, _nativeDiagnostic); }
            finally { Marshal.FreeCoTaskMem(entity); }
            if (_handle == IntPtr.Zero) throw new InvalidDataException("Native component create returned a null handle.");
            try
            {
                var propertyPointer = Marshal.StringToCoTaskMemUTF8(properties);
                try
                {
                    if (plugin.SetProperties(_handle, propertyPointer,
                            (nuint)System.Text.Encoding.UTF8.GetByteCount(properties)) != 0)
                    {
                        throw new InvalidDataException(plugin.LastError(_handle));
                    }
                }
                finally
                {
                    Marshal.FreeCoTaskMem(propertyPointer);
                }
                var enable = default(NativeUpdate);
                if (plugin.Dispatch(
                        _handle,
                        NativeLifecyclePhase.Enable,
                        ref enable) != 0)
                {
                    throw new InvalidDataException(plugin.LastError(_handle));
                }
                _enabled = plugin.IsV2;
            }
            catch
            {
                var failedHandle = _handle;
                _handle = IntPtr.Zero;
                try
                {
                    plugin.DestroyHandle(failedHandle);
                }
                catch (Exception destroyException)
                {
                    diagnostic(ProjectComponentDiagnosticSeverity.Error,
                        $"Native component destruction after failed property initialization also failed: {destroyException.Message}");
                }
                throw;
            }
        }

        public void FixedUpdate(ProjectComponentUpdate update) =>
            Dispatch(NativeLifecyclePhase.FixedUpdate, "fixed update", update);

        public void Update(ProjectComponentUpdate update) =>
            Dispatch(NativeLifecyclePhase.VariableUpdate, "variable update", update);

        public void LateUpdate(ProjectComponentUpdate update) =>
            Dispatch(NativeLifecyclePhase.LateUpdate, "late update", update);

        public void SubmitRender(ProjectComponentUpdate update) =>
            Dispatch(NativeLifecyclePhase.RenderSubmission, "render submission", update);

        private void Dispatch(
            NativeLifecyclePhase phase,
            string stage,
            ProjectComponentUpdate update)
        {
            if (_failed || _handle == IntPtr.Zero) return;
            var input = JsonSerializer.Serialize(update.InputActions);
            var inputPointer = Marshal.StringToCoTaskMemUTF8(input);
            try
            {
                var native = new NativeUpdate
                {
                    ElapsedSeconds = update.ElapsedSeconds,
                    DeltaSeconds = update.DeltaSeconds,
                    InputJson = inputPointer,
                    InputJsonLength = (nuint)System.Text.Encoding.UTF8.GetByteCount(input),
                };
                if (_plugin.Dispatch(_handle, phase, ref native) != 0)
                {
                    _failed = true;
                    _diagnostic(ProjectComponentDiagnosticSeverity.Error,
                        $"Native component on {_entityId} {stage} failed and was disabled: {_plugin.LastError(_handle)}");
                    DisableBestEffort(stage);
                }
            }
            finally { Marshal.FreeCoTaskMem(inputPointer); }
        }

        private void DisableBestEffort(string reason)
        {
            if (!_enabled || _handle == IntPtr.Zero) return;
            _enabled = false;
            var update = default(NativeUpdate);
            try
            {
                if (_plugin.Dispatch(
                        _handle,
                        NativeLifecyclePhase.Disable,
                        ref update) != 0)
                {
                    _diagnostic(ProjectComponentDiagnosticSeverity.Error,
                        $"Native component on {_entityId} disable after {reason} failed: {_plugin.LastError(_handle)}");
                }
            }
            catch (Exception exception)
            {
                _diagnostic(ProjectComponentDiagnosticSeverity.Error,
                    $"Native component on {_entityId} disable after {reason} failed: {exception.Message}");
            }
        }

        public void Dispose()
        {
            if (_handle == IntPtr.Zero) return;
            DisableBestEffort("disposal");
            _plugin.DestroyHandle(_handle);
            _handle = IntPtr.Zero;
        }
    }
}
