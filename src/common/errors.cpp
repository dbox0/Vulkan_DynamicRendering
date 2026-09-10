#include "errors.h"
#include <SDL3/SDL.h>
#include <iostream>

namespace {
    SDL_Window *g_errorWindow = nullptr;
}

void setErrorWindow(SDL_Window *window) { g_errorWindow = window; }

void showError(const std::string &message) {
    std::cerr << "[error] " << message << std::endl;
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error", message.c_str(), g_errorWindow);
}