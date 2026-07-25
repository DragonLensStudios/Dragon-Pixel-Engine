namespace DragonPixel.Runtime;

internal static class SoftwareFrameRenderer
{
    public static void Render(
        byte[] pixels,
        int width,
        int height,
        IReadOnlyList<ProjectedTriangle> triangles,
        float phase,
        bool experimental)
    {
        Clear(pixels, width, height, experimental ? (byte)44 : (byte)30, 24, 36);
        DrawGrid(pixels, width, height);
        DrawSprite(pixels, width, height, phase, experimental);
        foreach (var triangle in triangles.OrderBy(static triangle =>
                     triangle.A.Light + triangle.B.Light + triangle.C.Light))
        {
            FillTriangle(pixels, width, height, triangle, experimental);
            DrawLine(pixels, width, height, triangle.A.X, triangle.A.Y, triangle.B.X, triangle.B.Y, 238, 238, 246);
            DrawLine(pixels, width, height, triangle.B.X, triangle.B.Y, triangle.C.X, triangle.C.Y, 238, 238, 246);
            DrawLine(pixels, width, height, triangle.C.X, triangle.C.Y, triangle.A.X, triangle.A.Y, 238, 238, 246);
        }
    }

    private static void Clear(byte[] pixels, int width, int height, byte red, byte green, byte blue)
    {
        for (var y = 0; y < height; y++)
        {
            for (var x = 0; x < width; x++)
            {
                SetPixel(pixels, width, height, x, y, blue, green, red, 255);
            }
        }
    }

    private static void DrawGrid(byte[] pixels, int width, int height)
    {
        for (var y = height / 2; y < height; y += 24)
        {
            DrawLine(pixels, width, height, 0, y, width - 1, y, 60, 55, 66);
        }

        for (var x = 0; x < width; x += 32)
        {
            DrawLine(pixels, width, height, x, height / 2, x, height - 1, 52, 48, 58);
        }
    }

    private static void DrawSprite(byte[] pixels, int width, int height, float phase, bool experimental)
    {
        var size = Math.Max(24, Math.Min(width, height) / 5);
        var originX = width / 5 - size / 2;
        var originY = height / 2 - size / 2 + (int)(MathF.Sin(phase) * 12.0f);
        for (var y = 0; y < size; y++)
        {
            for (var x = 0; x < size; x++)
            {
                var checker = ((x / 10) + (y / 10)) % 2 == 0;
                var red = experimental ? (byte)(checker ? 238 : 160) : (byte)(checker ? 243 : 190);
                var green = experimental ? (byte)(checker ? 126 : 72) : (byte)(checker ? 94 : 50);
                var blue = experimental ? (byte)(checker ? 66 : 40) : (byte)(checker ? 172 : 106);
                SetPixel(pixels, width, height, originX + x, originY + y, blue, green, red, 255);
            }
        }
    }

    private static void FillTriangle(
        byte[] pixels,
        int width,
        int height,
        ProjectedTriangle triangle,
        bool experimental)
    {
        var minX = Math.Max(0, Math.Min(triangle.A.X, Math.Min(triangle.B.X, triangle.C.X)));
        var maxX = Math.Min(width - 1, Math.Max(triangle.A.X, Math.Max(triangle.B.X, triangle.C.X)));
        var minY = Math.Max(0, Math.Min(triangle.A.Y, Math.Min(triangle.B.Y, triangle.C.Y)));
        var maxY = Math.Min(height - 1, Math.Max(triangle.A.Y, Math.Max(triangle.B.Y, triangle.C.Y)));
        var area = Edge(triangle.A.X, triangle.A.Y, triangle.B.X, triangle.B.Y, triangle.C.X, triangle.C.Y);
        if (area == 0)
        {
            return;
        }

        var light = Math.Clamp((triangle.A.Light + triangle.B.Light + triangle.C.Light) / 3.0f, 0.15f, 1.0f);
        var red = (byte)((experimental ? 216 : 90) * light);
        var green = (byte)((experimental ? 118 : 168) * light);
        var blue = (byte)((experimental ? 52 : 236) * light);
        for (var y = minY; y <= maxY; y++)
        {
            for (var x = minX; x <= maxX; x++)
            {
                var edge0 = Edge(triangle.B.X, triangle.B.Y, triangle.C.X, triangle.C.Y, x, y);
                var edge1 = Edge(triangle.C.X, triangle.C.Y, triangle.A.X, triangle.A.Y, x, y);
                var edge2 = Edge(triangle.A.X, triangle.A.Y, triangle.B.X, triangle.B.Y, x, y);
                if ((edge0 >= 0 && edge1 >= 0 && edge2 >= 0) || (edge0 <= 0 && edge1 <= 0 && edge2 <= 0))
                {
                    SetPixel(pixels, width, height, x, y, blue, green, red, 255);
                }
            }
        }
    }

    private static int Edge(int ax, int ay, int bx, int by, int px, int py) =>
        ((px - ax) * (by - ay)) - ((py - ay) * (bx - ax));

    private static void DrawLine(
        byte[] pixels,
        int width,
        int height,
        int x0,
        int y0,
        int x1,
        int y1,
        byte blue,
        byte green,
        byte red)
    {
        var deltaX = Math.Abs(x1 - x0);
        var stepX = x0 < x1 ? 1 : -1;
        var deltaY = -Math.Abs(y1 - y0);
        var stepY = y0 < y1 ? 1 : -1;
        var error = deltaX + deltaY;
        while (true)
        {
            SetPixel(pixels, width, height, x0, y0, blue, green, red, 255);
            if (x0 == x1 && y0 == y1)
            {
                break;
            }

            var doubledError = 2 * error;
            if (doubledError >= deltaY)
            {
                error += deltaY;
                x0 += stepX;
            }

            if (doubledError <= deltaX)
            {
                error += deltaX;
                y0 += stepY;
            }
        }
    }

    private static void SetPixel(
        byte[] pixels,
        int width,
        int height,
        int x,
        int y,
        byte blue,
        byte green,
        byte red,
        byte alpha)
    {
        if ((uint)x >= (uint)width || (uint)y >= (uint)height)
        {
            return;
        }

        var offset = ((y * width) + x) * 4;
        pixels[offset] = blue;
        pixels[offset + 1] = green;
        pixels[offset + 2] = red;
        pixels[offset + 3] = alpha;
    }
}
