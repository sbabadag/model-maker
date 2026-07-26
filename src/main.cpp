#include "model_maker/application.hpp"

#include <exception>
#include <windows.h>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    try {
        mm::Application application(instance);
        return application.run(showCommand);
    } catch (const std::exception& error) {
        MessageBoxA(nullptr, error.what(), "Model Maker - Fatal Error", MB_OK | MB_ICONERROR);
        return 1;
    }
}
