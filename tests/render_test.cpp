#include "Camera.h"
#include "Renderer.h"
#include "Loader.h"

#include <epoxy/egl.h>
#include <epoxy/gl.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

int main(int argc, char** argv)
{
  if (argc < 3)
  {
    std::fprintf(stderr, "Usage: render_test model.step out.ppm [edges|shaded|wire] "
                         "[iso|front|right|top] [persp] [nocube]\n");
    return EXIT_FAILURE;
  }

  Renderer::Frame frame;
  frame.width = 1280;
  frame.height = 800;
  frame.scale = 1.0f;
  Vec3 eye{1.0, -1.0, 1.0};
  bool perspective = false;
  for (int i = 3; i < argc; ++i)
  {
    const std::string option = argv[i];
    if (option == "shaded") frame.style = Renderer::Style::Shaded;
    else if (option == "wire") frame.style = Renderer::Style::Wireframe;
    else if (option == "front") eye = {0.0, -1.0, 0.0};
    else if (option == "right") eye = {1.0, 0.0, 0.0};
    else if (option == "top") eye = {0.0, 0.0, 1.0};
    else if (option == "persp") perspective = true;
    else if (option == "nocube") frame.cube = false;
    else if (option == "2x") { frame.width *= 2; frame.height *= 2; frame.scale = 2.0f; }
  }

  LoadProgress progress;
  LoadResult result = loadModel(argv[1], progress);
  if (!result.scene)
  {
    std::fprintf(stderr, "render_test: %s\n", result.error.c_str());
    return EXIT_FAILURE;
  }
  const Scene& scene = *result.scene;
  std::printf("parse %.1f ms | transfer %.1f ms | tessellate %.1f ms | prepare %.1f ms | "
              "%zu triangles, %zu vertices, %zu edge segments\n",
              result.timings.parseMs, result.timings.transferMs, result.timings.meshMs,
              result.timings.buildMs, scene.triangleCount(), scene.vertices.size(),
              scene.edges.size());

  EGLDisplay display = eglGetPlatformDisplayEXT(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
  if (display == EGL_NO_DISPLAY || !eglInitialize(display, nullptr, nullptr))
  {
    std::fprintf(stderr, "render_test: no EGL display\n");
    return EXIT_FAILURE;
  }
  eglBindAPI(EGL_OPENGL_API);
  const EGLint configAttributes[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE,
                                     EGL_OPENGL_BIT, EGL_NONE};
  EGLConfig config = nullptr;
  EGLint configCount = 0;
  eglChooseConfig(display, configAttributes, &config, 1, &configCount);
  const EGLint contextAttributes[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 3,
                                      EGL_CONTEXT_OPENGL_PROFILE_MASK,
                                      EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL_NONE};
  EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttributes);
  if (context == EGL_NO_CONTEXT || !eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context))
  {
    std::fprintf(stderr, "render_test: cannot create an OpenGL 3.3 context\n");
    return EXIT_FAILURE;
  }

  unsigned framebuffer = 0, texture = 0;
  glGenFramebuffers(1, &framebuffer);
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, frame.width, frame.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);

  Renderer renderer;
  renderer.initialize();
  renderer.setScene(scene);

  Camera camera;
  camera.setViewport(frame.width / frame.scale, frame.height / frame.scale);
  camera.setBounds(scene.boundsMin, scene.boundsMax);
  frame.flat = scene.flat;
  camera.setPerspective(perspective && !scene.flat);
  camera.lookFrom(scene.flat ? Vec3{0.0, 0.0, 1.0} : eye);
  camera.fit();
  renderer.draw(camera, frame);

  std::vector<unsigned char> pixels(static_cast<std::size_t>(frame.width) * frame.height * 3);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, frame.width, frame.height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
  std::printf("GL: %s | error 0x%x\n", glGetString(GL_RENDERER), glGetError());

  std::ofstream out(argv[2], std::ios::binary);
  out << "P6\n" << frame.width << ' ' << frame.height << "\n255\n";
  for (int row = frame.height - 1; row >= 0; --row)
  {
    out.write(reinterpret_cast<const char*>(pixels.data()) + static_cast<std::size_t>(row) * frame.width * 3,
              static_cast<std::streamsize>(frame.width) * 3);
  }
  return EXIT_SUCCESS;
}
