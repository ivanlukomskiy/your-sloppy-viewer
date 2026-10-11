#include "TextAtlas.h"

#include <pango/pangocairo.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <string>

namespace
{
constexpr int kFontPixels = 56;
constexpr int kAtlasWidth = 4096;
constexpr int kMaxAtlasHeight = 8192;
constexpr int kPadding = 2;

struct Glyphs
{
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
  int baseline = 0;
};

int nextPowerOfTwo(int value)
{
  int result = 64;
  while (result < value)
  {
    result *= 2;
  }
  return result;
}
}

TextLayer buildTextLayer(const Scene& scene)
{
  TextLayer layer;
  if (scene.texts.empty())
  {
    return layer;
  }

  cairo_surface_t* probe = cairo_image_surface_create(CAIRO_FORMAT_A8, 8, 8);
  cairo_t* probeContext = cairo_create(probe);
  PangoLayout* layout = pango_cairo_create_layout(probeContext);
  PangoFontDescription* font = pango_font_description_from_string("Sans");
  pango_font_description_set_absolute_size(font, kFontPixels * PANGO_SCALE);
  pango_layout_set_font_description(layout, font);

  PangoRectangle ink{};
  pango_layout_set_text(layout, "H", -1);
  pango_layout_get_pixel_extents(layout, &ink, nullptr);
  const double capHeight = std::max(ink.height, 1);

  std::map<std::string, Glyphs> placed;
  int cursorX = kPadding, cursorY = kPadding, rowHeight = 0;
  for (const TextItem& item : scene.texts)
  {
    if (placed.contains(item.text))
    {
      continue;
    }
    int width = 0, height = 0;
    pango_layout_set_text(layout, item.text.c_str(), -1);
    pango_layout_get_pixel_size(layout, &width, &height);
    if (width <= 0 || height <= 0 || width + 2 * kPadding > kAtlasWidth)
    {
      continue;
    }
    if (cursorX + width + kPadding > kAtlasWidth)
    {
      cursorX = kPadding;
      cursorY += rowHeight + kPadding;
      rowHeight = 0;
    }
    if (cursorY + height + kPadding > kMaxAtlasHeight)
    {
      break;
    }
    placed[item.text] = {cursorX, cursorY, width, height, pango_layout_get_baseline(layout) / PANGO_SCALE};
    cursorX += width + kPadding;
    rowHeight = std::max(rowHeight, height);
  }

  layer.width = kAtlasWidth;
  layer.height = nextPowerOfTwo(cursorY + rowHeight + kPadding);
  cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_A8, layer.width, layer.height);
  cairo_t* context = cairo_create(surface);
  pango_cairo_update_layout(context, layout);
  for (const auto& [text, glyphs] : placed)
  {
    pango_layout_set_text(layout, text.c_str(), -1);
    cairo_move_to(context, glyphs.x, glyphs.y);
    pango_cairo_show_layout(context, layout);
  }
  cairo_surface_flush(surface);

  const int stride = cairo_image_surface_get_stride(surface);
  const unsigned char* data = cairo_image_surface_get_data(surface);
  layer.pixels.resize(static_cast<std::size_t>(layer.width) * layer.height);
  for (int row = 0; row < layer.height; ++row)
  {
    std::copy(data + static_cast<std::ptrdiff_t>(row) * stride,
              data + static_cast<std::ptrdiff_t>(row) * stride + layer.width,
              layer.pixels.begin() + static_cast<std::ptrdiff_t>(row) * layer.width);
  }

  cairo_destroy(context);
  cairo_surface_destroy(surface);
  pango_font_description_free(font);
  g_object_unref(layout);
  cairo_destroy(probeContext);
  cairo_surface_destroy(probe);

  layer.vertices.reserve(scene.texts.size() * 6);
  for (const TextItem& item : scene.texts)
  {
    const auto found = placed.find(item.text);
    if (found == placed.end())
    {
      continue;
    }
    const Glyphs& glyphs = found->second;
    const double scale = item.height / capHeight;

    double anchorX = 0.0;
    if (item.horizontal == TextItem::Horizontal::Center)
    {
      anchorX = glyphs.width * 0.5;
    }
    else if (item.horizontal == TextItem::Horizontal::Right)
    {
      anchorX = glyphs.width;
    }
    double anchorY = 0.0;
    switch (item.vertical)
    {
      case TextItem::Vertical::Baseline:
        break;
      case TextItem::Vertical::Bottom:
        anchorY = glyphs.baseline - glyphs.height;
        break;
      case TextItem::Vertical::Middle:
        anchorY = capHeight * 0.5;
        break;
      case TextItem::Vertical::Top:
        anchorY = capHeight;
        break;
    }

    const double angle = item.rotation * 3.14159265358979323846 / 180.0;
    const double cosine = std::cos(angle);
    const double sine = std::sin(angle);
    const double left = -anchorX;
    const double right = glyphs.width - anchorX;
    const double top = glyphs.baseline - anchorY;
    const double bottom = glyphs.baseline - glyphs.height - anchorY;
    const double u0 = static_cast<double>(glyphs.x) / layer.width;
    const double u1 = static_cast<double>(glyphs.x + glyphs.width) / layer.width;
    const double v0 = static_cast<double>(glyphs.y) / layer.height;
    const double v1 = static_cast<double>(glyphs.y + glyphs.height) / layer.height;

    const auto vertex = [&](double x, double y, double u, double v) {
      TextVertex result{};
      result.position[0] = static_cast<float>(item.x + (x * cosine - y * sine) * scale);
      result.position[1] = static_cast<float>(item.y + (x * sine + y * cosine) * scale);
      result.texCoord[0] = static_cast<float>(u);
      result.texCoord[1] = static_cast<float>(v);
      std::copy(item.color, item.color + 4, result.color);
      result.layer = item.layer;
      layer.vertices.push_back(result);
    };
    vertex(left, bottom, u0, v1);
    vertex(right, bottom, u1, v1);
    vertex(right, top, u1, v0);
    vertex(left, bottom, u0, v1);
    vertex(right, top, u1, v0);
    vertex(left, top, u0, v0);
  }
  return layer;
}
