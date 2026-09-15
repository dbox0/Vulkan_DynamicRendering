#include <SDL3/SDL_main.h>
#include <iostream>
#include "application.h"

int main(int argc, char *argv[])
{
    Application app;

    if (!app.initialize()) {
        app.shutdown();
        return 1;
    }
    if (argc > 1) {
        if (!app.loadData(argv[1])) {
            std::cerr << "[warn] Starting with an empty scene" << std::endl;
        }
    }

    app.run();
    app.shutdown();
    return 0;
}
