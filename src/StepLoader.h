#pragma once

#include <TDF_LabelSequence.hxx>
#include <TDocStd_Document.hxx>
#include <TopoDS_Shape.hxx>

#include <atomic>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>

struct MeshQuality
{
  double linearDeflection = 0.0;
  double angularDeflection = 0.0;
};

struct StepModel
{
  Handle(TDocStd_Document) document;
  TDF_LabelSequence roots;
  TopoDS_Shape shape;
  MeshQuality quality;
  std::size_t triangles = 0;
  double parseMs = 0.0;
  double transferMs = 0.0;
  double meshMs = 0.0;
};

struct StepLoadResult
{
  std::optional<StepModel> model;
  std::string error;
};

struct LoadProgress
{
  enum class Stage { Reading, Parsing, Converting, Tessellating };

  std::atomic<Stage> stage{Stage::Reading};
  std::atomic<float> fraction{0.0f};
};

bool hasStepExtension(const std::filesystem::path& path);

StepLoadResult loadStep(const std::filesystem::path& path, LoadProgress& progress);
