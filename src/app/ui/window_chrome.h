#pragma once

struct GLFWwindow;

namespace rerevved::tools
{

// Windows only: the system caption goes while resize edges, Snap, the shadow and title bar
// dragging stay native. Elsewhere the system frame stays and ChromeActive() is false.
bool               EnableCustomChrome(GLFWwindow* window);
[[nodiscard]] bool ChromeActive() noexcept;

// The title bar's drag area for this frame, in logical pixels: from the top of
// the window down to bottom, excluding everything right of buttonsLeft.
void               SetChromeDragArea(float bottom, float buttonsLeft, float scale) noexcept;
void               MinimizeWindow();
void               ToggleMaximizeWindow();
void               RequestWindowClose();
[[nodiscard]] bool WindowMaximized();

} // namespace rerevved::tools
