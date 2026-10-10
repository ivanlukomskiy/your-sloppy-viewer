#include "StepLoader.h"

#include <BRepBndLib.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <IMeshTools_Parameters.hxx>
#include <Poly_Triangulation.hxx>
#include <STEPCAFControl_Reader.hxx>
#include <Standard_Failure.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <exception>
#include <system_error>

namespace
{
using Clock = std::chrono::steady_clock;

double millisecondsSince(Clock::time_point start)
{
  return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

const char* describeReadStatus(IFSelect_ReturnStatus status)
{
  switch (status)
  {
    case IFSelect_RetVoid:
      return "the file contains no STEP data";
    case IFSelect_RetError:
    case IFSelect_RetFail:
      return "not a valid STEP file";
    case IFSelect_RetStop:
      return "reading was interrupted";
    default:
      return "unknown reader error";
  }
}

std::size_t countTriangles(const TopoDS_Shape& shape)
{
  std::size_t count = 0;
  for (TopExp_Explorer faces(shape, TopAbs_FACE); faces.More(); faces.Next())
  {
    TopLoc_Location location;
    const Handle(Poly_Triangulation)& triangulation =
      BRep_Tool::Triangulation(TopoDS::Face(faces.Current()), location);
    if (!triangulation.IsNull())
    {
      count += static_cast<std::size_t>(triangulation->NbTriangles());
    }
  }
  return count;
}

StepLoadResult fail(std::string message)
{
  return {std::nullopt, std::move(message)};
}

StepLoadResult loadChecked(const std::filesystem::path& path)
{
  StepModel model;

  STEPCAFControl_Reader reader;
  reader.SetColorMode(true);
  reader.SetNameMode(false);
  reader.SetLayerMode(false);
  reader.SetPropsMode(false);

  auto start = Clock::now();
  const IFSelect_ReturnStatus status = reader.ReadFile(path.c_str());
  model.parseMs = millisecondsSince(start);
  if (status != IFSelect_RetDone)
  {
    return fail(describeReadStatus(status));
  }

  start = Clock::now();
  model.document = new TDocStd_Document("StepView");
  if (!reader.Transfer(model.document))
  {
    return fail("the STEP entities could not be converted to geometry");
  }

  XCAFDoc_DocumentTool::ShapeTool(model.document->Main())->GetFreeShapes(model.roots);

  BRep_Builder builder;
  TopoDS_Compound compound;
  builder.MakeCompound(compound);
  bool hasGeometry = false;
  for (const TDF_Label& root : model.roots)
  {
    const TopoDS_Shape shape = XCAFDoc_ShapeTool::GetShape(root);
    if (!shape.IsNull())
    {
      builder.Add(compound, shape);
      hasGeometry = true;
    }
  }
  model.transferMs = millisecondsSince(start);

  Bnd_Box bounds;
  if (hasGeometry)
  {
    BRepBndLib::Add(compound, bounds, false);
  }
  if (bounds.IsVoid())
  {
    return fail("the file contains no displayable geometry");
  }
  model.shape = compound;

  TopTools_IndexedMapOfShape faces;
  TopExp::MapShapes(model.shape, TopAbs_FACE, faces);

  const double diagonal = std::sqrt(bounds.SquareExtent());
  model.quality.linearDeflection = std::max(diagonal * 4.0e-4, 1.0e-6);
  model.quality.angularDeflection = std::clamp(0.15 * std::sqrt(faces.Extent() / 1500.0), 0.15, 0.40);

  start = Clock::now();
  IMeshTools_Parameters parameters;
  parameters.Deflection = model.quality.linearDeflection;
  parameters.Angle = model.quality.angularDeflection;
  parameters.InParallel = true;
  BRepMesh_IncrementalMesh mesher(model.shape, parameters);
  model.meshMs = millisecondsSince(start);

  model.triangles = countTriangles(model.shape);
  if (model.triangles == 0 && !TopExp_Explorer(model.shape, TopAbs_EDGE).More())
  {
    return fail("the file contains no displayable geometry");
  }

  return {std::move(model), {}};
}
}

bool hasStepExtension(const std::filesystem::path& path)
{
  std::string extension = path.extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return extension == ".step" || extension == ".stp";
}

StepLoadResult loadStep(const std::filesystem::path& path)
{
  std::error_code ec;
  if (!std::filesystem::exists(path, ec))
  {
    return fail("file not found");
  }
  if (!std::filesystem::is_regular_file(path, ec))
  {
    return fail("not a regular file");
  }
  if (!hasStepExtension(path))
  {
    return fail("unsupported file type (expected .step or .stp)");
  }

  try
  {
    return loadChecked(path);
  }
  catch (const Standard_Failure& failure)
  {
    const char* message = failure.GetMessageString();
    return fail(std::string("OpenCascade error: ")
                + (message != nullptr && *message != '\0' ? message : failure.DynamicType()->Name()));
  }
  catch (const std::exception& error)
  {
    return fail(error.what());
  }
}
