#include "StepLoader.h"

#include <BRepBndLib.hxx>
#include <BRepLib_ToolTriangulatedShape.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <IMeshTools_Parameters.hxx>
#include <Message_ProgressIndicator.hxx>
#include <Geom_Curve.hxx>
#include <Poly_Polygon3D.hxx>
#include <Poly_PolygonOnTriangulation.hxx>
#include <Poly_Triangulation.hxx>
#include <Precision.hxx>
#include <Quantity_ColorRGBA.hxx>
#include <STEPCAFControl_Reader.hxx>
#include <Standard_Failure.hxx>
#include <TDF_LabelSequence.hxx>
#include <TDocStd_Document.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Iterator.hxx>
#include <XCAFPrs.hxx>
#include <XCAFPrs_IndexedDataMapOfShapeStyle.hxx>
#include <XCAFPrs_Style.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <exception>
#include <fstream>
#include <streambuf>
#include <system_error>
#include <vector>

#ifdef __GLIBC__
#include <malloc.h>
#endif

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

class StageProgress : public Message_ProgressIndicator
{
public:
  explicit StageProgress(LoadProgress& progress) : progress_(progress) {}

  Message_ProgressRange begin(LoadProgress::Stage stage)
  {
    progress_.fraction.store(0.0f);
    progress_.stage.store(stage);
    return Start();
  }

protected:
  void Show(const Message_ProgressScope&, const Standard_Boolean) override
  {
    progress_.fraction.store(static_cast<float>(GetPosition()));
  }

private:
  LoadProgress& progress_;
};

class CountingBuffer : public std::streambuf
{
public:
  CountingBuffer(std::streambuf& source, std::uintmax_t total, LoadProgress& progress)
  : source_(source), total_(total), progress_(progress)
  {
  }

protected:
  int_type underflow() override
  {
    const std::streamsize count = source_.sgetn(buffer_.data(), buffer_.size());
    if (count <= 0)
    {
      progress_.stage.store(LoadProgress::Stage::Parsing);
      return traits_type::eof();
    }
    read_ += static_cast<std::uintmax_t>(count);
    if (total_ > 0)
    {
      progress_.fraction.store(static_cast<float>(read_) / static_cast<float>(total_));
    }
    if (read_ >= total_)
    {
      progress_.stage.store(LoadProgress::Stage::Parsing);
    }
    setg(buffer_.data(), buffer_.data(), buffer_.data() + count);
    return traits_type::to_int_type(*gptr());
  }

private:
  std::streambuf& source_;
  std::uintmax_t total_;
  std::uintmax_t read_ = 0;
  LoadProgress& progress_;
  std::array<char, 1 << 16> buffer_;
};

void releaseFreedMemory()
{
#ifdef __GLIBC__
  malloc_trim(0);
#endif
}

StepLoadResult fail(std::string message)
{
  StepLoadResult result;
  result.error = std::move(message);
  return result;
}

using Color = std::array<std::uint8_t, 4>;

constexpr Color kDefaultColor{143, 161, 181, 255};

Color toColor(const Quantity_ColorRGBA& color)
{
  NCollection_Vec3<float> srgb = Quantity_Color::Convert_LinearRGB_To_sRGB(
    NCollection_Vec3<float>(static_cast<float>(color.GetRGB().Red()),
                            static_cast<float>(color.GetRGB().Green()),
                            static_cast<float>(color.GetRGB().Blue())));
  const auto channel = [](float value) {
    return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
  };
  return {channel(srgb.r()), channel(srgb.g()), channel(srgb.b()), 255};
}

class SceneBuilder
{
public:
  SceneBuilder(Scene& scene, const gp_XYZ& origin, int faceCount, LoadProgress& progress)
  : scene_(scene), origin_(origin), faceCount_(std::max(faceCount, 1)), progress_(progress)
  {
  }

  void addRoot(const TDF_Label& label)
  {
    const TopoDS_Shape shape = XCAFDoc_ShapeTool::GetShape(label);
    if (shape.IsNull())
    {
      return;
    }
    styles_.Clear();
    XCAFPrs::CollectStyleSettings(label, TopLoc_Location(), styles_);
    visit(shape, kDefaultColor);

    TopTools_IndexedMapOfShape edges;
    TopExp::MapShapes(shape, TopAbs_EDGE, edges);
    for (int i = 1; i <= edges.Extent(); ++i)
    {
      addEdge(TopoDS::Edge(edges(i)));
    }
  }

  void finish()
  {
    for (int axis = 0; axis < 3; ++axis)
    {
      const double low = extent_.CornerMin().Coord(axis + 1);
      const double high = extent_.CornerMax().Coord(axis + 1);
      scene_.boundsMin[axis] = static_cast<float>(low - origin_.Coord(axis + 1));
      scene_.boundsMax[axis] = static_cast<float>(high - origin_.Coord(axis + 1));
      scene_.size[axis] = high - low;
    }
  }

private:
  void visit(const TopoDS_Shape& shape, Color color)
  {
    if (const XCAFPrs_Style* style = styles_.Seek(shape))
    {
      if (!style->IsVisible())
      {
        return;
      }
      if (style->IsSetColorSurf())
      {
        color = toColor(style->GetColorSurfRGBA());
      }
    }
    if (shape.ShapeType() == TopAbs_FACE)
    {
      addFace(TopoDS::Face(shape), color);
      return;
    }
    if (shape.ShapeType() > TopAbs_FACE)
    {
      return;
    }
    for (TopoDS_Iterator children(shape); children.More(); children.Next())
    {
      visit(children.Value(), color);
    }
  }

  void addFace(const TopoDS_Face& face, const Color& color)
  {
    progress_.fraction.store(static_cast<float>(++facesDone_) / static_cast<float>(faceCount_));

    TopLoc_Location location;
    const Handle(Poly_Triangulation)& triangulation = BRep_Tool::Triangulation(face, location);
    if (triangulation.IsNull() || triangulation->NbTriangles() == 0)
    {
      return;
    }
    if (!triangulation->HasNormals())
    {
      BRepLib_ToolTriangulatedShape::ComputeNormals(face, triangulation);
    }

    const bool moved = !location.IsIdentity();
    const gp_Trsf transform = location.Transformation();
    const gp_Mat rotation = transform.VectorialPart();
    const auto base = static_cast<std::uint32_t>(scene_.vertices.size());

    for (int i = 1; i <= triangulation->NbNodes(); ++i)
    {
      gp_Pnt point = triangulation->Node(i);
      gp_Vec3f stored;
      triangulation->Normal(i, stored);
      gp_XYZ normal(stored.x(), stored.y(), stored.z());
      if (moved)
      {
        point.Transform(transform);
        normal.Multiply(rotation);
      }
      extent_.Add(point);
      const double length = normal.Modulus();
      if (length > 1.0e-12)
      {
        normal /= length;
      }

      Vertex vertex;
      vertex.position[0] = static_cast<float>(point.X() - origin_.X());
      vertex.position[1] = static_cast<float>(point.Y() - origin_.Y());
      vertex.position[2] = static_cast<float>(point.Z() - origin_.Z());
      vertex.normal[0] = static_cast<float>(normal.X());
      vertex.normal[1] = static_cast<float>(normal.Y());
      vertex.normal[2] = static_cast<float>(normal.Z());
      std::copy(color.begin(), color.end(), vertex.color);
      scene_.vertices.push_back(vertex);
    }

    for (int i = 1; i <= triangulation->NbTriangles(); ++i)
    {
      int a = 0, b = 0, c = 0;
      triangulation->Triangle(i).Get(a, b, c);
      scene_.indices.push_back(base + static_cast<std::uint32_t>(a - 1));
      scene_.indices.push_back(base + static_cast<std::uint32_t>(b - 1));
      scene_.indices.push_back(base + static_cast<std::uint32_t>(c - 1));
    }
  }

  void addEdge(const TopoDS_Edge& edge)
  {
    if (BRep_Tool::Degenerated(edge))
    {
      return;
    }

    polyline_.clear();
    TopLoc_Location location;
    Handle(Poly_PolygonOnTriangulation) onTriangulation;
    Handle(Poly_Triangulation) triangulation;
    BRep_Tool::PolygonOnTriangulation(edge, onTriangulation, triangulation, location);
    if (!onTriangulation.IsNull() && !triangulation.IsNull())
    {
      for (int i = 1; i <= onTriangulation->NbNodes(); ++i)
      {
        polyline_.push_back(triangulation->Node(onTriangulation->Node(i)));
      }
    }
    else if (const Handle(Poly_Polygon3D)& polygon = BRep_Tool::Polygon3D(edge, location);
             !polygon.IsNull())
    {
      for (int i = 1; i <= polygon->NbNodes(); ++i)
      {
        polyline_.push_back(polygon->Nodes()(i));
      }
    }
    else
    {
      location = TopLoc_Location();
      double first = 0.0, last = 0.0;
      const Handle(Geom_Curve) curve = BRep_Tool::Curve(edge, first, last);
      if (curve.IsNull() || Precision::IsInfinite(first) || Precision::IsInfinite(last))
      {
        return;
      }
      constexpr int kSegments = 48;
      for (int i = 0; i <= kSegments; ++i)
      {
        polyline_.push_back(curve->Value(first + (last - first) * i / kSegments));
      }
    }

    const bool moved = !location.IsIdentity();
    const gp_Trsf transform = location.Transformation();
    for (std::size_t i = 0; i < polyline_.size(); ++i)
    {
      if (moved)
      {
        polyline_[i].Transform(transform);
      }
      extent_.Add(polyline_[i]);
      if (i > 0)
      {
        for (const gp_Pnt* point : {&polyline_[i - 1], &polyline_[i]})
        {
          scene_.edges.push_back(static_cast<float>(point->X() - origin_.X()));
          scene_.edges.push_back(static_cast<float>(point->Y() - origin_.Y()));
          scene_.edges.push_back(static_cast<float>(point->Z() - origin_.Z()));
        }
      }
    }
  }

  Scene& scene_;
  gp_XYZ origin_;
  int faceCount_;
  int facesDone_ = 0;
  LoadProgress& progress_;
  XCAFPrs_IndexedDataMapOfShapeStyle styles_;
  std::vector<gp_Pnt> polyline_;
  Bnd_Box extent_;
};

StepLoadResult loadChecked(const std::filesystem::path& path, LoadProgress& progress)
{
  StepLoadResult result;
  LoadTimings& timings = result.timings;
  Handle(StageProgress) indicator = new StageProgress(progress);

  std::ifstream file(path, std::ios::binary);
  if (!file)
  {
    return fail("the file cannot be opened for reading");
  }
  std::error_code ec;
  const std::uintmax_t fileSize = std::filesystem::file_size(path, ec);
  CountingBuffer counting(*file.rdbuf(), ec ? 0 : fileSize, progress);
  std::istream stream(&counting);

  STEPCAFControl_Reader reader;
  reader.SetColorMode(true);
  reader.SetNameMode(false);
  reader.SetLayerMode(false);
  reader.SetPropsMode(false);

  auto start = Clock::now();
  const IFSelect_ReturnStatus status = reader.ReadStream(path.c_str(), stream);
  timings.parseMs = millisecondsSince(start);
  if (status != IFSelect_RetDone)
  {
    return fail(describeReadStatus(status));
  }

  start = Clock::now();
  Handle(TDocStd_Document) document = new TDocStd_Document("XCAF");
  if (!reader.Transfer(document, indicator->begin(LoadProgress::Stage::Converting)))
  {
    return fail("the STEP entities could not be converted to geometry");
  }

  TDF_LabelSequence roots;
  XCAFDoc_DocumentTool::ShapeTool(document->Main())->GetFreeShapes(roots);

  BRep_Builder builder;
  TopoDS_Compound compound;
  builder.MakeCompound(compound);
  bool hasGeometry = false;
  for (const TDF_Label& root : roots)
  {
    const TopoDS_Shape shape = XCAFDoc_ShapeTool::GetShape(root);
    if (!shape.IsNull())
    {
      builder.Add(compound, shape);
      hasGeometry = true;
    }
  }
  timings.transferMs = millisecondsSince(start);

  Bnd_Box bounds;
  if (hasGeometry)
  {
    BRepBndLib::Add(compound, bounds, false);
  }
  if (bounds.IsVoid())
  {
    return fail("the file contains no displayable geometry");
  }

  TopTools_IndexedMapOfShape faces;
  TopExp::MapShapes(compound, TopAbs_FACE, faces);

  const double diagonal = std::sqrt(bounds.SquareExtent());

  start = Clock::now();
  IMeshTools_Parameters parameters;
  parameters.Deflection = std::max(diagonal * 4.0e-4, 1.0e-6);
  parameters.Angle = std::clamp(0.15 * std::sqrt(faces.Extent() / 1500.0), 0.15, 0.40);
  parameters.InParallel = true;
  BRepMesh_IncrementalMesh mesher(compound, parameters,
                                  indicator->begin(LoadProgress::Stage::Tessellating));
  timings.meshMs = millisecondsSince(start);

  start = Clock::now();
  progress.fraction.store(0.0f);
  progress.stage.store(LoadProgress::Stage::Preparing);

  const gp_XYZ origin = (bounds.CornerMin().XYZ() + bounds.CornerMax().XYZ()) * 0.5;
  Scene scene;
  SceneBuilder sceneBuilder(scene, origin, faces.Extent(), progress);
  for (const TDF_Label& root : roots)
  {
    sceneBuilder.addRoot(root);
  }
  if (scene.indices.empty() && scene.edges.empty())
  {
    return fail("the file contains no displayable geometry");
  }

  sceneBuilder.finish();
  timings.buildMs = millisecondsSince(start);

  result.scene = std::move(scene);
  return result;
}
}

bool hasStepExtension(const std::filesystem::path& path)
{
  std::string extension = path.extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return extension == ".step" || extension == ".stp";
}

StepLoadResult loadStep(const std::filesystem::path& path, LoadProgress& progress)
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
    StepLoadResult result = loadChecked(path, progress);
    releaseFreedMemory();
    return result;
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
