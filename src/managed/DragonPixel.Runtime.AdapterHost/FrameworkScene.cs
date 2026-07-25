namespace DragonPixel.Runtime;

public readonly record struct ProjectedVertex(int X, int Y, float Light);

public readonly record struct ProjectedTriangle(
    ProjectedVertex A,
    ProjectedVertex B,
    ProjectedVertex C);

public interface IFrameworkSceneAdapter
{
    string Name { get; }
    string Version { get; }
    bool Experimental { get; }
    IReadOnlyList<ProjectedTriangle> CreateTriangles(float angle, int width, int height);
}
