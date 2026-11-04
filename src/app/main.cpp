#include "application.h"
#include "map/map_draft_store.h"
#include "smoke/smoke_capture.h"
#include "smoke/smoke_input.h"
#include "smoke/ui_probe.h"
#include "ui/native_dialog.h"
#include "ui/ui_theme.h"
#include "ui/window_chrome.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <imgui_internal.h>

#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace
{

// Scale the 1600 by 900 canvas to fit, then expand the layout to fill other window shapes.
constexpr float kCanvasWidth  = 1600.0F;
constexpr float kCanvasHeight = 900.0F;
float           uiScale       = 1.0F;

constexpr unsigned int kSmokeScheduledFrames = 16;

constexpr unsigned int kSmokeInputFrameLimit = 6000;

void glfwErrorCallback(int error, const char* description)
{
    std::fprintf(stderr, "GLFW error %d: %s\n", error, description);
}

void dropCallback(GLFWwindow* window, int count, const char** paths)
{
    std::vector<std::filesystem::path> droppedPaths;
    for (int index = 0; index < count; ++index)
    {
        droppedPaths.emplace_back(paths[index]);
    }
    static_cast<rerevved::tools::Application*>(glfwGetWindowUserPointer(window))->DropPaths(droppedPaths);
}

// Framebuffer pixels per window unit. GLFW reports the cursor in window units,
// which are not pixels on scaled Wayland and macOS displays.
ImVec2 pixelsPerUnit(GLFWwindow* window)
{
    int width             = 0;
    int height            = 0;
    int framebufferWidth  = 0;
    int framebufferHeight = 0;
    glfwGetWindowSize(window, &width, &height);
    glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
    return ImVec2(width > 0 ? static_cast<float>(framebufferWidth) / static_cast<float>(width) : 1.0F,
                  height > 0 ? static_cast<float>(framebufferHeight) / static_cast<float>(height) : 1.0F);
}

ImVec2 toCanvas(GLFWwindow* window, double x, double y)
{
    const auto ratio = pixelsPerUnit(window);
    return ImVec2(static_cast<float>(x) * ratio.x / uiScale, static_cast<float>(y) * ratio.y / uiScale);
}

void cursorPositionCallback(GLFWwindow* window, double x, double y)
{
    const auto position = toCanvas(window, x, y);
    ImGui_ImplGlfw_CursorPosCallback(window, position.x, position.y);
}

// Logical size whose product with the scale, truncated as the OpenGL backend
// does, still reaches every framebuffer pixel.
float logicalSize(int pixels)
{
    float size = static_cast<float>(pixels) / uiScale;
    while (static_cast<int>(size * uiScale) < pixels)
    {
        size = std::nextafter(size, std::numeric_limits<float>::infinity());
    }
    return size;
}

// Runs after ImGui_ImplGlfw_NewFrame, which reports physical window pixels.
void applyUiScale(GLFWwindow* window, ImGuiIO& io)
{
    int framebufferWidth  = 0;
    int framebufferHeight = 0;
    glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
    if (framebufferWidth <= 0 || framebufferHeight <= 0)
    {
        return;
    }
    uiScale                    = std::min(static_cast<float>(framebufferWidth) / kCanvasWidth, static_cast<float>(framebufferHeight) / kCanvasHeight);
    io.DisplaySize             = ImVec2(logicalSize(framebufferWidth), logicalSize(framebufferHeight));
    io.DisplayFramebufferScale = ImVec2(uiScale, uiScale);
    // The backend's fallback for the focused window feeds raw cursor pixels until the
    // first cursor enter event, so replace it with the canvas position.
    if (glfwGetWindowAttrib(window, GLFW_FOCUSED) != 0)
    {
        double cursorX = 0;
        double cursorY = 0;
        glfwGetCursorPos(window, &cursorX, &cursorY);
        const auto position = toCanvas(window, cursorX, cursorY);
        io.AddMousePosEvent(position.x, position.y);
    }
}

void placeCanvas(ImDrawData& data, int framebufferWidth, int framebufferHeight)
{
    data.DisplaySize = ImVec2(logicalSize(framebufferWidth), logicalSize(framebufferHeight));
}

void sizeWindowToMonitor(GLFWwindow* window)
{
    auto* monitor = glfwGetPrimaryMonitor();
    int   left    = 0;
    int   top     = 0;
    int   width   = 0;
    int   height  = 0;
    if (monitor == nullptr)
    {
        return;
    }
    glfwGetMonitorWorkarea(monitor, &left, &top, &width, &height);
    const int windowWidth  = static_cast<int>(std::min(width * 0.85F, height * 0.85F * kCanvasWidth / kCanvasHeight));
    const int windowHeight = static_cast<int>(std::lround(windowWidth * kCanvasHeight / kCanvasWidth));
    if (windowWidth <= 0 || windowHeight <= 0)
    {
        return;
    }
    const int windowLeft = left + (width - windowWidth) / 2;
    const int windowTop  = top + (height - windowHeight) / 2;
    glfwSetWindowSize(window, windowWidth, windowHeight);
    glfwSetWindowPos(window, windowLeft, windowTop);
    // GLFW adds a system frame the custom chrome does not have, so take the
    // difference back off once.
    if (rerevved::tools::ChromeActive())
    {
        int actualWidth  = 0;
        int actualHeight = 0;
        int actualLeft   = 0;
        int actualTop    = 0;
        glfwGetWindowSize(window, &actualWidth, &actualHeight);
        glfwSetWindowSize(window, 2 * windowWidth - actualWidth, 2 * windowHeight - actualHeight);
        glfwGetWindowPos(window, &actualLeft, &actualTop);
        glfwSetWindowPos(window, 2 * windowLeft - actualLeft, 2 * windowTop - actualTop);
    }
}

} // namespace

int main(int argc, char** argv)
{
    bool                                 smokeTest = false;
    std::optional<std::filesystem::path> captureDirectory;
    std::optional<std::filesystem::path> smokeDropPath;
    std::vector<std::filesystem::path>   referencePaths;
    std::optional<std::filesystem::path> inputScript;
    std::optional<std::filesystem::path> smokeData;
    for (int index = 1; index < argc; ++index)
    {
        const std::string_view argument(argv[index]);
        if (argument == "--smoke-compare")
        {
            if (argc != index + 4)
            {
                std::fprintf(stderr, "Usage: rerevved-tools --smoke-compare <baseline-dir> <candidate-dir> <existing-diff-dir>\n");
                return 1;
            }
            const auto compared = rerevved::tools::CompareSmokeCaptures(argv[index + 1], argv[index + 2], argv[index + 3]);
            if (!compared)
            {
                std::fprintf(stderr, "%s\n", compared.error().c_str());
                return 1;
            }
            return *compared ? 0 : 2;
        }
        if (argument == "--smoke-test")
        {
            smokeTest = true;
        }
        else if (argument == "--smoke-input")
        {
            if (inputScript || index + 1 == argc || std::string_view(argv[index + 1]).starts_with("--"))
            {
                std::fprintf(stderr, "Usage: rerevved-tools --smoke-test [paths] --smoke-input <script>\n");
                return 1;
            }
            inputScript = std::filesystem::path(argv[++index]);
        }
        else if (argument == "--smoke-data")
        {
            if (smokeData || index + 1 == argc || std::string_view(argv[index + 1]).starts_with("--"))
            {
                std::fprintf(stderr, "Usage: rerevved-tools --smoke-test [paths] --smoke-data <existing-directory>\n");
                return 1;
            }
            smokeData = std::filesystem::path(argv[++index]);
        }
        else if (argument == "--smoke-capture")
        {
            if (captureDirectory || index + 1 == argc || std::string_view(argv[index + 1]).starts_with("--"))
            {
                std::fprintf(stderr, "Usage: rerevved-tools --smoke-test [paths] --smoke-capture <existing-directory>\n");
                return 1;
            }
            captureDirectory = std::filesystem::path(argv[++index]);
        }
        else
        {
            referencePaths.emplace_back(argv[index]);
            if (!smokeDropPath && referencePaths.back().filename() == "README.md")
            {
                smokeDropPath = referencePaths.back();
            }
        }
    }
    std::optional<rerevved::tools::SmokeInputRunner> inputRunner;
    if (inputScript)
    {
        auto commands = smokeTest ? rerevved::tools::LoadSmokeInput(*inputScript) : std::unexpected(std::string("--smoke-input requires --smoke-test"));
        if (!commands)
        {
            std::fprintf(stderr, "%s\n", commands.error().c_str());
            return 1;
        }
        if (!captureDirectory && std::ranges::any_of(*commands, [](const auto& command)
                                                     {
                                                         return command.kind == rerevved::tools::SmokeInputCommand::Kind::Capture;
                                                     }))
        {
            std::fprintf(stderr, "Smoke input capture steps require --smoke-capture\n");
            return 1;
        }
        inputRunner.emplace(std::move(*commands));
    }
    if (smokeData)
    {
        std::error_code error;
        if (!smokeTest || !std::filesystem::is_directory(*smokeData, error))
        {
            std::fprintf(stderr, "--smoke-data requires --smoke-test and an existing directory\n");
            return 1;
        }
    }
    if (captureDirectory)
    {
        if (!smokeTest)
        {
            std::fprintf(stderr, "--smoke-capture requires --smoke-test\n");
            return 1;
        }
        if (const auto result = rerevved::tools::CheckSmokeCaptureDirectory(*captureDirectory); !result)
        {
            std::fprintf(stderr, "%s\n", rerevved::tools::SmokeCaptureErrorMessage(result.error()).data());
            return 1;
        }
    }

    glfwSetErrorCallback(glfwErrorCallback);
    if (glfwInit() == GLFW_FALSE)
    {
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    if (smokeTest)
    {
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    }
    // The canvas scale follows the window size, so the window needs no DPI scaling of its own.
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_FALSE);

    GLFWwindow* window = glfwCreateWindow(static_cast<int>(kCanvasWidth), static_cast<int>(kCanvasHeight), "ReRevved Tools", nullptr, nullptr);
    if (window == nullptr)
    {
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    // Dragging an edge keeps 16:9. A maximized or snapped window widens the layout instead.
    // The custom chrome keeps the ratio itself: GLFW's limits assume a system frame.
    if (!rerevved::tools::EnableCustomChrome(window))
    {
        glfwSetWindowSizeLimits(window, static_cast<int>(kCanvasWidth / 2.0F), static_cast<int>(kCanvasHeight / 2.0F), GLFW_DONT_CARE, GLFW_DONT_CARE);
        glfwSetWindowAspectRatio(window, 16, 9);
    }
    if (!smokeTest)
    {
        sizeWindowToMonitor(window);
    }
    if (captureDirectory)
    {
        int width  = 0;
        int height = 0;
        glfwGetFramebufferSize(window, &width, &height);
        if (width > 0 && height > 0 && (width != rerevved::tools::kSmokeCaptureWidth || height != rerevved::tools::kSmokeCaptureHeight))
        {
            int windowWidth  = 0;
            int windowHeight = 0;
            glfwGetWindowSize(window, &windowWidth, &windowHeight);
            glfwSetWindowSize(window,
                              static_cast<int>(std::lround(static_cast<double>(windowWidth) * rerevved::tools::kSmokeCaptureWidth / width)),
                              static_cast<int>(std::lround(static_cast<double>(windowHeight) * rerevved::tools::kSmokeCaptureHeight / height)));
            glfwGetFramebufferSize(window, &width, &height);
        }
        std::fprintf(stdout, "Smoke framebuffer: %dx%d (requested %dx%d)\n", width, height, rerevved::tools::kSmokeCaptureWidth, rerevved::tools::kSmokeCaptureHeight);
    }

    std::string layoutFile;
    if (const auto folder = rerevved::tools::DefaultAppDataFolder(); !smokeTest && !folder.empty())
    {
        std::error_code error;
        std::filesystem::create_directories(folder, error);
        if (!error)
        {
            layoutFile = rerevved::tools::PathToUtf8(folder / "layout.ini");
        }
    }

    bool        smokePassed = true;
    std::string smokeLayout;
    // Scripted input replaces the scheduled smoke states and the layout restart session.
    const int sessions = smokeTest && !inputRunner ? 2 : 1;
    for (int session = 0; session < sessions; ++session)
    {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io    = ImGui::GetIO();
        io.IniFilename = layoutFile.empty() ? nullptr : layoutFile.c_str();
        // Developer popups flag mistakes in smoke captures without interrupting normal use.
        ImGui::GetCurrentContext()->ConfigNavWindowingKeyNext = 0;
        ImGui::GetCurrentContext()->ConfigNavWindowingKeyPrev = 0;
        io.ConfigDebugHighlightIdConflicts                    = smokeTest;
        io.ConfigErrorRecoveryEnableTooltip                   = smokeTest;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        // Fonts and layout share one logical pixel scale (see uiScale), so the theme stays 1x.
        rerevved::tools::ui::ApplyTheme(1.0F);

        ImGui_ImplGlfw_InitForOpenGL(window, true);
        glfwSetCursorPosCallback(window, cursorPositionCallback);
        ImGui_ImplOpenGL3_Init("#version 330");
        if (inputRunner)
        {
            rerevved::tools::ui_probe::SetEnabled(true);
        }

        {
            rerevved::tools::Application application;
            // Smoke runs never touch the user's data, and save maps into --smoke-data without dialogs.
            application.SetDataFolder(smokeTest ? smokeData.value_or(std::filesystem::path()) : rerevved::tools::DefaultAppDataFolder(), !smokeTest);
            application.WaitForMapImages(smokeTest);
            if (smokeTest && session == 1)
            {
                ImGui::LoadIniSettingsFromMemory(smokeLayout.data(), smokeLayout.size());
            }
            // Earlier builds kept the layout in the working folder's imgui.ini, so start the new file from it once.
            std::error_code layoutError;
            if (!layoutFile.empty() && !std::filesystem::exists(rerevved::tools::PathFromUtf8(layoutFile), layoutError) && !layoutError && std::filesystem::is_regular_file("imgui.ini", layoutError))
            {
                ImGui::LoadIniSettingsFromDisk("imgui.ini");
            }
            glfwSetWindowUserPointer(window, &application);
            glfwSetDropCallback(window, dropCallback);
            bool referenceOpened = false;
            for (const auto& path : referencePaths)
            {
                referenceOpened = application.OpenPath(path, rerevved::tools::OpenOrigin::CommandLine) || referenceOpened;
            }

            unsigned int                      smokeFrame = 0;
            rerevved::tools::SmokeInputAction inputAction;
            while (!application.ShouldClose())
            {
                glfwPollEvents();
                if (glfwWindowShouldClose(window) != GLFW_FALSE)
                {
                    glfwSetWindowShouldClose(window, GLFW_FALSE);
                    if (!smokeTest)
                    {
                        application.RequestClose();
                        if (application.ShouldClose())
                        {
                            break;
                        }
                    }
                }
                ImGui_ImplOpenGL3_NewFrame();
                ImGui_ImplGlfw_NewFrame();
                applyUiScale(window, io);
                ImGui::NewFrame();

                if (smokeTest && session == 0 && !inputRunner)
                {
                    if (!application.SetSmokeTestState(smokeFrame))
                    {
                        std::fprintf(stderr, "Smoke state failed at frame %u\n", smokeFrame);
                        smokePassed = false;
                        ImGui::EndFrame();
                        break;
                    }
                }
                application.Draw();
                if (inputRunner)
                {
                    rerevved::tools::ui_probe::EndFrame();
                    auto step = inputRunner->Step(rerevved::tools::ui_probe::Items(), ImGui::GetCurrentContext()->HoveredId);
                    if (!step)
                    {
                        std::fprintf(stderr, "Smoke input failed: %s\n", step.error().c_str());
                        smokePassed = false;
                        ImGui::EndFrame();
                        break;
                    }
                    inputAction = std::move(*step);
                }

                ImGui::Render();
                int width  = 0;
                int height = 0;
                glfwGetFramebufferSize(window, &width, &height);
                placeCanvas(*ImGui::GetDrawData(), width, height);
                glViewport(0, 0, width, height);
                glClearColor(15.0F / 255.0F, 17.0F / 255.0F, 21.0F / 255.0F, 1.0F);
                glClear(GL_COLOR_BUFFER_BIT);
                ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
                if (const auto* capture = std::get_if<rerevved::tools::SmokeInputCapture>(&inputAction))
                {
                    const auto result = captureDirectory ? rerevved::tools::CaptureNamedSmokeFrame(*captureDirectory, capture->name + ".png", width, height) : std::unexpected(rerevved::tools::SmokeCaptureError::DirectoryUnavailable);
                    if (!result)
                    {
                        std::fprintf(stderr, "Smoke input capture %s failed: %s\n", capture->name.c_str(), rerevved::tools::SmokeCaptureErrorMessage(result.error()).data());
                        smokePassed = false;
                        break;
                    }
                }
                else if (captureDirectory && !inputRunner)
                {
                    if (const auto result = rerevved::tools::CaptureSmokeFrame(*captureDirectory, session, smokeFrame, width, height); !result)
                    {
                        std::fprintf(stderr, "Smoke capture failed in session %d frame %u: %s\n", session, smokeFrame, rerevved::tools::SmokeCaptureErrorMessage(result.error()).data());
                        smokePassed = false;
                        break;
                    }
                }
                glfwSwapBuffers(window);
                if (inputRunner)
                {
                    // Pointer input goes through the physical pixel callbacks
                    // so the UI scale mapping is exercised.
                    auto& input = ImGui::GetIO();
                    if (const auto* move = std::get_if<rerevved::tools::SmokeInputMove>(&inputAction))
                    {
                        const auto ratio = pixelsPerUnit(window);
                        cursorPositionCallback(window, move->logical.x * uiScale / ratio.x, move->logical.y * uiScale / ratio.y);
                    }
                    else if (const auto* button = std::get_if<rerevved::tools::SmokeInputButton>(&inputAction))
                    {
                        input.AddMouseButtonEvent(ImGuiMouseButton_Left, button->down);
                    }
                    else if (const auto* key = std::get_if<rerevved::tools::SmokeInputKey>(&inputAction))
                    {
                        for (const auto modifier : { ImGuiMod_Ctrl, ImGuiMod_Shift, ImGuiMod_Alt })
                        {
                            if ((key->chord & modifier) != 0)
                            {
                                input.AddKeyEvent(modifier, key->down);
                            }
                        }
                        input.AddKeyEvent(static_cast<ImGuiKey>(key->chord & ~ImGuiMod_Mask_), key->down);
                    }
                    else if (const auto* text = std::get_if<rerevved::tools::SmokeInputText>(&inputAction))
                    {
                        input.AddInputCharactersUTF8(text->text.c_str());
                    }
                    else if (std::holds_alternative<rerevved::tools::SmokeInputClose>(inputAction))
                    {
                        application.RequestClose();
                    }
                    else if (const auto* drop = std::get_if<rerevved::tools::SmokeInputDrop>(&inputAction))
                    {
                        const std::array paths{ drop->path };
                        application.DropPaths(paths);
                    }
                    if (++smokeFrame == kSmokeInputFrameLimit || inputRunner->Finished())
                    {
                        break;
                    }
                    continue;
                }
                if (captureDirectory && session == 0 && smokeFrame == 1 && smokeDropPath)
                {
                    const std::array paths{ *smokeDropPath };
                    application.DropPaths(paths);
                    std::fprintf(stdout,
                                 "Smoke drop: %s. Application exposes no public drop result "
                                 "to assert\n",
                                 smokeDropPath->string().c_str());
                }
                if (smokeTest && ++smokeFrame == (session == 0 ? kSmokeScheduledFrames : 2U))
                {
                    break;
                }
            }
            if (inputRunner)
            {
                if (smokePassed && application.ShouldClose())
                {
                    if (const auto exited = inputRunner->Exited(); !exited)
                    {
                        std::fprintf(stderr, "Smoke input failed: %s\n", exited.error().c_str());
                        smokePassed = false;
                    }
                }
                else if (smokePassed && !inputRunner->Finished())
                {
                    std::fprintf(stderr, "Smoke input stopped after %u frames before the script finished\n", smokeFrame);
                    smokePassed = false;
                }
                if (smokePassed)
                {
                    std::fprintf(stdout, "Smoke input passed in %u frames at UI scale %.2f\n", smokeFrame, uiScale);
                }
            }
            else if (smokeTest && smokePassed && smokeFrame != (session == 0 ? kSmokeScheduledFrames : 2U))
            {
                std::fprintf(stderr, "Smoke session %d ended before all scheduled frames\n", session);
                smokePassed = false;
            }
            if (smokeTest && session == 0 && !inputRunner)
            {
                std::size_t size = 0;
                const auto* data = ImGui::SaveIniSettingsToMemory(&size);
                smokeLayout.assign(data, size);
            }
            else if (smokeTest && !inputRunner && !application.CheckSmokeTestRestart(referenceOpened))
            {
                std::fprintf(stderr, "Smoke layout restart failed\n");
                smokePassed = false;
            }
        }

        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        if (!smokePassed)
        {
            break;
        }
    }
    glfwDestroyWindow(window);
    glfwTerminate();
    return smokePassed ? 0 : 1;
}
