#include <SDL3/SDL_main.h>
#include "application.h"

int main(int argc, char *argv[])
{
    // Model path from argv so it isn't hardcoded in loadData anymore.
    const std::filesystem::path modelPath =
        (argc > 1) ? std::filesystem::path(argv[1])
                   : std::filesystem::path("/home/lougi/gltfModels/mariokart8deluxe/scene.gltf");

    Application app;

    if (!app.initialize()) {
        app.shutdown();
        return 1;
    }

    if (!app.loadData(modelPath)) {
        app.shutdown();
        return 1;
    }

    app.run();
    app.shutdown();
    return 0;
}
