#include "FileBridge.h"
#include "FontData.h"
#include "ui/EditorApp.h"
#include "ui/Theme.h"
#include <GLES3/gl3.h>
#include <SDL.h>
#include <algorithm>
#include <emscripten.h>
#include <emscripten/html5.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl2.h>
#include <memory>

namespace
{
struct Application
{
    SDL_Window*          Window = nullptr;
    SDL_GLContext        Context = nullptr;
    odreditor::EditorApp Editor;
    float                FontScale = 0;
    bool                 ContextLost = false;
    bool                 ContextRestored = false;
    ~Application()
    {
        if (ImGui::GetCurrentContext())
        {
            if (ImGui::GetIO().BackendRendererUserData)
                ImGui_ImplOpenGL3_Shutdown();
            if (ImGui::GetIO().BackendPlatformUserData)
                ImGui_ImplSDL2_Shutdown();
            ImGui::DestroyContext();
        }
        SDL_GL_DeleteContext(Context);
        SDL_DestroyWindow(Window);
        SDL_Quit();
    }
};
std::unique_ptr<Application> App;

void DrawFrame()
{
    if (App->ContextLost)
        return;
    double width = 0, height = 0;
    emscripten_get_element_css_size("#canvas", &width, &height);
    const double dpi = emscripten_get_device_pixel_ratio();
    if (App->FontScale != dpi || App->ContextRestored)
    {
        auto& io = ImGui::GetIO();
        ImGui_ImplOpenGL3_DestroyDeviceObjects();
        io.Fonts->Clear();
        ImFontConfig config;
        config.SizePixels = odreditor::BaseFontSize * dpi;
        config.FontDataOwnedByAtlas = false;
        io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(odreditor::FontData), sizeof(odreditor::FontData), config.SizePixels, &config);
        io.FontGlobalScale = 1.0f / dpi;
        App->FontScale = dpi;
        App->ContextRestored = false;
    }
    const int logicalX = std::max(1, static_cast<int>(width));
    const int logicalY = std::max(1, static_cast<int>(height));
    int       oldX = 0, oldY = 0;
    SDL_GetWindowSize(App->Window, &oldX, &oldY);
    if (oldX != logicalX || oldY != logicalY)
        SDL_SetWindowSize(App->Window, logicalX, logicalY);
    int pixelsX = 0, pixelsY = 0;
    SDL_GL_GetDrawableSize(App->Window, &pixelsX, &pixelsY);
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        ImGui_ImplSDL2_ProcessEvent(&event);
    }
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    auto& io = ImGui::GetIO();
    io.DisplaySize = {static_cast<float>(width), static_cast<float>(height)};
    io.DisplayFramebufferScale = {static_cast<float>(pixelsX / width), static_cast<float>(pixelsY / height)};
    ImGui::NewFrame();
    App->Editor.Draw();
    ImGui::Render();
    glViewport(0, 0, pixelsX, pixelsY);
    glClearColor(0.961f, 0.976f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    SDL_GL_SwapWindow(App->Window);
}
} // namespace

int main()
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0)
        return 1;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    App = std::make_unique<Application>();
    App->Window = SDL_CreateWindow("OpenDRIVE editor", 0, 0, 1280, 800, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!App->Window)
        return 1;
    App->Context = SDL_GL_CreateContext(App->Window);
    if (!App->Context)
        return 1;
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    odreditor::ApplyTheme(1);
    ImGui_ImplSDL2_InitForOpenGL(App->Window, App->Context);
    ImGui_ImplOpenGL3_Init("#version 300 es");
    emscripten_set_webglcontextlost_callback("#canvas",
                                             nullptr,
                                             true,
                                             [](int, const void*, void*) -> EM_BOOL
                                             {
                                                 App->ContextLost = true;
                                                 return true;
                                             });
    emscripten_set_webglcontextrestored_callback("#canvas",
                                                 nullptr,
                                                 true,
                                                 [](int, const void*, void*) -> EM_BOOL
                                                 {
                                                     App->ContextLost = false;
                                                     App->ContextRestored = true;
                                                     return true;
                                                 });
    odreditor::InitializeFileBridge();
    emscripten_set_main_loop(DrawFrame, 0, true);
    return 0;
}
