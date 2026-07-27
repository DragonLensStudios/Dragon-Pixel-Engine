using Microsoft.Xna.Framework;
using Microsoft.Xna.Framework.Graphics;

internal sealed class Game1 : Game
{
    private GraphicsDeviceManager _graphics;
    private SpriteBatch? _spriteBatch;

    public Game1()
    {
        _graphics = new GraphicsDeviceManager(this);
    }

    protected override void LoadContent()
    {
        _spriteBatch = new SpriteBatch(GraphicsDevice);
        _ = Content.Load<Texture2D>("dragon");
    }
}
