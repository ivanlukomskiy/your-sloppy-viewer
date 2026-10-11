#include "Viewer.h"

#include <AIS_AnimationCamera.hxx>
#include <Font_FontMgr.hxx>
#include <Graphic3d_ArrayOfTriangles.hxx>
#include <Graphic3d_AspectFillArea3d.hxx>
#include <Graphic3d_Camera.hxx>
#include <Graphic3d_Group.hxx>
#include <Graphic3d_TransformPers.hxx>
#include <OpenGl_GraphicDriver.hxx>
#include <Prs3d_Drawer.hxx>
#include <TCollection_ExtendedString.hxx>
#include <V3d_AmbientLight.hxx>
#include <V3d_DirectionalLight.hxx>
#include <XCAFPrs_AISObject.hxx>
#include <XCAFPrs_Style.hxx>

#include <algorithm>
#include <chrono>
#include <cmath>

class ProgressBar : public AIS_InteractiveObject
{
public:
  ProgressBar(double width, double height) : width_(width), height_(height)
  {
    aspect_ = new Graphic3d_AspectFillArea3d();
    aspect_->SetInteriorStyle(Aspect_IS_SOLID);
    aspect_->SetShadingModel(Graphic3d_TypeOfShadingModel_Unlit);
    SetZLayer(Graphic3d_ZLayerId_TopOSD);
  }

  double width() const { return width_; }
  double height() const { return height_; }

  void place(double x, double y)
  {
    originX_ = x;
    originY_ = y;
  }

  bool setFill(double start, double end)
  {
    const double from = std::round(std::clamp(start, 0.0, 1.0) * width_);
    const double to = std::max(std::round(std::clamp(end, 0.0, 1.0) * width_), from);
    if (from == fillFrom_ && to == fillTo_)
    {
      return false;
    }
    fillFrom_ = from;
    fillTo_ = to;
    return true;
  }

private:
  static constexpr int kArcSteps = 10;

  Quantity_Color gradient(double x) const
  {
    const double t = std::clamp(x / width_, 0.0, 1.0);
    return Quantity_Color(0.47 + (0.22 - 0.47) * t, 0.88 + (0.62 - 0.88) * t,
                          0.38 + (0.93 - 0.38) * t, Quantity_TOC_sRGB);
  }

  Handle(Graphic3d_ArrayOfTriangles) capsule(double from, double to, const Quantity_Color* flat) const
  {
    const double radius = height_ * 0.5;
    const double left = from + radius;
    const double right = std::max(to - radius, left);
    const int rim = 2 * (kArcSteps + 1);

    Handle(Graphic3d_ArrayOfTriangles) triangles =
      new Graphic3d_ArrayOfTriangles(rim + 1, rim * 3, false, true);
    const auto add = [&](double x, double y) {
      triangles->AddVertex(gp_Pnt(originX_ + x, originY_ + y, 0.0),
                           flat != nullptr ? *flat : gradient(x));
    };

    add((left + right) * 0.5, radius);
    for (int side = 0; side < 2; ++side)
    {
      const double centre = side == 0 ? right : left;
      const double offset = side == 0 ? -M_PI_2 : M_PI_2;
      for (int i = 0; i <= kArcSteps; ++i)
      {
        const double angle = offset + M_PI * i / kArcSteps;
        add(centre + radius * std::cos(angle), radius + radius * std::sin(angle));
      }
    }
    for (int i = 0; i < rim; ++i)
    {
      triangles->AddEdges(1, 2 + i, 2 + (i + 1) % rim);
    }
    return triangles;
  }

  void Compute(const Handle(PrsMgr_PresentationManager)&, const Handle(Prs3d_Presentation)& presentation,
               const Standard_Integer) override
  {
    static const Quantity_Color track(0.20, 0.215, 0.24, Quantity_TOC_sRGB);

    Handle(Graphic3d_Group) group = presentation->NewGroup();
    group->SetGroupPrimitivesAspect(aspect_);
    group->AddPrimitiveArray(capsule(0.0, width_, &track));
    if (fillTo_ > fillFrom_)
    {
      const double to = std::max(fillTo_, fillFrom_ + height_);
      group->AddPrimitiveArray(capsule(std::min(fillFrom_, width_ - height_), std::min(to, width_), nullptr));
    }
  }

  void ComputeSelection(const Handle(SelectMgr_Selection)&, const Standard_Integer) override {}

  Handle(Graphic3d_AspectFillArea3d) aspect_;
  double width_;
  double height_;
  double originX_ = 0.0;
  double originY_ = 0.0;
  double fillFrom_ = 0.0;
  double fillTo_ = 0.0;
};

namespace
{
Quantity_Color srgb(double r, double g, double b)
{
  return Quantity_Color(r, g, b, Quantity_TOC_sRGB);
}

const Quantity_Color kSurfaceColor = srgb(0.56, 0.63, 0.71);
const Quantity_Color kCurveColor = srgb(0.80, 0.83, 0.87);
const Quantity_Color kEdgeColor = srgb(0.09, 0.10, 0.12);
const Quantity_Color kHintColor = srgb(0.46, 0.48, 0.52);
const Quantity_Color kInfoColor = srgb(0.66, 0.69, 0.74);
const Quantity_Color kErrorColor = srgb(0.93, 0.47, 0.42);

class ModelShape : public XCAFPrs_AISObject
{
public:
  using XCAFPrs_AISObject::XCAFPrs_AISObject;

protected:
  void DefaultStyle(XCAFPrs_Style& style) const override
  {
    style.SetColorSurf(kSurfaceColor);
    style.SetColorCurv(kCurveColor);
  }
};

TCollection_AsciiString findUiFont()
{
  const Handle(Font_FontMgr) fonts = Font_FontMgr::GetInstance();
  for (const char* name : {"Adwaita Sans", "Cantarell", "Inter", "Noto Sans", "DejaVu Sans",
                           "Liberation Sans", "Open Sans"})
  {
    Font_FontAspect aspect = Font_FontAspect_Regular;
    if (!fonts->FindFont(name, Font_StrictLevel_Strict, aspect, false).IsNull())
    {
      return name;
    }
  }
  return Font_NOF_SANS_SERIF;
}

Handle(V3d_DirectionalLight) headlight(double x, double y, double z, double brightness)
{
  return new V3d_DirectionalLight(gp_Dir(x, y, z), Quantity_Color(brightness, brightness, brightness, Quantity_TOC_RGB), true);
}
}

Viewer::Viewer(const Handle(GlfwOcctWindow)& window, double uiScale)
: window_(window),
  material_(Graphic3d_NameOfMaterial_DEFAULT),
  font_(findUiFont()),
  uiScale_(uiScale)
{
  Handle(OpenGl_GraphicDriver) driver = new OpenGl_GraphicDriver(window_->GetDisplay(), false);
  viewer_ = new V3d_Viewer(driver);
  viewer_->SetDefaultTypeOfView(V3d_ORTHOGRAPHIC);

  viewer_->AddLight(new V3d_AmbientLight(Quantity_NOC_WHITE));
  viewer_->AddLight(headlight(0.40, -0.65, -1.0, 0.95));
  viewer_->AddLight(headlight(-0.85, 0.30, -0.50, 0.30));
  viewer_->SetLightOn();

  view_ = viewer_->CreateView();
  view_->SetImmediateUpdate(false);
  view_->SetWindow(window_, window_->NativeGlContext());
  view_->SetBgGradientColors(srgb(0.185, 0.195, 0.215), srgb(0.065, 0.068, 0.075),
                             Aspect_GradientFillMethod_Vertical, false);
  view_->SetProj(V3d_XposYnegZpos);

  Graphic3d_RenderingParams& rendering = view_->ChangeRenderingParams();
  rendering.NbMsaaSamples = 4;
  rendering.IsAntialiasingEnabled = true;

  context_ = new AIS_InteractiveContext(viewer_);
  context_->SetAutoActivateSelection(false);
  context_->HighlightStyle(Prs3d_TypeOfHighlight_Dynamic)->SetColor(srgb(0.45, 0.62, 0.85));

  material_.SetAmbientColor(Quantity_Color(0.26, 0.26, 0.26, Quantity_TOC_RGB));
  material_.SetDiffuseColor(Quantity_Color(0.72, 0.72, 0.72, Quantity_TOC_RGB));
  material_.SetSpecularColor(Quantity_Color(0.22, 0.23, 0.25, Quantity_TOC_RGB));
  material_.SetShininess(0.35f);

  edgeAspect_ = new Prs3d_LineAspect(kEdgeColor, Aspect_TOL_SOLID, 1.0);

  AIS_MouseGestureMap& gestures = ChangeMouseGestureMap();
  gestures.Clear();
  gestures.Bind(Aspect_VKeyMouse_LeftButton, AIS_MouseGesture_RotateOrbit);
  gestures.Bind(Aspect_VKeyMouse_MiddleButton, AIS_MouseGesture_Pan);
  gestures.Bind(Aspect_VKeyMouse_RightButton, AIS_MouseGesture_Pan);
  SetLockOrbitZUp(true);
  SetShowRotateCenter(false);
  SetShowPanAnchorPoint(false);

  const int cubeOffset = static_cast<int>(std::lround(66.0 * uiScale_));
  cube_ = new AIS_ViewCube();
  cube_->SetSize(38.0 * uiScale_);
  cube_->SetFont(font_);
  cube_->SetFontHeight(11.0 * uiScale_);
  cube_->SetBoxColor(srgb(0.30, 0.33, 0.38));
  cube_->SetTextColor(srgb(0.90, 0.91, 0.93));
  cube_->SetTransparency(0.1);
  cube_->SetDrawAxes(false);
  cube_->SetViewAnimation(ViewAnimation());
  cube_->SetFixedAnimationLoop(false);
  cube_->SetDuration(0.2);
  cube_->SetTransformPersistence(new Graphic3d_TransformPers(
    Graphic3d_TMF_TriedronPers, Aspect_TOTP_RIGHT_UPPER, Graphic3d_Vec2i(cubeOffset, cubeOffset)));

  bar_ = new ProgressBar(std::round(240.0 * uiScale_), std::round(8.0 * uiScale_));

  rebuildOverlay();
}

Viewer::~Viewer()
{
  context_->Remove(bar_, false);
  for (const Handle(AIS_TextLabel)& label : labels_)
  {
    context_->Remove(label, false);
  }
  removeModel();
  context_->Remove(cube_, false);
  view_->Remove();
}

double Viewer::show(const StepModel& model)
{
  const auto start = std::chrono::steady_clock::now();

  AbortViewAnimation();
  removeModel();
  document_ = model.document;

  for (const TDF_Label& root : model.roots)
  {
    Handle(ModelShape) shape = new ModelShape(root);
    const Handle(Prs3d_Drawer)& drawer = shape->Attributes();
    drawer->SetTypeOfDeflection(Aspect_TOD_ABSOLUTE);
    drawer->SetMaximalChordialDeviation(model.quality.linearDeflection);
    drawer->SetDeviationAngle(model.quality.angularDeflection);
    drawer->SetAutoTriangulation(false);
    drawer->SetFaceBoundaryAspect(edgeAspect_);
    drawer->SetFaceBoundaryDraw(edges_);
    shape->SetMaterial(material_);
    context_->Display(shape, AIS_Shaded, -1, false);
    shapes_.push_back(shape);
  }

  if (!context_->IsDisplayed(cube_))
  {
    context_->Display(cube_, 0, 0, false);
  }

  statusKind_ = Status::None;
  statusText_.clear();
  rebuildOverlay();

  view_->SetProj(V3d_XposYnegZpos);
  view_->FitAll(0.08, false);
  view_->Redraw();

  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

void Viewer::setStatus(Status kind, const std::string& text)
{
  statusKind_ = kind;
  statusText_ = text;
  rebuildOverlay();
}

void Viewer::setLoading(const std::string& title, float fraction)
{
  if (statusKind_ != Status::Loading)
  {
    loadingStart_ = std::chrono::steady_clock::now();
  }
  if (statusKind_ != Status::Loading || statusText_ != title)
  {
    statusKind_ = Status::Loading;
    statusText_ = title;
    loadingPercent_ = -1;
    rebuildOverlay();
  }

  double start = 0.0;
  double end = fraction;
  if (fraction < 0.0f)
  {
    const double seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - loadingStart_).count();
    start = std::fmod(seconds * 0.8, 1.3) - 0.3;
    end = start + 0.3;
  }
  if (bar_->setFill(start, end))
  {
    context_->Redisplay(bar_, false);
    view_->Invalidate();
  }

  const int percent = fraction < 0.0f ? -1 : static_cast<int>(std::clamp(fraction, 0.0f, 1.0f) * 100.0f);
  if (percent != loadingPercent_)
  {
    loadingPercent_ = percent;
    const std::string text = percent < 0 ? std::string() : std::to_string(percent) + "%";
    percentLabel_->SetText(TCollection_ExtendedString(text.c_str(), true));
    context_->Redisplay(percentLabel_, false);
    view_->Invalidate();
  }
}

void Viewer::resize()
{
  window_->DoResize();
  view_->MustBeResized();
  view_->Invalidate();
}

void Viewer::invalidate()
{
  view_->Invalidate();
}

void Viewer::redrawNow()
{
  view_->Redraw();
}

bool Viewer::processEvents()
{
  FlushViewEvents(context_, view_, true);
  return window_->TakeFrameRequest();
}

void Viewer::mouseMove(int x, int y)
{
  UpdateMousePosition(Graphic3d_Vec2i(x, y), PressedMouseButtons(), Aspect_VKeyFlags_NONE, false);
}

void Viewer::mouseButton(int x, int y, Aspect_VKeyMouse button, bool pressed)
{
  const Graphic3d_Vec2i position(x, y);
  if (pressed)
  {
    PressMouseButton(position, button, Aspect_VKeyFlags_NONE, false);
  }
  else
  {
    ReleaseMouseButton(position, button, Aspect_VKeyFlags_NONE, false);
  }
}

void Viewer::scroll(int x, int y, double delta)
{
  UpdateZoom(Aspect_ScrollDelta(Graphic3d_Vec2i(x, y), delta));
}

void Viewer::fit()
{
  if (shapes_.empty())
  {
    return;
  }
  AbortViewAnimation();
  view_->FitAll(0.08, false);
  view_->Invalidate();
}

void Viewer::orient(V3d_TypeOfOrientation orientation)
{
  AbortViewAnimation();
  view_->SetProj(orientation);
  if (!shapes_.empty())
  {
    view_->FitAll(0.08, false);
  }
  view_->Invalidate();
}

void Viewer::toggleEdges()
{
  edges_ = !edges_;
  for (const Handle(AIS_InteractiveObject)& shape : shapes_)
  {
    shape->Attributes()->SetFaceBoundaryDraw(edges_);
    context_->Redisplay(shape, false);
  }
  view_->Invalidate();
}

void Viewer::toggleProjection()
{
  AbortViewAnimation();
  const Handle(Graphic3d_Camera)& camera = view_->Camera();
  const double scale = camera->Scale();
  camera->SetProjectionType(camera->IsOrthographic() ? Graphic3d_Camera::Projection_Perspective
                                                     : Graphic3d_Camera::Projection_Orthographic);
  camera->SetScale(scale);
  view_->Invalidate();
}

void Viewer::removeModel()
{
  for (const Handle(AIS_InteractiveObject)& shape : shapes_)
  {
    context_->Remove(shape, false);
  }
  shapes_.clear();
  document_.Nullify();
}

void Viewer::rebuildOverlay()
{
  for (const Handle(AIS_TextLabel)& label : labels_)
  {
    context_->Remove(label, false);
  }
  labels_.clear();
  percentLabel_.Nullify();
  context_->Remove(bar_, false);

  const bool empty = shapes_.empty();
  const int margin = static_cast<int>(std::lround(18.0 * uiScale_));
  const int lineGap = static_cast<int>(std::lround(16.0 * uiScale_));
  const int barWidth = static_cast<int>(bar_->width());
  const int barHeight = static_cast<int>(bar_->height());
  const int percentGap = static_cast<int>(std::lround(12.0 * uiScale_));
  const int percentLift = static_cast<int>(std::lround(1.5 * uiScale_));

  if (statusKind_ == Status::Loading)
  {
    Aspect_TypeOfTriedronPosition anchor = Aspect_TOTP_LEFT_LOWER;
    if (empty)
    {
      anchor = Aspect_TOTP_CENTER;
      addLabel(statusText_, kInfoColor, 16.0, anchor, 0, lineGap);
      bar_->place(-barWidth / 2, -lineGap - barHeight / 2);
      bar_->SetTransformPersistence(new Graphic3d_TransformPers(Graphic3d_TMF_2d, anchor));
      percentLabel_ = addLabel("", kHintColor, 13.0, anchor, barWidth / 2 + percentGap,
                               -lineGap + percentLift, true);
    }
    else
    {
      addLabel(statusText_, kInfoColor, 14.0, anchor, margin, margin + barHeight + percentGap);
      bar_->place(0.0, 0.0);
      bar_->SetTransformPersistence(
        new Graphic3d_TransformPers(Graphic3d_TMF_2d, anchor, Graphic3d_Vec2i(margin, margin)));
      percentLabel_ = addLabel("", kHintColor, 13.0, anchor, margin + barWidth + percentGap,
                               margin - percentLift / 2, true);
    }
    bar_->setFill(0.0, 0.0);
    context_->Display(bar_, 0, -1, false);
  }
  else if (empty && statusKind_ == Status::None)
  {
    addLabel("Drop a STEP file here", kHintColor, 17.0, Aspect_TOTP_CENTER, 0, 0);
  }
  else if (empty)
  {
    addLabel(statusText_, kErrorColor, 15.0, Aspect_TOTP_CENTER, 0, lineGap);
    addLabel("Drop a STEP file here", kHintColor, 15.0, Aspect_TOTP_CENTER, 0, -lineGap);
  }
  else if (statusKind_ == Status::Error)
  {
    addLabel(statusText_, kErrorColor, 14.0, Aspect_TOTP_LEFT_LOWER, margin, margin);
  }

  view_->Invalidate();
}

Handle(AIS_TextLabel) Viewer::addLabel(const std::string& text, const Quantity_Color& color,
                                       double height, Aspect_TypeOfTriedronPosition anchor,
                                       int offsetX, int offsetY, bool alignLeft)
{
  const bool centered = anchor == Aspect_TOTP_CENTER;

  Handle(AIS_TextLabel) label = new AIS_TextLabel();
  label->SetText(TCollection_ExtendedString(text.c_str(), true));
  label->SetColor(color);
  label->SetFont(font_.ToCString());
  label->SetHeight(height * uiScale_);
  label->SetHJustification(centered && !alignLeft ? Graphic3d_HTA_CENTER : Graphic3d_HTA_LEFT);
  label->SetVJustification(centered ? Graphic3d_VTA_CENTER : Graphic3d_VTA_BOTTOM);
  label->SetZLayer(Graphic3d_ZLayerId_TopOSD);
  if (centered)
  {
    label->SetPosition(gp_Pnt(offsetX, offsetY, 0.0));
    label->SetTransformPersistence(new Graphic3d_TransformPers(Graphic3d_TMF_2d, anchor));
  }
  else
  {
    label->SetTransformPersistence(
      new Graphic3d_TransformPers(Graphic3d_TMF_2d, anchor, Graphic3d_Vec2i(offsetX, offsetY)));
  }
  context_->Display(label, 0, -1, false);
  labels_.push_back(label);
  return label;
}
