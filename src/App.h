#pragma once

#include "GlfwOcctWindow.h"
#include "Viewer.h"

#include <filesystem>
#include <memory>
#include <string>

class App
{
public:
  App();
  ~App();

  App(const App&) = delete;
  App& operator=(const App&) = delete;

  int run(const std::filesystem::path& initialFile);

private:
  struct GlfwSession
  {
    GlfwSession();
    ~GlfwSession();
  };

  struct LoadJob;

  void startLoad(const std::filesystem::path& path);
  void finishLoad();
  void updateTitle();

  static App& from(GLFWwindow* window);
  static void onKey(GLFWwindow* window, int key, int scancode, int action, int mods);
  static void onMouseButton(GLFWwindow* window, int button, int action, int mods);
  static void onCursor(GLFWwindow* window, double x, double y);
  static void onScroll(GLFWwindow* window, double dx, double dy);
  static void onFramebufferSize(GLFWwindow* window, int width, int height);
  static void onRefresh(GLFWwindow* window);
  static void onDrop(GLFWwindow* window, int count, const char** paths);

  GlfwSession glfw_;
  Handle(GlfwOcctWindow) window_;
  std::unique_ptr<Viewer> viewer_;
  std::unique_ptr<LoadJob> job_;
  std::filesystem::path pending_;
  std::string currentName_;
  int cursorX_ = 0;
  int cursorY_ = 0;
};
