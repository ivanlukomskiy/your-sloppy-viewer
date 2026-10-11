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
// Adapted for Your Sloppy Viewer from samples/glfw/GlfwOcctWindow.cpp (OCCT 7.9.3).

#include "GlfwOcctWindow.h"

#include <TCollection_AsciiString.hxx>

#define GLFW_EXPOSE_NATIVE_X11
#define GLFW_EXPOSE_NATIVE_GLX
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <X11/Xatom.h>

GlfwOcctWindow::GlfwOcctWindow(int width, int height, const char* title)
: myGlfwWindow(glfwCreateWindow(width, height, title, nullptr, nullptr))
{
  if (myGlfwWindow == nullptr)
  {
    return;
  }

  DoResize();

  Display* display = glfwGetX11Display();
  myDisplay = new Aspect_DisplayConnection(reinterpret_cast<Aspect_XDisplay*>(display));

  const Atom variant = XInternAtom(display, "_GTK_THEME_VARIANT", False);
  const Atom utf8 = XInternAtom(display, "UTF8_STRING", False);
  XChangeProperty(display, glfwGetX11Window(myGlfwWindow), variant, utf8, 8, PropModeReplace,
                  reinterpret_cast<const unsigned char*>("dark"), 4);
}

void GlfwOcctWindow::Close()
{
  if (myGlfwWindow != nullptr)
  {
    glfwDestroyWindow(myGlfwWindow);
    myGlfwWindow = nullptr;
  }
}

bool GlfwOcctWindow::TakeFrameRequest()
{
  const bool requested = myFrameRequested;
  myFrameRequested = false;
  return requested;
}

Aspect_Drawable GlfwOcctWindow::NativeHandle() const
{
  return static_cast<Aspect_Drawable>(glfwGetX11Window(myGlfwWindow));
}

Aspect_RenderingContext GlfwOcctWindow::NativeGlContext() const
{
  return glfwGetGLXContext(myGlfwWindow);
}

Standard_Boolean GlfwOcctWindow::IsMapped() const
{
  return glfwGetWindowAttrib(myGlfwWindow, GLFW_VISIBLE) != 0;
}

void GlfwOcctWindow::Map() const
{
  glfwShowWindow(myGlfwWindow);
}

void GlfwOcctWindow::Unmap() const
{
  glfwHideWindow(myGlfwWindow);
}

void GlfwOcctWindow::SetTitle(const TCollection_AsciiString& title)
{
  glfwSetWindowTitle(myGlfwWindow, title.ToCString());
}

Aspect_TypeOfResize GlfwOcctWindow::DoResize()
{
  int x = 0, y = 0, width = 0, height = 0;
  glfwGetWindowPos(myGlfwWindow, &x, &y);
  glfwGetFramebufferSize(myGlfwWindow, &width, &height);
  myXLeft = x;
  myXRight = x + width;
  myYTop = y;
  myYBottom = y + height;
  return Aspect_TOR_UNKNOWN;
}
