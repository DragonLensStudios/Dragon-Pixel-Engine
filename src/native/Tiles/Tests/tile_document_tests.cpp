#include <dragonpixel/tiles/tile_documents.h>

#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace
{
void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error{message};
}
}

int main()
{
    try
    {
        using namespace dragonpixel;
        tiles::tile_set_document set{
            *core::uuid::parse("4fe655df-c40f-4e48-a5cc-fbe9bd356ac6"), "Tiles",
            *core::uuid::parse("dd02cd2a-8a7e-4b27-9d26-06331093885a"), {32, 32}, {}, {}, 32.0,
            {{*core::uuid::parse("b4a8bd46-f46a-4490-be95-f8a149187deb"), "One", {0, 0, 32, 32}, tiles::collision_rectangle{}}}};
        const auto encoded_set = tiles::write_tile_set(set);
        const auto decoded_set = tiles::read_tile_set(encoded_set);
        require(decoded_set.succeeded() && *decoded_set.document == set, "TileSet did not round-trip.");
        set.tiles.front().collision.reset();
        const auto collision_free_set = tiles::read_tile_set(tiles::write_tile_set(set));
        require(collision_free_set.succeeded() && *collision_free_set.document == set,
            "A TileSet tile without collision did not round-trip as JSON null.");

        tiles::tilemap_document map{
            *core::uuid::parse("fd2f3574-8e6f-43d1-bc96-c6650ab49a54"), "Map", {set.asset_id},
            {{*core::uuid::parse("59737391-9417-45bf-a8af-cb6e24e7aa38"), "Ground", true, 0,
                {{0, 0, {{0, set.tiles.front().tile_id, false, false, 0}}}}}}};
        const auto encoded_map = tiles::write_tilemap(map);
        const auto decoded_map = tiles::read_tilemap(encoded_map);
        require(decoded_map.succeeded() && *decoded_map.document == map, "Tilemap did not round-trip.");
        require(tiles::write_tilemap(*decoded_map.document) == encoded_map, "Tilemap output was not deterministic.");
        require(!tiles::read_tilemap("{\"format\":\"dpe.tilemap\",\"formatVersion\":2}").succeeded(),
            "Unsupported Tilemap version was accepted.");
        std::cout << "Tile document tests passed.\n";
        return EXIT_SUCCESS;
    }
    catch (const std::exception& exception)
    {
        std::cerr << exception.what() << '\n';
        return EXIT_FAILURE;
    }
}
