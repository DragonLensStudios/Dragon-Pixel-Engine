#include "TiledJsonImporter.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

int main(int argc, char** argv)
{
    if (argc != 3 || std::string{argv[1]} != "--request")
    {
        std::cerr << "Usage: DragonPixelTiledImporterWorker --request <request.json>\n";
        return EXIT_FAILURE;
    }
    std::string error;
    const auto result = dragonpixel::importers::tiled::execute_request_file(
        std::filesystem::path{argv[2]}, error);
    if (result != 0 && !error.empty()) std::cerr << error << '\n';
    return result;
}
