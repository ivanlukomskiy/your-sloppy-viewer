#pragma once

#include "Scene.h"

#include <atomic>
#include <filesystem>
#include <optional>
#include <string>

struct LoadTimings
{
  double parseMs = 0.0;
  double transferMs = 0.0;
  double meshMs = 0.0;
  double buildMs = 0.0;
};

struct LoadResult
{
  std::optional<Scene> scene;
  LoadTimings timings;
  std::string error;
  std::string note;
};

struct LoadProgress
{
  enum class Stage { Reading, Parsing, Converting, Tessellating, Preparing };

  std::atomic<Stage> stage{Stage::Reading};
  std::atomic<float> fraction{0.0f};
};

bool isSupportedFile(const std::filesystem::path& path);

LoadResult loadModel(const std::filesystem::path& path, LoadProgress& progress);
