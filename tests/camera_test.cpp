#include "Camera.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace
{
int failures = 0;

void expectNear(const char* what, double actual, double expected, double tolerance)
{
  if (std::abs(actual - expected) > tolerance)
  {
    std::printf("FAIL %s: got %.6f, expected %.6f\n", what, actual, expected);
    ++failures;
  }
}

struct Pixel
{
  double x;
  double y;
};

Pixel project(const Camera& camera, const Vec3& point)
{
  const Mat4 matrix = multiply(camera.projection(), camera.view());
  const double in[4] = {point.x, point.y, point.z, 1.0};
  double out[4] = {};
  for (int row = 0; row < 4; ++row)
  {
    for (int column = 0; column < 4; ++column)
    {
      out[row] += matrix[column * 4 + row] * in[column];
    }
  }
  return {(out[0] / out[3] * 0.5 + 0.5) * camera.width(), (0.5 - out[1] / out[3] * 0.5) * camera.height()};
}

Camera makeCamera(bool perspective)
{
  Camera camera;
  camera.setViewport(1200.0, 800.0);
  camera.setBounds({-60.0f, -40.0f, 0.0f}, {60.0f, 40.0f, 70.0f});
  camera.setPerspective(perspective);
  camera.lookFrom({1.0, -1.0, 1.0});
  camera.fit();
  return camera;
}

void run(bool perspective)
{
  const char* mode = perspective ? "perspective" : "orthographic";
  std::printf("-- %s\n", mode);

  Camera camera = makeCamera(perspective);
  for (int corner = 0; corner < 8; ++corner)
  {
    const Vec3 point{corner & 1 ? 60.0 : -60.0, corner & 2 ? 40.0 : -40.0, corner & 4 ? 70.0 : 0.0};
    const Pixel pixel = project(camera, point);
    if (pixel.x < 0.0 || pixel.x > 1200.0 || pixel.y < 0.0 || pixel.y > 800.0)
    {
      std::printf("FAIL fit: corner %d outside viewport (%.1f, %.1f)\n", corner, pixel.x, pixel.y);
      ++failures;
    }
  }

  const Vec3 center{0.0, 0.0, 35.0};
  Pixel before = project(camera, center);
  expectNear("fit centres the model (x)", before.x, 600.0, 0.01);
  expectNear("fit centres the model (y)", before.y, 400.0, 0.01);

  camera.pan(50.0, -30.0);
  Pixel after = project(camera, center);
  expectNear("pan follows the cursor (x)", after.x - before.x, 50.0, 0.01);
  expectNear("pan follows the cursor (y)", after.y - before.y, -30.0, 0.01);

  camera = makeCamera(perspective);
  const Vec3 anchor = center + camera.right() * 20.0 + camera.up() * 12.0;
  before = project(camera, anchor);
  camera.zoomAt(before.x, before.y, 2.5);
  after = project(camera, anchor);
  expectNear("zoom keeps the point under the cursor (x)", after.x, before.x, 0.01);
  expectNear("zoom keeps the point under the cursor (y)", after.y, before.y, 0.01);
  const Pixel edge = project(camera, anchor + camera.right() * 10.0);
  camera.zoomAt(before.x, before.y, 1.0 / 2.5);
  const Pixel restored = project(camera, anchor + camera.right() * 10.0);
  expectNear("zoom magnifies by the factor", (edge.x - after.x) / (restored.x - before.x), 2.5, 0.01);

  camera = makeCamera(perspective);
  camera.orbit(300.0, 9000.0);
  expectNear("orbit keeps Z up", camera.right().z, 0.0, 1.0e-12);
  expectNear("orbit clamps at straight down", camera.eyeDirection().z, 1.0, 1.0e-9);
  after = project(camera, center);
  expectNear("orbit pivots on the view centre (x)", after.x, 600.0, 0.01);
  expectNear("orbit pivots on the view centre (y)", after.y, 400.0, 0.01);

  camera = makeCamera(perspective);
  camera.lookFrom({0.0, -1.0, 0.0});
  expectNear("front view looks along +Y", camera.eyeDirection().y, -1.0, 1.0e-12);
  expectNear("front view has X to the right", camera.right().x, 1.0, 1.0e-12);
  expectNear("front view has Z up", camera.up().z, 1.0, 1.0e-12);
  camera.lookFrom({0.0, 0.0, 1.0});
  expectNear("top view has X to the right", camera.right().x, 1.0, 1.0e-12);
  expectNear("top view has Y up", camera.up().y, 1.0, 1.0e-9);
}
}

int main()
{
  run(false);
  run(true);

  Camera camera = makeCamera(false);
  const Pixel orthographic = project(camera, {0.0, 0.0, 35.0});
  camera.setPerspective(true);
  const Pixel perspective = project(camera, {0.0, 0.0, 35.0});
  expectNear("projection toggle keeps the centre (x)", perspective.x, orthographic.x, 0.01);
  expectNear("projection toggle keeps the centre (y)", perspective.y, orthographic.y, 0.01);

  std::printf(failures == 0 ? "camera_test: all checks passed\n" : "camera_test: %d failure(s)\n", failures);
  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
