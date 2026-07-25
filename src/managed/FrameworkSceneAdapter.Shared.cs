using DragonPixel.Runtime;
using Microsoft.Xna.Framework;

namespace DragonPixel.Runtime.FrameworkWorker;

internal sealed class FrameworkSceneAdapter : IFrameworkSceneAdapter
{
#if DPE_MONOGAME
    public string Name => "MonoGame";
    public bool Experimental => false;
#elif DPE_KNI
    public string Name => "KNI";
    public bool Experimental => true;
#else
#error A framework symbol must be defined.
#endif

    public string Version => typeof(Matrix).Assembly.GetName().Version?.ToString() ?? "unknown";

    public IReadOnlyList<ProjectedTriangle> CreateTriangles(float angle, int width, int height)
    {
        var vertices = new[]
        {
            new Vector3(-1, -1, -1), new Vector3(1, -1, -1),
            new Vector3(1, 1, -1), new Vector3(-1, 1, -1),
            new Vector3(-1, -1, 1), new Vector3(1, -1, 1),
            new Vector3(1, 1, 1), new Vector3(-1, 1, 1),
        };
        var faces = new[]
        {
            (0, 2, 1), (0, 3, 2),
            (4, 5, 6), (4, 6, 7),
            (0, 1, 5), (0, 5, 4),
            (2, 3, 7), (2, 7, 6),
            (1, 2, 6), (1, 6, 5),
            (3, 0, 4), (3, 4, 7),
        };

        var world = Matrix.CreateRotationY(angle) * Matrix.CreateRotationX(-0.42f);
        var transformed = vertices.Select(vertex => Vector3.Transform(vertex, world)).ToArray();
        var lightDirection = Vector3.Normalize(new Vector3(-0.4f, -0.7f, -1.0f));
        var triangles = new List<ProjectedTriangle>(faces.Length);
        foreach (var (first, second, third) in faces)
        {
            var edgeA = transformed[second] - transformed[first];
            var edgeB = transformed[third] - transformed[first];
            var normal = Vector3.Normalize(Vector3.Cross(edgeA, edgeB));
            var light = Math.Clamp(Vector3.Dot(normal, -lightDirection), 0.15f, 1.0f);
            triangles.Add(new ProjectedTriangle(
                Project(transformed[first], light, width, height),
                Project(transformed[second], light, width, height),
                Project(transformed[third], light, width, height)));
        }

        return triangles;
    }

    private static ProjectedVertex Project(Vector3 vertex, float light, int width, int height)
    {
        var distance = vertex.Z + 4.5f;
        var scale = Math.Min(width, height) * 0.32f / distance;
        var centerX = width * 0.70f;
        var centerY = height * 0.48f;
        return new ProjectedVertex(
            (int)MathF.Round(centerX + (vertex.X * scale)),
            (int)MathF.Round(centerY - (vertex.Y * scale)),
            light);
    }
}
