#pragma once

#include "Camera.h"
#include "Scene.h"

#include <cstdint>
#include <optional>
#include <vector>

struct TextVertex
{
  float position[2];
  float texCoord[2];
  std::uint8_t color[4];
};

struct TextLayer
{
  std::vector<std::uint8_t> pixels;
  int width = 0;
  int height = 0;
  std::vector<TextVertex> vertices;
};

class Renderer
{
public:
  enum class Style { ShadedEdges, Shaded, Wireframe };

  struct Frame
  {
    int width = 1;
    int height = 1;
    float scale = 1.0f;
    Style style = Style::ShadedEdges;
    bool cube = true;
    bool flat = false;
  };

  Renderer() = default;
  Renderer(const Renderer&) = delete;
  Renderer& operator=(const Renderer&) = delete;

  void initialize();
  void destroy();

  void setScene(const Scene& scene);
  void clearScene();
  bool hasScene() const { return hasScene_; }

  void setCubeLabels(const std::uint8_t* rgba, int tileSize);
  void setTexts(const TextLayer& layer);

  void draw(const Camera& camera, const Frame& frame);

  static std::optional<Vec3> pickCube(const Camera& camera, double x, double y);

private:
  void resizeTarget(int width, int height);
  void drawCube(const Camera& camera, const Frame& frame);

  unsigned meshProgram_ = 0;
  unsigned edgeProgram_ = 0;
  unsigned backgroundProgram_ = 0;
  unsigned cubeProgram_ = 0;
  unsigned textProgram_ = 0;

  unsigned meshArray_ = 0;
  unsigned meshVertices_ = 0;
  unsigned meshIndices_ = 0;
  unsigned edgeArray_ = 0;
  unsigned edgeVertices_ = 0;
  unsigned emptyArray_ = 0;
  unsigned cubeArray_ = 0;
  unsigned cubeVertices_ = 0;
  unsigned cubeTexture_ = 0;
  unsigned textArray_ = 0;
  unsigned textVertices_ = 0;
  unsigned textTexture_ = 0;

  unsigned framebuffer_ = 0;
  unsigned colorBuffer_ = 0;
  unsigned depthBuffer_ = 0;
  int targetWidth_ = 0;
  int targetHeight_ = 0;

  int indexCount_ = 0;
  int segmentCount_ = 0;
  int textVertexCount_ = 0;
  bool hasScene_ = false;
  bool hasCubeLabels_ = false;
};
