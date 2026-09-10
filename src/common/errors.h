#pragma once
#include <string>

struct SDL_Window;

void setErrorWindow(SDL_Window *window);
void showError(const std::string &message);