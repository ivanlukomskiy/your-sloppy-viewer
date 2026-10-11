#include "Renderer.h"

#include <epoxy/gl.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
constexpr double kCubeSize = 92.0;
constexpr double kCubeMargin = 12.0;
constexpr double kCubeExtent = 1.95;
constexpr int kSamples = 4;

const char* kMeshVertex = R"(#version 330 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec4 aColor;
uniform mat4 uView;
uniform mat4 uProjection;
out vec3 vNormal;
out vec3 vViewPosition;
out vec3 vColor;
void main()
{
  vec4 viewPosition = uView * vec4(aPosition, 1.0);
  vNormal = mat3(uView) * aNormal;
  vViewPosition = viewPosition.xyz;
  vColor = aColor.rgb;
  gl_Position = uProjection * viewPosition;
}
)";

const char* kMeshFragment = R"(#version 330 core
in vec3 vNormal;
in vec3 vViewPosition;
in vec3 vColor;
uniform bool uPerspective;
out vec4 fragColor;
const vec3 kKeyLight = normalize(vec3(-0.40, 0.65, 1.0));
const vec3 kFillLight = normalize(vec3(0.85, -0.30, 0.50));
void main()
{
  vec3 toEye = uPerspective ? normalize(-vViewPosition) : vec3(0.0, 0.0, 1.0);
  vec3 normal = normalize(vNormal);
  if (dot(normal, toEye) < 0.0)
  {
    normal = -normal;
  }
  float key = max(dot(normal, kKeyLight), 0.0);
  float fill = max(dot(normal, kFillLight), 0.0);
  float keySpecular = pow(max(dot(normal, normalize(kKeyLight + toEye)), 0.0), 45.0);
  float fillSpecular = pow(max(dot(normal, normalize(kFillLight + toEye)), 0.0), 45.0);
  vec3 color = vColor * (0.26 + 0.72 * (0.95 * key + 0.30 * fill))
             + vec3(0.22, 0.23, 0.25) * (0.95 * keySpecular + 0.30 * fillSpecular);
  fragColor = vec4(color, 1.0);
}
)";

const char* kEdgeVertex = R"(#version 330 core
layout(location = 0) in vec3 aStart;
layout(location = 1) in vec3 aEnd;
layout(location = 2) in vec4 aColor;
layout(location = 3) in float aLayer;
uniform mat4 uViewProjection;
uniform vec2 uViewport;
uniform float uWidth;
uniform vec3 uColor;
uniform int uHidden[8];
bool hiddenLayer(float layer)
{
  int index = int(layer + 0.5);
  return ((uHidden[index >> 5] >> (index & 31)) & 1) != 0;
}
out vec3 vColor;
void main()
{
  if (hiddenLayer(aLayer))
  {
    gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
    return;
  }
  vColor = mix(uColor, aColor.rgb, aColor.a);
  vec4 a = uViewProjection * vec4(aStart, 1.0);
  vec4 b = uViewProjection * vec4(aEnd, 1.0);
  float da = a.z + a.w;
  float db = b.z + b.w;
  if (da < 0.0 && db < 0.0)
  {
    gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
    return;
  }
  if (da < 0.0)
  {
    a = mix(a, b, da / (da - db));
  }
  else if (db < 0.0)
  {
    b = mix(b, a, db / (db - da));
  }
  vec2 screenA = a.xy / a.w * uViewport;
  vec2 screenB = b.xy / b.w * uViewport;
  vec2 direction = screenB - screenA;
  float len = length(direction);
  direction = len > 1.0e-6 ? direction / len : vec2(1.0, 0.0);
  vec2 normal = vec2(-direction.y, direction.x);
  float along = float(gl_VertexID & 1);
  float side = float(gl_VertexID >> 1) * 2.0 - 1.0;
  vec4 position = mix(a, b, along);
  position.xy += normal * side * uWidth / uViewport * position.w;
  gl_Position = position;
}
)";

const char* kEdgeFragment = R"(#version 330 core
in vec3 vColor;
out vec4 fragColor;
void main()
{
  fragColor = vec4(vColor, 1.0);
}
)";

const char* kTextVertex = R"(#version 330 core
layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec2 aTexCoord;
layout(location = 2) in vec4 aColor;
layout(location = 3) in float aLayer;
uniform mat4 uViewProjection;
uniform int uHidden[8];
bool hiddenLayer(float layer)
{
  int index = int(layer + 0.5);
  return ((uHidden[index >> 5] >> (index & 31)) & 1) != 0;
}
out vec2 vTexCoord;
out vec3 vColor;
void main()
{
  if (hiddenLayer(aLayer))
  {
    gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
    return;
  }
  vTexCoord = aTexCoord;
  vColor = aColor.rgb;
  gl_Position = uViewProjection * vec4(aPosition, 0.0, 1.0);
}
)";

const char* kTextFragment = R"(#version 330 core
in vec2 vTexCoord;
in vec3 vColor;
uniform sampler2D uAtlas;
out vec4 fragColor;
void main()
{
  fragColor = vec4(vColor, texture(uAtlas, vTexCoord).r);
}
)";

const char* kBackgroundVertex = R"(#version 330 core
out float vHeight;
void main()
{
  vec2 position = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2)) * 2.0 - 1.0;
  vHeight = position.y * 0.5 + 0.5;
  gl_Position = vec4(position, 0.0, 1.0);
}
)";

const char* kBackgroundFragment = R"(#version 330 core
in float vHeight;
out vec4 fragColor;
void main()
{
  vec3 color = mix(vec3(0.065, 0.068, 0.075), vec3(0.185, 0.195, 0.215), clamp(vHeight, 0.0, 1.0));
  float noise = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453) - 0.5;
  fragColor = vec4(color + noise / 255.0, 1.0);
}
)";

const char* kCubeVertex = R"(#version 330 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;
uniform mat4 uRotation;
uniform float uExtent;
out vec3 vNormal;
out vec2 vTexCoord;
void main()
{
  vec4 rotated = uRotation * vec4(aPosition, 1.0);
  vNormal = mat3(uRotation) * aNormal;
  vTexCoord = aTexCoord;
  gl_Position = vec4(rotated.xy / uExtent, -rotated.z / 4.0, 1.0);
}
)";

const char* kCubeFragment = R"(#version 330 core
in vec3 vNormal;
in vec2 vTexCoord;
uniform sampler2D uLabels;
uniform bool uHasLabels;
out vec4 fragColor;
void main()
{
  vec3 normal = normalize(vNormal);
  float light = 0.55 + 0.45 * max(dot(normal, normalize(vec3(-0.35, 0.55, 1.0))), 0.0);
  vec2 edge = min(vTexCoord * vec2(6.0, 1.0) - floor(vTexCoord * vec2(6.0, 1.0)),
                  1.0 - (vTexCoord * vec2(6.0, 1.0) - floor(vTexCoord * vec2(6.0, 1.0))));
  float border = 1.0 - smoothstep(0.0, 0.05, min(edge.x, edge.y));
  vec3 color = mix(vec3(0.36, 0.39, 0.45), vec3(0.20, 0.22, 0.26), border) * light;
  if (uHasLabels)
  {
    float text = texture(uLabels, vTexCoord).r;
    color = mix(color, vec3(0.93, 0.94, 0.96), text);
  }
  fragColor = vec4(color, 1.0);
}
)";

unsigned compile(GLenum type, const char* source)
{
  const unsigned shader = glCreateShader(type);
  glShaderSource(shader, 1, &source, nullptr);
  glCompileShader(shader);
  int ok = 0;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
  if (ok == 0)
  {
    char log[2048] = {};
    glGetShaderInfoLog(shader, sizeof log, nullptr, log);
    glDeleteShader(shader);
    throw std::runtime_error(std::string("shader compilation failed: ") + log);
  }
  return shader;
}

unsigned link(const char* vertexSource, const char* fragmentSource)
{
  const unsigned vertex = compile(GL_VERTEX_SHADER, vertexSource);
  const unsigned fragment = compile(GL_FRAGMENT_SHADER, fragmentSource);
  const unsigned program = glCreateProgram();
  glAttachShader(program, vertex);
  glAttachShader(program, fragment);
  glLinkProgram(program);
  glDeleteShader(vertex);
  glDeleteShader(fragment);
  int ok = 0;
  glGetProgramiv(program, GL_LINK_STATUS, &ok);
  if (ok == 0)
  {
    char log[2048] = {};
    glGetProgramInfoLog(program, sizeof log, nullptr, log);
    glDeleteProgram(program);
    throw std::runtime_error(std::string("shader linking failed: ") + log);
  }
  return program;
}

struct CubeFace
{
  Vec3 normal;
  Vec3 right;
  Vec3 up;
};

const CubeFace kCubeFaces[6] = {
  {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}},  {{-1, 0, 0}, {0, -1, 0}, {0, 0, 1}},
  {{0, 1, 0}, {-1, 0, 0}, {0, 0, 1}}, {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}},
  {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}},  {{0, 0, -1}, {1, 0, 0}, {0, -1, 0}},
};

std::vector<float> cubeGeometry()
{
  std::vector<float> data;
  const double corners[6][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, -1}, {1, 1}, {-1, 1}};
  for (int face = 0; face < 6; ++face)
  {
    const CubeFace& f = kCubeFaces[face];
    for (const auto& corner : corners)
    {
      const Vec3 position = f.normal + f.right * corner[0] + f.up * corner[1];
      data.insert(data.end(),
                  {static_cast<float>(position.x), static_cast<float>(position.y),
                   static_cast<float>(position.z), static_cast<float>(f.normal.x),
                   static_cast<float>(f.normal.y), static_cast<float>(f.normal.z),
                   static_cast<float>((face + (corner[0] + 1.0) * 0.5) / 6.0),
                   static_cast<float>((1.0 - corner[1]) * 0.5)});
    }
  }
  return data;
}
}

void Renderer::initialize()
{
  meshProgram_ = link(kMeshVertex, kMeshFragment);
  edgeProgram_ = link(kEdgeVertex, kEdgeFragment);
  backgroundProgram_ = link(kBackgroundVertex, kBackgroundFragment);
  cubeProgram_ = link(kCubeVertex, kCubeFragment);
  textProgram_ = link(kTextVertex, kTextFragment);

  glGenVertexArrays(1, &meshArray_);
  glGenBuffers(1, &meshVertices_);
  glGenBuffers(1, &meshIndices_);
  glBindVertexArray(meshArray_);
  glBindBuffer(GL_ARRAY_BUFFER, meshVertices_);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, meshIndices_);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                        reinterpret_cast<void*>(offsetof(Vertex, position)));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                        reinterpret_cast<void*>(offsetof(Vertex, normal)));
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex),
                        reinterpret_cast<void*>(offsetof(Vertex, color)));

  glGenVertexArrays(1, &edgeArray_);
  glGenBuffers(1, &edgeVertices_);
  glBindVertexArray(edgeArray_);
  glBindBuffer(GL_ARRAY_BUFFER, edgeVertices_);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(EdgeSegment),
                        reinterpret_cast<void*>(offsetof(EdgeSegment, start)));
  glVertexAttribDivisor(0, 1);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(EdgeSegment),
                        reinterpret_cast<void*>(offsetof(EdgeSegment, end)));
  glVertexAttribDivisor(1, 1);
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(EdgeSegment),
                        reinterpret_cast<void*>(offsetof(EdgeSegment, color)));
  glVertexAttribDivisor(2, 1);
  glEnableVertexAttribArray(3);
  glVertexAttribPointer(3, 1, GL_UNSIGNED_BYTE, GL_FALSE, sizeof(EdgeSegment),
                        reinterpret_cast<void*>(offsetof(EdgeSegment, layer)));
  glVertexAttribDivisor(3, 1);

  glGenVertexArrays(1, &textArray_);
  glGenBuffers(1, &textVertices_);
  glBindVertexArray(textArray_);
  glBindBuffer(GL_ARRAY_BUFFER, textVertices_);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(TextVertex),
                        reinterpret_cast<void*>(offsetof(TextVertex, position)));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(TextVertex),
                        reinterpret_cast<void*>(offsetof(TextVertex, texCoord)));
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(TextVertex),
                        reinterpret_cast<void*>(offsetof(TextVertex, color)));
  glEnableVertexAttribArray(3);
  glVertexAttribPointer(3, 1, GL_UNSIGNED_BYTE, GL_FALSE, sizeof(TextVertex),
                        reinterpret_cast<void*>(offsetof(TextVertex, layer)));

  glGenVertexArrays(1, &emptyArray_);

  const std::vector<float> cube = cubeGeometry();
  glGenVertexArrays(1, &cubeArray_);
  glGenBuffers(1, &cubeVertices_);
  glBindVertexArray(cubeArray_);
  glBindBuffer(GL_ARRAY_BUFFER, cubeVertices_);
  glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(cube.size() * sizeof(float)), cube.data(),
               GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), nullptr);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                        reinterpret_cast<void*>(3 * sizeof(float)));
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                        reinterpret_cast<void*>(6 * sizeof(float)));
  glBindVertexArray(0);

  glGenTextures(1, &cubeTexture_);
  glGenTextures(1, &textTexture_);
  glGenFramebuffers(1, &framebuffer_);
  glGenRenderbuffers(1, &colorBuffer_);
  glGenRenderbuffers(1, &depthBuffer_);
}

void Renderer::destroy()
{
  glDeleteProgram(meshProgram_);
  glDeleteProgram(edgeProgram_);
  glDeleteProgram(backgroundProgram_);
  glDeleteProgram(cubeProgram_);
  glDeleteProgram(textProgram_);
  const unsigned arrays[] = {meshArray_, edgeArray_, emptyArray_, cubeArray_, textArray_};
  glDeleteVertexArrays(5, arrays);
  const unsigned buffers[] = {meshVertices_, meshIndices_, edgeVertices_, cubeVertices_, textVertices_};
  glDeleteBuffers(5, buffers);
  glDeleteTextures(1, &cubeTexture_);
  glDeleteTextures(1, &textTexture_);
  textVertexCount_ = 0;
  glDeleteFramebuffers(1, &framebuffer_);
  const unsigned renderbuffers[] = {colorBuffer_, depthBuffer_};
  glDeleteRenderbuffers(2, renderbuffers);
  targetWidth_ = 0;
  targetHeight_ = 0;
  indexCount_ = 0;
  segmentCount_ = 0;
  hasScene_ = false;
  hasCubeLabels_ = false;
}

void Renderer::setScene(const Scene& scene)
{
  glBindBuffer(GL_ARRAY_BUFFER, meshVertices_);
  glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(scene.vertices.size() * sizeof(Vertex)),
               scene.vertices.data(), GL_STATIC_DRAW);
  glBindVertexArray(meshArray_);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER,
               static_cast<GLsizeiptr>(scene.indices.size() * sizeof(std::uint32_t)),
               scene.indices.data(), GL_STATIC_DRAW);
  glBindVertexArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, edgeVertices_);
  glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(scene.edges.size() * sizeof(EdgeSegment)),
               scene.edges.data(), GL_STATIC_DRAW);
  glBindBuffer(GL_ARRAY_BUFFER, 0);

  indexCount_ = static_cast<int>(scene.indices.size());
  segmentCount_ = static_cast<int>(scene.edges.size());
  textVertexCount_ = 0;
  hasScene_ = true;
}

void Renderer::clearScene()
{
  glBindBuffer(GL_ARRAY_BUFFER, meshVertices_);
  glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_STATIC_DRAW);
  glBindVertexArray(meshArray_);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, 0, nullptr, GL_STATIC_DRAW);
  glBindVertexArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, edgeVertices_);
  glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_STATIC_DRAW);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  indexCount_ = 0;
  segmentCount_ = 0;
  hasScene_ = false;
}

void Renderer::setCubeLabels(const std::uint8_t* rgba, int tileSize)
{
  glBindTexture(GL_TEXTURE_2D, cubeTexture_);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, tileSize * 6, tileSize, 0, GL_RED, GL_UNSIGNED_BYTE, rgba);
  glGenerateMipmap(GL_TEXTURE_2D);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindTexture(GL_TEXTURE_2D, 0);
  hasCubeLabels_ = true;
}

void Renderer::setTexts(const TextLayer& layer)
{
  textVertexCount_ = static_cast<int>(layer.vertices.size());
  if (textVertexCount_ == 0)
  {
    return;
  }
  glBindBuffer(GL_ARRAY_BUFFER, textVertices_);
  glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(layer.vertices.size() * sizeof(TextVertex)),
               layer.vertices.data(), GL_STATIC_DRAW);
  glBindBuffer(GL_ARRAY_BUFFER, 0);

  glBindTexture(GL_TEXTURE_2D, textTexture_);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, layer.width, layer.height, 0, GL_RED, GL_UNSIGNED_BYTE,
               layer.pixels.data());
  glGenerateMipmap(GL_TEXTURE_2D);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindTexture(GL_TEXTURE_2D, 0);
}

void Renderer::resizeTarget(int width, int height)
{
  if (width == targetWidth_ && height == targetHeight_)
  {
    return;
  }
  targetWidth_ = width;
  targetHeight_ = height;

  int maxSamples = 0;
  glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
  const int samples = std::min(kSamples, maxSamples);

  glBindRenderbuffer(GL_RENDERBUFFER, colorBuffer_);
  glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA8, width, height);
  glBindRenderbuffer(GL_RENDERBUFFER, depthBuffer_);
  glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH_COMPONENT24, width, height);
  glBindRenderbuffer(GL_RENDERBUFFER, 0);

  glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, colorBuffer_);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthBuffer_);
}

void Renderer::draw(const Camera& camera, const Frame& frame)
{
  int target = 0;
  glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &target);

  resizeTarget(frame.width, frame.height);
  glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
  glViewport(0, 0, frame.width, frame.height);
  glDisable(GL_SCISSOR_TEST);
  glDisable(GL_BLEND);
  glDisable(GL_CULL_FACE);
  glEnable(GL_MULTISAMPLE);
  glDepthMask(GL_TRUE);
  glClearDepth(1.0);
  glClear(GL_DEPTH_BUFFER_BIT);

  glDisable(GL_DEPTH_TEST);
  glUseProgram(backgroundProgram_);
  glBindVertexArray(emptyArray_);
  glDrawArrays(GL_TRIANGLES, 0, 3);

  if (hasScene_)
  {
    const Mat4 view = camera.view();
    const Mat4 projection = camera.projection();
    const Mat4 viewProjection = multiply(projection, view);
    const bool faces = !frame.flat && frame.style != Style::Wireframe && indexCount_ > 0;
    const bool edges = frame.flat || frame.style != Style::Shaded || indexCount_ == 0;

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);

    if (faces)
    {
      glEnable(GL_POLYGON_OFFSET_FILL);
      glPolygonOffset(1.0f, 1.0f);
      glUseProgram(meshProgram_);
      glUniformMatrix4fv(glGetUniformLocation(meshProgram_, "uView"), 1, GL_FALSE, view.data());
      glUniformMatrix4fv(glGetUniformLocation(meshProgram_, "uProjection"), 1, GL_FALSE,
                         projection.data());
      glUniform1i(glGetUniformLocation(meshProgram_, "uPerspective"), camera.perspective() ? 1 : 0);
      glBindVertexArray(meshArray_);
      glDrawElements(GL_TRIANGLES, indexCount_, GL_UNSIGNED_INT, nullptr);
      glDisable(GL_POLYGON_OFFSET_FILL);
    }

    if (edges && segmentCount_ > 0)
    {
      const bool onFaces = faces;
      glUseProgram(edgeProgram_);
      glUniformMatrix4fv(glGetUniformLocation(edgeProgram_, "uViewProjection"), 1, GL_FALSE,
                         viewProjection.data());
      glUniform1iv(glGetUniformLocation(edgeProgram_, "uHidden"), 8, frame.hiddenLayers.data());
      glUniform2f(glGetUniformLocation(edgeProgram_, "uViewport"), static_cast<float>(frame.width),
                  static_cast<float>(frame.height));
      glUniform1f(glGetUniformLocation(edgeProgram_, "uWidth"),
                  std::max(1.0f, (frame.flat ? 0.6f : onFaces ? 0.7f : 0.8f) * frame.scale));
      if (onFaces)
      {
        glUniform3f(glGetUniformLocation(edgeProgram_, "uColor"), 0.09f, 0.10f, 0.12f);
      }
      else
      {
        glUniform3f(glGetUniformLocation(edgeProgram_, "uColor"), 0.62f, 0.72f, 0.84f);
      }
      glBindVertexArray(edgeArray_);
      glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, segmentCount_);
    }

    if (frame.flat && textVertexCount_ > 0)
    {
      glDisable(GL_DEPTH_TEST);
      glEnable(GL_BLEND);
      glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
      glUseProgram(textProgram_);
      glUniformMatrix4fv(glGetUniformLocation(textProgram_, "uViewProjection"), 1, GL_FALSE,
                         viewProjection.data());
      glUniform1iv(glGetUniformLocation(textProgram_, "uHidden"), 8, frame.hiddenLayers.data());
      glUniform1i(glGetUniformLocation(textProgram_, "uAtlas"), 0);
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, textTexture_);
      glBindVertexArray(textArray_);
      glDrawArrays(GL_TRIANGLES, 0, textVertexCount_);
      glBindTexture(GL_TEXTURE_2D, 0);
      glDisable(GL_BLEND);
    }

    if (frame.cube && !frame.flat)
    {
      drawCube(camera, frame);
    }
  }

  glBindVertexArray(0);
  glUseProgram(0);
  glDisable(GL_DEPTH_TEST);

  glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer_);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<unsigned>(target));
  glBlitFramebuffer(0, 0, frame.width, frame.height, 0, 0, frame.width, frame.height,
                    GL_COLOR_BUFFER_BIT, GL_NEAREST);
  glBindFramebuffer(GL_FRAMEBUFFER, static_cast<unsigned>(target));
}

void Renderer::drawCube(const Camera& camera, const Frame& frame)
{
  const int size = static_cast<int>(std::lround(kCubeSize * frame.scale));
  const int margin = static_cast<int>(std::lround(kCubeMargin * frame.scale));
  const int x = frame.width - margin - size;
  const int y = frame.height - margin - size;
  if (x < 0 || y < 0)
  {
    return;
  }

  glViewport(x, y, size, size);
  glEnable(GL_SCISSOR_TEST);
  glScissor(x, y, size, size);
  glClear(GL_DEPTH_BUFFER_BIT);
  glDisable(GL_SCISSOR_TEST);
  glEnable(GL_DEPTH_TEST);

  const Mat4 rotation = camera.rotation();
  glUseProgram(cubeProgram_);
  glUniformMatrix4fv(glGetUniformLocation(cubeProgram_, "uRotation"), 1, GL_FALSE, rotation.data());
  glUniform1f(glGetUniformLocation(cubeProgram_, "uExtent"), static_cast<float>(kCubeExtent));
  glUniform1i(glGetUniformLocation(cubeProgram_, "uHasLabels"), hasCubeLabels_ ? 1 : 0);
  glUniform1i(glGetUniformLocation(cubeProgram_, "uLabels"), 0);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, cubeTexture_);
  glBindVertexArray(cubeArray_);
  glDrawArrays(GL_TRIANGLES, 0, 36);
  glBindTexture(GL_TEXTURE_2D, 0);
  glViewport(0, 0, frame.width, frame.height);
}

std::optional<Vec3> Renderer::pickCube(const Camera& camera, double x, double y)
{
  const double half = kCubeSize * 0.5;
  const double centerX = camera.width() - kCubeMargin - half;
  const double centerY = kCubeMargin + half;
  const double nx = (x - centerX) / half;
  const double ny = (centerY - y) / half;
  if (std::abs(nx) > 1.0 || std::abs(ny) > 1.0)
  {
    return std::nullopt;
  }

  const Vec3 e = camera.eyeDirection();
  const Vec3 origin = camera.right() * (nx * kCubeExtent) + camera.up() * (ny * kCubeExtent) + e * 10.0;
  const Vec3 direction = e * -1.0;
  const double o[3] = {origin.x, origin.y, origin.z};
  const double d[3] = {direction.x, direction.y, direction.z};

  double entry = -1.0e30;
  double exit = 1.0e30;
  int entryAxis = -1;
  for (int axis = 0; axis < 3; ++axis)
  {
    if (std::abs(d[axis]) < 1.0e-12)
    {
      if (std::abs(o[axis]) > 1.0)
      {
        return std::nullopt;
      }
      continue;
    }
    double t0 = (-1.0 - o[axis]) / d[axis];
    double t1 = (1.0 - o[axis]) / d[axis];
    if (t0 > t1)
    {
      std::swap(t0, t1);
    }
    if (t0 > entry)
    {
      entry = t0;
      entryAxis = axis;
    }
    exit = std::min(exit, t1);
  }
  if (entryAxis < 0 || entry > exit)
  {
    return std::nullopt;
  }

  Vec3 normal;
  const double sign = d[entryAxis] > 0.0 ? -1.0 : 1.0;
  (entryAxis == 0 ? normal.x : entryAxis == 1 ? normal.y : normal.z) = sign;
  return normal;
}
