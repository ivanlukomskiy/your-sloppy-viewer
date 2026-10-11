#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

struct Vertex
{
  float position[3];
  float normal[3];
  std::uint8_t color[4];
};

struct Scene
{
  std::vector<Vertex> vertices;
  std::vector<std::uint32_t> indices;
  std::vector<float> edges;
  std::array<float, 3> boundsMin{};
  std::array<float, 3> boundsMax{};

  std::size_t triangleCount() const { return indices.size() / 3; }
};
