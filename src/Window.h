#pragma once

#include <adwaita.h>

#include <filesystem>

inline constexpr const char* kAppId = "io.github.ivanlukomskiy.YourSloppyViewer";
inline constexpr const char* kAppName = "Your Sloppy Viewer";

GtkWindow* createViewerWindow(AdwApplication* application, const std::filesystem::path& initialFile);
