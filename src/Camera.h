#pragma once

#include <array>

struct Vec3
{
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
};

inline Vec3 operator+(const Vec3& a, const Vec3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator*(const Vec3& a, double s) { return {a.x * s, a.y * s, a.z * s}; }
inline double dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(const Vec3& a, const Vec3& b)
{
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

using Mat4 = std::array<float, 16>;

Mat4 multiply(const Mat4& a, const Mat4& b);

class Camera
{
public:
  void setBounds(const std::array<float, 3>& min, const std::array<float, 3>& max);
  void setViewport(double width, double height);

  void orbit(double dx, double dy);
  void pan(double dx, double dy);
  void zoomAt(double x, double y, double factor);
  void fit();
  void lookFrom(const Vec3& eyeDirection);
  void setPerspective(bool perspective) { perspective_ = perspective; }

  bool perspective() const { return perspective_; }
  double width() const { return width_; }
  double height() const { return height_; }

  Vec3 eyeDirection() const;
  Vec3 right() const;
  Vec3 up() const;

  Mat4 view() const;
  Mat4 projection() const;
  Mat4 rotation() const;

private:
  double eyeDistance() const;

  Vec3 boundsCenter_;
  Vec3 boundsHalf_{1.0, 1.0, 1.0};
  double boundsRadius_ = 1.0;

  Vec3 center_;
  double yaw_ = -0.78539816339744831;
  double pitch_ = 0.61547970867038734;
  double viewHeight_ = 2.0;
  double width_ = 1.0;
  double height_ = 1.0;
  bool perspective_ = false;
};
