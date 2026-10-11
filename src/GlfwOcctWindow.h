// Copyright (c) 2019 OPEN CASCADE SAS
//
// This file is part of the examples of the Open CASCADE Technology software library.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE
//
// Adapted for Your Sloppy Viewer from samples/glfw/GlfwOcctWindow.h (OCCT 7.9.3).

#pragma once

#include <Aspect_DisplayConnection.hxx>
#include <Aspect_RenderingContext.hxx>
#include <Aspect_Window.hxx>

struct GLFWwindow;

class GlfwOcctWindow : public Aspect_Window
{
  DEFINE_STANDARD_RTTI_INLINE(GlfwOcctWindow, Aspect_Window)
public:
  GlfwOcctWindow(int width, int height, const char* title);
  ~GlfwOcctWindow() override { Close(); }

  void Close();

  GLFWwindow* Glfw() const { return myGlfwWindow; }
  const Handle(Aspect_DisplayConnection)& GetDisplay() const { return myDisplay; }
  Aspect_RenderingContext NativeGlContext() const;

  bool TakeFrameRequest();

  Aspect_Drawable NativeHandle() const override;
  Aspect_Drawable NativeParentHandle() const override { return 0; }
  Aspect_FBConfig NativeFBConfig() const override { return nullptr; }
  Aspect_TypeOfResize DoResize() override;
  Standard_Boolean IsMapped() const override;
  Standard_Boolean DoMapping() const override { return Standard_True; }
  void Map() const override;
  void Unmap() const override;
  void SetTitle(const TCollection_AsciiString& title) override;
  void InvalidateContent(const Handle(Aspect_DisplayConnection)&) override { myFrameRequested = true; }

  void Position(Standard_Integer& x1, Standard_Integer& y1,
                Standard_Integer& x2, Standard_Integer& y2) const override
  {
    x1 = myXLeft;
    x2 = myXRight;
    y1 = myYTop;
    y2 = myYBottom;
  }

  Standard_Real Ratio() const override
  {
    return Standard_Real(myXRight - myXLeft) / Standard_Real(myYBottom - myYTop);
  }

  void Size(Standard_Integer& width, Standard_Integer& height) const override
  {
    width = myXRight - myXLeft;
    height = myYBottom - myYTop;
  }

private:
  Handle(Aspect_DisplayConnection) myDisplay;
  GLFWwindow* myGlfwWindow = nullptr;
  Standard_Integer myXLeft = 0;
  Standard_Integer myYTop = 0;
  Standard_Integer myXRight = 0;
  Standard_Integer myYBottom = 0;
  bool myFrameRequested = false;
};
