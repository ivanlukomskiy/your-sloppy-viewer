#include <BRepAlgoAPI_Cut.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <BRepPrimAPI_MakeTorus.hxx>
#include <Quantity_Color.hxx>
#include <STEPCAFControl_Writer.hxx>
#include <TDocStd_Document.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <XCAFDoc_ColorTool.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <gp_Ax2.hxx>

#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv)
{
  if (argc != 2)
  {
    std::fprintf(stderr, "Usage: make_fixture output.step\n");
    return EXIT_FAILURE;
  }

  TopoDS_Shape plate = BRepPrimAPI_MakeBox(gp_Pnt(-60.0, -40.0, 0.0), 120.0, 80.0, 16.0).Shape();
  BRepFilletAPI_MakeFillet fillet(plate);
  for (TopExp_Explorer edges(plate, TopAbs_EDGE); edges.More(); edges.Next())
  {
    fillet.Add(3.0, TopoDS::Edge(edges.Current()));
  }
  plate = fillet.Shape();
  for (const double x : {-42.0, 42.0})
  {
    for (const double y : {-24.0, 24.0})
    {
      const gp_Ax2 axis(gp_Pnt(x, y, -1.0), gp::DZ());
      plate = BRepAlgoAPI_Cut(plate, BRepPrimAPI_MakeCylinder(axis, 5.0, 18.0).Shape()).Shape();
    }
  }

  const TopoDS_Shape boss =
    BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0.0, 0.0, 16.0), gp::DZ()), 18.0, 30.0).Shape();
  const TopoDS_Shape ring =
    BRepPrimAPI_MakeTorus(gp_Ax2(gp_Pnt(0.0, 0.0, 46.0), gp::DZ()), 18.0, 4.0).Shape();
  const TopoDS_Shape knob = BRepPrimAPI_MakeSphere(gp_Pnt(0.0, 0.0, 58.0), 11.0).Shape();

  Handle(TDocStd_Document) document = new TDocStd_Document("StepViewFixture");
  Handle(XCAFDoc_ShapeTool) shapes = XCAFDoc_DocumentTool::ShapeTool(document->Main());
  Handle(XCAFDoc_ColorTool) colors = XCAFDoc_DocumentTool::ColorTool(document->Main());

  shapes->AddShape(plate, false);
  shapes->AddShape(boss, false);
  colors->SetColor(shapes->AddShape(ring, false),
                   Quantity_Color(0.85, 0.45, 0.15, Quantity_TOC_sRGB), XCAFDoc_ColorSurf);
  colors->SetColor(shapes->AddShape(knob, false),
                   Quantity_Color(0.20, 0.55, 0.75, Quantity_TOC_sRGB), XCAFDoc_ColorSurf);

  STEPCAFControl_Writer writer;
  if (!writer.Transfer(document, STEPControl_AsIs) || writer.Write(argv[1]) != IFSelect_RetDone)
  {
    std::fprintf(stderr, "make_fixture: failed to write %s\n", argv[1]);
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
