#include "App.h"

#include "StepLoader.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <thread>
#include <utility>

struct App::LoadJob
{
  std::filesystem::path path;
  std::string name;
  StepLoadResult result;
  std::atomic<bool> done{false};
  std::jthread thread;
};

namespace
{
constexpr const char* kAppName = "StepView";

void onGlfwError(int code, const char* description)
{
  std::fprintf(stderr, "stepview: GLFW error %d: %s\n", code, description);
}
}

App::GlfwSession::GlfwSession()
{
  glfwSetErrorCallback(onGlfwError);
  glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
  if (glfwInit() != GLFW_TRUE)
  {
    throw std::runtime_error(
      "cannot initialize GLFW on X11; an X server or XWayland is required (is DISPLAY set?)");
  }
}

App::GlfwSession::~GlfwSession()
{
  glfwTerminate();
}

App::App()
{
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
  glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
  glfwWindowHintString(GLFW_X11_CLASS_NAME, "stepview");
  glfwWindowHintString(GLFW_X11_INSTANCE_NAME, "stepview");

  window_ = new GlfwOcctWindow(1100, 720, kAppName);
  GLFWwindow* window = window_->Glfw();
  if (window == nullptr)
  {
    throw std::runtime_error("cannot create an OpenGL 3.3 window");
  }

  glfwMakeContextCurrent(window);
  glfwSwapInterval(1);

  float scaleX = 1.0f, scaleY = 1.0f;
  glfwGetWindowContentScale(window, &scaleX, &scaleY);
  viewer_ = std::make_unique<Viewer>(window_, std::max(1.0f, scaleX));

  glfwSetWindowUserPointer(window, this);
  glfwSetKeyCallback(window, onKey);
  glfwSetMouseButtonCallback(window, onMouseButton);
  glfwSetCursorPosCallback(window, onCursor);
  glfwSetScrollCallback(window, onScroll);
  glfwSetFramebufferSizeCallback(window, onFramebufferSize);
  glfwSetWindowRefreshCallback(window, onRefresh);
  glfwSetDropCallback(window, onDrop);
}

App::~App() = default;

int App::run(const std::filesystem::path& initialFile)
{
  GLFWwindow* window = window_->Glfw();
  if (!initialFile.empty())
  {
    startLoad(initialFile);
  }

  window_->Map();
  glfwSwapInterval(0);
  viewer_->redrawNow();
  glfwSwapInterval(1);

  while (glfwWindowShouldClose(window) == GLFW_FALSE)
  {
    if (job_ && job_->done.load())
    {
      finishLoad();
    }
    if (!job_ && !pending_.empty())
    {
      startLoad(std::exchange(pending_, {}));
    }
    if (viewer_->processEvents())
    {
      glfwPollEvents();
    }
    else
    {
      glfwWaitEvents();
    }
  }

  if (job_)
  {
    glfwHideWindow(window);
    std::fflush(nullptr);
    std::_Exit(EXIT_SUCCESS);
  }
  return EXIT_SUCCESS;
}

void App::startLoad(const std::filesystem::path& path)
{
  job_ = std::make_unique<LoadJob>();
  job_->path = path;
  job_->name = path.filename().string();

  glfwSetWindowTitle(window_->Glfw(), ("Loading " + job_->name + "… — " + kAppName).c_str());
  viewer_->setStatus(Viewer::Status::Info, "Loading " + job_->name + "…");

  job_->thread = std::jthread([job = job_.get()] {
    job->result = loadStep(job->path);
    job->done.store(true);
    glfwPostEmptyEvent();
  });
}

void App::finishLoad()
{
  const std::unique_ptr<LoadJob> job = std::move(job_);
  job->thread.join();

  if (!job->result.model)
  {
    std::fprintf(stderr, "stepview: cannot open '%s': %s\n", job->path.c_str(),
                 job->result.error.c_str());
    viewer_->setStatus(Viewer::Status::Error, "Cannot open " + job->name + ": " + job->result.error);
    updateTitle();
    return;
  }

  const StepModel& model = *job->result.model;
  const double renderMs = viewer_->show(model);
  currentName_ = job->name;
  updateTitle();

  std::printf("%s: parse %.1f ms | transfer %.1f ms | tessellate %.1f ms (%zu triangles) | "
              "first frame %.1f ms\n",
              job->name.c_str(), model.parseMs, model.transferMs, model.meshMs, model.triangles,
              renderMs);
  std::fflush(stdout);
}

void App::updateTitle()
{
  const std::string title = currentName_.empty() ? kAppName : currentName_ + " — " + kAppName;
  glfwSetWindowTitle(window_->Glfw(), title.c_str());
}

App& App::from(GLFWwindow* window)
{
  return *static_cast<App*>(glfwGetWindowUserPointer(window));
}

void App::onKey(GLFWwindow* window, int key, int, int action, int)
{
  if (action != GLFW_PRESS)
  {
    return;
  }

  Viewer& viewer = *from(window).viewer_;
  switch (key)
  {
    case GLFW_KEY_ESCAPE:
      glfwSetWindowShouldClose(window, GLFW_TRUE);
      break;
    case GLFW_KEY_F:
      viewer.fit();
      break;
    case GLFW_KEY_E:
      viewer.toggleEdges();
      break;
    case GLFW_KEY_P:
      viewer.toggleProjection();
      break;
    case GLFW_KEY_1:
    case GLFW_KEY_KP_1:
      viewer.orient(V3d_Yneg);
      break;
    case GLFW_KEY_2:
    case GLFW_KEY_KP_2:
      viewer.orient(V3d_Xpos);
      break;
    case GLFW_KEY_3:
    case GLFW_KEY_KP_3:
      viewer.orient(V3d_Zpos);
      break;
    case GLFW_KEY_0:
    case GLFW_KEY_KP_0:
      viewer.orient(V3d_XposYnegZpos);
      break;
    default:
      break;
  }
}

void App::onMouseButton(GLFWwindow* window, int button, int action, int)
{
  Aspect_VKeyMouse mapped = Aspect_VKeyMouse_NONE;
  switch (button)
  {
    case GLFW_MOUSE_BUTTON_LEFT:
      mapped = Aspect_VKeyMouse_LeftButton;
      break;
    case GLFW_MOUSE_BUTTON_MIDDLE:
      mapped = Aspect_VKeyMouse_MiddleButton;
      break;
    case GLFW_MOUSE_BUTTON_RIGHT:
      mapped = Aspect_VKeyMouse_RightButton;
      break;
    default:
      return;
  }

  App& app = from(window);
  app.viewer_->mouseButton(app.cursorX_, app.cursorY_, mapped, action == GLFW_PRESS);
}

void App::onCursor(GLFWwindow* window, double x, double y)
{
  App& app = from(window);
  app.cursorX_ = static_cast<int>(std::lround(x));
  app.cursorY_ = static_cast<int>(std::lround(y));
  app.viewer_->mouseMove(app.cursorX_, app.cursorY_);
}

void App::onScroll(GLFWwindow* window, double, double dy)
{
  App& app = from(window);
  app.viewer_->scroll(app.cursorX_, app.cursorY_, dy * 10.0);
}

void App::onFramebufferSize(GLFWwindow* window, int width, int height)
{
  if (width > 0 && height > 0)
  {
    from(window).viewer_->resize();
  }
}

void App::onRefresh(GLFWwindow* window)
{
  from(window).viewer_->invalidate();
}

void App::onDrop(GLFWwindow* window, int count, const char** paths)
{
  if (count <= 0)
  {
    return;
  }

  App& app = from(window);
  app.pending_ = paths[0];
  for (int i = 0; i < count; ++i)
  {
    if (hasStepExtension(paths[i]))
    {
      app.pending_ = paths[i];
      break;
    }
  }
}
