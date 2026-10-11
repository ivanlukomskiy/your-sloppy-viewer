#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct Vertex
{
  float position[3];
  float normal[3];
  std::uint8_t color[4];
};

struct EdgeSegment
{
  float start[3];
  float end[3];
  std::uint8_t color[4];
  std::uint8_t layer = 0;
  std::uint8_t padding[3] = {};
};

struct SceneLayer
{
  std::string name;
  std::uint8_t color[4] = {255, 255, 255, 255};
  bool visible = true;
};

struct TextItem
{
  enum class Horizontal { Left, Center, Right };
  enum class Vertical { Baseline, Bottom, Middle, Top };

  std::string text;
  float x = 0.0f;
  float y = 0.0f;
  float height = 1.0f;
  float rotation = 0.0f;
  Horizontal horizontal = Horizontal::Left;
  Vertical vertical = Vertical::Baseline;
  std::uint8_t color[4] = {255, 255, 255, 255};
  std::uint8_t layer = 0;
};

struct Scene
{
  std::vector<Vertex> vertices;
  std::vector<std::uint32_t> indices;
  std::vector<EdgeSegment> edges;
  std::vector<TextItem> texts;
  std::vector<SceneLayer> layers;
  std::array<float, 3> boundsMin{};
  std::array<float, 3> boundsMax{};
  std::array<double, 3> size{};
  std::string unit = "mm";
  bool flat = false;

  std::size_t triangleCount() const { return indices.size() / 3; }
};
