#include "model_maker/application.hpp"
#include "model_maker/force_diagram.hpp"

#include <exception>
#include <windows.h>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR commandLine, int showCommand) {
    try {
        mm::registerForceDiagramClass(instance);
        mm::Application application(instance);
        std::optional<std::filesystem::path> startupDxf;
        if (commandLine && *commandLine) {
            std::wstring value(commandLine);
            if (value.size() >= 2 && value.front() == L'"' && value.back() == L'"')
                value = value.substr(1, value.size() - 2);
            startupDxf = std::filesystem::path(value);
        }
        return application.run(showCommand, startupDxf);
    } catch (const std::exception& error) {
        MessageBoxA(nullptr, error.what(), "Model Maker - Fatal Error", MB_OK | MB_ICONERROR);
        return 1;
    }
}
