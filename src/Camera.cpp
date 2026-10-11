#include "Camera.h"

#include <algorithm>
#include <cmath>

namespace
{
constexpr double kHalfPi = 1.5707963267948966;
constexpr double kTanHalfFov = 0.41421356237309503;
constexpr double kOrbitSpeed = 0.006;
constexpr double kFitMargin = 1.12;
}

Mat4 multiply(const Mat4& a, const Mat4& b)
{
  Mat4 result{};
  for (int column = 0; column < 4; ++column)
  {
    for (int row = 0; row < 4; ++row)
    {
      float sum = 0.0f;
      for (int k = 0; k < 4; ++k)
      {
        sum += a[k * 4 + row] * b[column * 4 + k];
      }
      result[column * 4 + row] = sum;
    }
  }
  return result;
}

void Camera::setBounds(const std::array<float, 3>& min, const std::array<float, 3>& max)
{
  boundsCenter_ = {(min[0] + max[0]) * 0.5, (min[1] + max[1]) * 0.5, (min[2] + max[2]) * 0.5};
  boundsHalf_ = {(max[0] - min[0]) * 0.5, (max[1] - min[1]) * 0.5, (max[2] - min[2]) * 0.5};
  boundsRadius_ = std::max(std::sqrt(dot(boundsHalf_, boundsHalf_)), 1.0e-9);
}

void Camera::setViewport(double width, double height)
{
  width_ = std::max(width, 1.0);
  height_ = std::max(height, 1.0);
}

Vec3 Camera::eyeDirection() const
{
  return {std::cos(pitch_) * std::cos(yaw_), std::cos(pitch_) * std::sin(yaw_), std::sin(pitch_)};
}

Vec3 Camera::right() const
{
  return {-std::sin(yaw_), std::cos(yaw_), 0.0};
}

Vec3 Camera::up() const
{
  return cross(right(), eyeDirection() * -1.0);
}

void Camera::orbit(double dx, double dy)
{
  yaw_ -= dx * kOrbitSpeed;
  pitch_ = std::clamp(pitch_ + dy * kOrbitSpeed, -kHalfPi, kHalfPi);
}

void Camera::pan(double dx, double dy)
{
  const double worldPerPixel = viewHeight_ / height_;
  center_ = center_ - right() * (dx * worldPerPixel) + up() * (dy * worldPerPixel);
}

void Camera::zoomAt(double x, double y, double factor)
{
  const double minHeight = boundsRadius_ * 1.0e-5;
  const double maxHeight = boundsRadius_ * 200.0;
  const double newHeight = std::clamp(viewHeight_ / factor, minHeight, maxHeight);
  const double applied = viewHeight_ / newHeight;

  const double worldPerPixel = viewHeight_ / height_;
  const double offsetX = (x - width_ * 0.5) * worldPerPixel;
  const double offsetY = (height_ * 0.5 - y) * worldPerPixel;
  center_ = center_ + (right() * offsetX + up() * offsetY) * (1.0 - 1.0 / applied);
  viewHeight_ = newHeight;
}

void Camera::fit()
{
  const Vec3 r = right();
  const Vec3 u = up();
  const Vec3 e = eyeDirection();
  const double aspect = width_ / height_;

  double extentX = 0.0;
  double extentY = 0.0;
  double distance = 0.0;
  for (int corner = 0; corner < 8; ++corner)
  {
    const Vec3 offset{(corner & 1 ? 1.0 : -1.0) * boundsHalf_.x,
                      (corner & 2 ? 1.0 : -1.0) * boundsHalf_.y,
                      (corner & 4 ? 1.0 : -1.0) * boundsHalf_.z};
    const double x = std::abs(dot(offset, r));
    const double y = std::abs(dot(offset, u));
    const double towardEye = dot(offset, e);
    extentX = std::max(extentX, x);
    extentY = std::max(extentY, y);
    distance = std::max({distance, towardEye + y / kTanHalfFov, towardEye + x / (kTanHalfFov * aspect)});
  }

  center_ = boundsCenter_;
  if (perspective_)
  {
    viewHeight_ = 2.0 * distance * kTanHalfFov * kFitMargin;
  }
  else
  {
    viewHeight_ = 2.0 * std::max(extentY, extentX / aspect) * kFitMargin;
  }
  viewHeight_ = std::max(viewHeight_, boundsRadius_ * 1.0e-5);
}

void Camera::lookFrom(const Vec3& eyeDirection)
{
  const double length = std::sqrt(dot(eyeDirection, eyeDirection));
  if (length < 1.0e-12)
  {
    return;
  }
  const Vec3 e = eyeDirection * (1.0 / length);
  pitch_ = std::asin(std::clamp(e.z, -1.0, 1.0));
  yaw_ = std::abs(e.z) > 0.999999 ? -kHalfPi : std::atan2(e.y, e.x);
}

double Camera::eyeDistance() const
{
  if (perspective_)
  {
    return viewHeight_ / (2.0 * kTanHalfFov);
  }
  const Vec3 offset = center_ - boundsCenter_;
  return std::sqrt(dot(offset, offset)) + 2.0 * boundsRadius_ + viewHeight_;
}

Mat4 Camera::rotation() const
{
  const Vec3 r = right();
  const Vec3 u = up();
  const Vec3 e = eyeDirection();
  return {static_cast<float>(r.x), static_cast<float>(u.x), static_cast<float>(e.x), 0.0f,
          static_cast<float>(r.y), static_cast<float>(u.y), static_cast<float>(e.y), 0.0f,
          static_cast<float>(r.z), static_cast<float>(u.z), static_cast<float>(e.z), 0.0f,
          0.0f,                    0.0f,                    0.0f,                    1.0f};
}

Mat4 Camera::view() const
{
  const Vec3 r = right();
  const Vec3 u = up();
  const Vec3 e = eyeDirection();
  const Vec3 eye = center_ + e * eyeDistance();

  Mat4 matrix = rotation();
  matrix[12] = static_cast<float>(-dot(r, eye));
  matrix[13] = static_cast<float>(-dot(u, eye));
  matrix[14] = static_cast<float>(-dot(e, eye));
  return matrix;
}

Mat4 Camera::projection() const
{
  const Vec3 e = eyeDirection();
  const double distance = eyeDistance();
  const Vec3 eye = center_ + e * distance;
  const double centerDepth = dot(eye - boundsCenter_, e);

  double nearPlane = centerDepth - boundsRadius_ * 1.01;
  double farPlane = centerDepth + boundsRadius_ * 1.01;
  const double aspect = width_ / height_;

  Mat4 matrix{};
  if (perspective_)
  {
    nearPlane = std::max(nearPlane, distance * 0.02);
    farPlane = std::max(farPlane, nearPlane * 2.0);
    matrix[0] = static_cast<float>(1.0 / (kTanHalfFov * aspect));
    matrix[5] = static_cast<float>(1.0 / kTanHalfFov);
    matrix[10] = static_cast<float>(-(farPlane + nearPlane) / (farPlane - nearPlane));
    matrix[11] = -1.0f;
    matrix[14] = static_cast<float>(-2.0 * farPlane * nearPlane / (farPlane - nearPlane));
  }
  else
  {
    const double halfHeight = viewHeight_ * 0.5;
    matrix[0] = static_cast<float>(1.0 / (halfHeight * aspect));
    matrix[5] = static_cast<float>(1.0 / halfHeight);
    matrix[10] = static_cast<float>(-2.0 / (farPlane - nearPlane));
    matrix[14] = static_cast<float>(-(farPlane + nearPlane) / (farPlane - nearPlane));
    matrix[15] = 1.0f;
  }
  return matrix;
}
