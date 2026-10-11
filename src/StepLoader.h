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

struct StepLoadResult
{
  std::optional<Scene> scene;
  LoadTimings timings;
  std::string error;
};

struct LoadProgress
{
  enum class Stage { Reading, Parsing, Converting, Tessellating, Preparing };

  std::atomic<Stage> stage{Stage::Reading};
  std::atomic<float> fraction{0.0f};
};

bool hasStepExtension(const std::filesystem::path& path);

StepLoadResult loadStep(const std::filesystem::path& path, LoadProgress& progress);
