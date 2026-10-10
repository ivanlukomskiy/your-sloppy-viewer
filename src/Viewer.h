#pragma once

#include "GlfwOcctWindow.h"
#include "StepLoader.h"

#include <AIS_InteractiveContext.hxx>
#include <AIS_TextLabel.hxx>
#include <AIS_ViewController.hxx>
#include <AIS_ViewCube.hxx>
#include <Graphic3d_MaterialAspect.hxx>
#include <Prs3d_LineAspect.hxx>
#include <V3d_View.hxx>
#include <V3d_Viewer.hxx>

#include <string>
#include <vector>

class Viewer : private AIS_ViewController
{
public:
  enum class Status { None, Info, Error };

  Viewer(const Handle(GlfwOcctWindow)& window, double uiScale);
  ~Viewer();

  Viewer(const Viewer&) = delete;
  Viewer& operator=(const Viewer&) = delete;

  double show(const StepModel& model);
  void setStatus(Status kind, const std::string& text = {});

  void resize();
  void invalidate();
  void redrawNow();
  bool processEvents();

  void mouseMove(int x, int y);
  void mouseButton(int x, int y, Aspect_VKeyMouse button, bool pressed);
  void scroll(int x, int y, double delta);

  void fit();
  void orient(V3d_TypeOfOrientation orientation);
  void toggleEdges();
  void toggleProjection();

private:
  void removeModel();
  void rebuildOverlay();
  void addLabel(const std::string& text, const Quantity_Color& color, double height,
                Aspect_TypeOfTriedronPosition anchor, int offsetX, int offsetY);

  Handle(GlfwOcctWindow) window_;
  Handle(V3d_Viewer) viewer_;
  Handle(V3d_View) view_;
  Handle(AIS_InteractiveContext) context_;
  Handle(AIS_ViewCube) cube_;
  Handle(Prs3d_LineAspect) edgeAspect_;
  Graphic3d_MaterialAspect material_;
  TCollection_AsciiString font_;

  Handle(TDocStd_Document) document_;
  std::vector<Handle(AIS_InteractiveObject)> shapes_;
  std::vector<Handle(AIS_TextLabel)> labels_;

  Status statusKind_ = Status::None;
  std::string statusText_;
  double uiScale_ = 1.0;
  bool edges_ = true;
};
