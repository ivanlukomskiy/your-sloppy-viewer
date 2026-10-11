#include "DxfLoader.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace
{
using Clock = std::chrono::steady_clock;
using Color = std::array<std::uint8_t, 4>;

constexpr double kPi = 3.14159265358979323846;
constexpr int kByLayer = 256;

struct Point
{
  double x = 0.0;
  double y = 0.0;
};

struct PolyVertex
{
  Point point;
  double bulge = 0.0;
};

struct Polyline
{
  std::vector<PolyVertex> vertices;
  bool closed = false;
  Color color{};
};

struct Arc
{
  Point center;
  double radius = 0.0;
  double start = 0.0;
  double sweep = 2.0 * kPi;
  Color color{};
};

struct Label
{
  std::string text;
  Point insertion;
  Point alignment;
  bool hasAlignment = false;
  double height = 1.0;
  double rotation = 0.0;
  int horizontal = 0;
  int vertical = 0;
  Color color{};
};

struct Layer
{
  int color = 7;
  bool hidden = false;
};

struct Drawing
{
  std::vector<Polyline> polylines;
  std::vector<Arc> arcs;
  std::vector<Label> labels;
  std::map<std::string, Layer> layers;
  std::map<std::string, int> skipped;
  int units = 0;
};

Color indexedColor(int index)
{
  static const Color basic[10] = {
    {255, 255, 255, 255}, {255, 64, 64, 255},   {255, 235, 80, 255},  {80, 220, 100, 255},
    {80, 220, 235, 255},  {90, 130, 255, 255},  {235, 100, 235, 255}, {232, 234, 238, 255},
    {140, 144, 152, 255}, {192, 196, 204, 255},
  };
  if (index < 1 || index > 255)
  {
    return basic[7];
  }
  if (index < 10)
  {
    return basic[index];
  }
  if (index >= 250)
  {
    const auto gray = static_cast<std::uint8_t>(60 + (index - 250) * 36);
    return {gray, gray, gray, 255};
  }

  static const double levels[5] = {1.0, 0.8, 0.6, 0.5, 0.3};
  const double hue = ((index / 10) - 1) * 15.0;
  const int digit = index % 10;
  const double value = std::max(levels[digit / 2], 0.45);
  const double saturation = digit % 2 == 0 ? 1.0 : 0.5;

  const double sector = hue / 60.0;
  const double chroma = value * saturation;
  const double x = chroma * (1.0 - std::abs(std::fmod(sector, 2.0) - 1.0));
  double r = 0.0, g = 0.0, b = 0.0;
  switch (static_cast<int>(sector) % 6)
  {
    case 0: r = chroma; g = x; break;
    case 1: r = x; g = chroma; break;
    case 2: g = chroma; b = x; break;
    case 3: g = x; b = chroma; break;
    case 4: r = x; b = chroma; break;
    default: r = chroma; b = x; break;
  }
  const double base = value - chroma;
  const auto channel = [base](double c) {
    return static_cast<std::uint8_t>(std::lround((c + base) * 255.0));
  };
  return {channel(r), channel(g), channel(b), 255};
}

std::string_view trim(std::string_view text)
{
  while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
  {
    text.remove_prefix(1);
  }
  while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r'))
  {
    text.remove_suffix(1);
  }
  return text;
}

double toDouble(std::string_view text)
{
  text = trim(text);
  double value = 0.0;
  std::from_chars(text.data(), text.data() + text.size(), value);
  return value;
}

int toInt(std::string_view text)
{
  text = trim(text);
  int value = 0;
  std::from_chars(text.data(), text.data() + text.size(), value);
  return value;
}

bool isValidUtf8(std::string_view text)
{
  for (std::size_t i = 0; i < text.size();)
  {
    const auto byte = static_cast<unsigned char>(text[i]);
    const std::size_t length = byte < 0x80 ? 1 : (byte >> 5) == 0x6 ? 2 : (byte >> 4) == 0xE ? 3
                               : (byte >> 3) == 0x1E ? 4 : 0;
    if (length == 0 || i + length > text.size())
    {
      return false;
    }
    for (std::size_t k = 1; k < length; ++k)
    {
      if ((static_cast<unsigned char>(text[i + k]) & 0xC0) != 0x80)
      {
        return false;
      }
    }
    i += length;
  }
  return true;
}

std::string decodeText(std::string_view raw)
{
  std::string text;
  if (isValidUtf8(raw))
  {
    text.assign(raw);
  }
  else
  {
    for (const char c : raw)
    {
      const auto byte = static_cast<unsigned char>(c);
      if (byte < 0x80)
      {
        text.push_back(c);
      }
      else
      {
        text.push_back(static_cast<char>(0xC0 | (byte >> 6)));
        text.push_back(static_cast<char>(0x80 | (byte & 0x3F)));
      }
    }
  }

  static const std::pair<const char*, const char*> symbols[] = {
    {"%%d", "°"}, {"%%D", "°"}, {"%%c", "⌀"}, {"%%C", "⌀"}, {"%%p", "±"}, {"%%P", "±"}};
  for (const auto& [code, replacement] : symbols)
  {
    for (std::size_t at = text.find(code); at != std::string::npos; at = text.find(code, at))
    {
      text.replace(at, 3, replacement);
    }
  }
  return text;
}

class Reader
{
public:
  Reader(std::string_view data, LoadProgress& progress) : data_(data), progress_(progress) {}

  bool next()
  {
    std::string_view codeLine;
    if (!line(codeLine) || !line(value))
    {
      return false;
    }
    code = toInt(codeLine);
    if ((++count_ & 0x3FFF) == 0)
    {
      progress_.fraction.store(static_cast<float>(position_) / static_cast<float>(data_.size()));
    }
    return true;
  }

  int code = 0;
  std::string_view value;

private:
  bool line(std::string_view& out)
  {
    if (position_ >= data_.size())
    {
      return false;
    }
    const std::size_t end = data_.find('\n', position_);
    const std::size_t stop = end == std::string_view::npos ? data_.size() : end;
    out = data_.substr(position_, stop - position_);
    if (!out.empty() && out.back() == '\r')
    {
      out.remove_suffix(1);
    }
    position_ = stop + 1;
    return true;
  }

  std::string_view data_;
  LoadProgress& progress_;
  std::size_t position_ = 0;
  std::size_t count_ = 0;
};

class Parser
{
public:
  Parser(std::string_view data, LoadProgress& progress) : reader_(data, progress) {}

  Drawing parse()
  {
    bool more = reader_.next();
    while (more)
    {
      if (reader_.code == 0 && trim(reader_.value) == "SECTION" && reader_.next() && reader_.code == 2)
      {
        const std::string name(trim(reader_.value));
        more = reader_.next();
        if (name == "HEADER")
        {
          more = header();
        }
        else if (name == "TABLES")
        {
          more = tables();
        }
        else if (name == "ENTITIES")
        {
          more = entities();
        }
        continue;
      }
      more = reader_.next();
    }
    return std::move(drawing_);
  }

private:
  bool atSectionEnd() const { return reader_.code == 0 && trim(reader_.value) == "ENDSEC"; }

  bool header()
  {
    std::string variable;
    bool more = true;
    while (more && !atSectionEnd())
    {
      if (reader_.code == 9)
      {
        variable.assign(trim(reader_.value));
      }
      else if (variable == "$INSUNITS" && reader_.code == 70)
      {
        drawing_.units = toInt(reader_.value);
      }
      more = reader_.next();
    }
    return more;
  }

  bool tables()
  {
    bool more = true;
    while (more && !atSectionEnd())
    {
      if (reader_.code == 0 && trim(reader_.value) == "LAYER")
      {
        std::string name;
        Layer layer;
        more = reader_.next();
        while (more && reader_.code != 0)
        {
          if (reader_.code == 2)
          {
            name.assign(trim(reader_.value));
          }
          else if (reader_.code == 62)
          {
            const int color = toInt(reader_.value);
            layer.color = std::abs(color);
            layer.hidden = layer.hidden || color < 0;
          }
          else if (reader_.code == 70)
          {
            layer.hidden = layer.hidden || (toInt(reader_.value) & 1) != 0;
          }
          more = reader_.next();
        }
        if (!name.empty())
        {
          drawing_.layers[name] = layer;
        }
        continue;
      }
      more = reader_.next();
    }
    return more;
  }

  struct Common
  {
    std::string layer = "0";
    int color = kByLayer;
    bool paperSpace = false;
  };

  bool common(Common& fields)
  {
    switch (reader_.code)
    {
      case 8:
        fields.layer.assign(trim(reader_.value));
        return true;
      case 62:
        fields.color = toInt(reader_.value);
        return true;
      case 67:
        fields.paperSpace = toInt(reader_.value) != 0;
        return true;
      default:
        return false;
    }
  }

  bool visible(const Common& fields, Color& color) const
  {
    if (fields.paperSpace)
    {
      return false;
    }
    Layer layer;
    if (const auto found = drawing_.layers.find(fields.layer); found != drawing_.layers.end())
    {
      layer = found->second;
    }
    if (layer.hidden)
    {
      return false;
    }
    const bool own = fields.color > 0 && fields.color < kByLayer;
    color = indexedColor(own ? fields.color : layer.color);
    return true;
  }

  bool entities()
  {
    bool more = true;
    while (more && !atSectionEnd())
    {
      if (reader_.code != 0)
      {
        more = reader_.next();
        continue;
      }
      const std::string type(trim(reader_.value));
      more = reader_.next();
      if (type == "LINE")
      {
        more = line(more);
      }
      else if (type == "CIRCLE" || type == "ARC")
      {
        more = arc(more, type == "ARC");
      }
      else if (type == "POLYLINE")
      {
        more = polyline(more);
      }
      else if (type == "LWPOLYLINE")
      {
        more = lightPolyline(more);
      }
      else if (type == "TEXT")
      {
        more = text(more);
      }
      else if (!type.empty())
      {
        ++drawing_.skipped[type];
      }
    }
    return more;
  }

  bool line(bool more)
  {
    Common fields;
    Point a, b;
    for (; more && reader_.code != 0; more = reader_.next())
    {
      if (common(fields)) continue;
      switch (reader_.code)
      {
        case 10: a.x = toDouble(reader_.value); break;
        case 20: a.y = toDouble(reader_.value); break;
        case 11: b.x = toDouble(reader_.value); break;
        case 21: b.y = toDouble(reader_.value); break;
        default: break;
      }
    }
    Polyline result;
    if (visible(fields, result.color))
    {
      result.vertices = {{a, 0.0}, {b, 0.0}};
      drawing_.polylines.push_back(std::move(result));
    }
    return more;
  }

  bool arc(bool more, bool partial)
  {
    Common fields;
    Arc result;
    double startDegrees = 0.0, endDegrees = 360.0;
    for (; more && reader_.code != 0; more = reader_.next())
    {
      if (common(fields)) continue;
      switch (reader_.code)
      {
        case 10: result.center.x = toDouble(reader_.value); break;
        case 20: result.center.y = toDouble(reader_.value); break;
        case 40: result.radius = toDouble(reader_.value); break;
        case 50: startDegrees = toDouble(reader_.value); break;
        case 51: endDegrees = toDouble(reader_.value); break;
        default: break;
      }
    }
    if (partial)
    {
      double sweep = std::fmod(endDegrees - startDegrees, 360.0);
      if (sweep <= 0.0)
      {
        sweep += 360.0;
      }
      result.start = startDegrees * kPi / 180.0;
      result.sweep = sweep * kPi / 180.0;
    }
    if (result.radius > 0.0 && visible(fields, result.color))
    {
      drawing_.arcs.push_back(result);
    }
    return more;
  }

  bool polyline(bool more)
  {
    Common fields;
    Polyline result;
    int flags = 0;
    for (; more && reader_.code != 0; more = reader_.next())
    {
      if (common(fields)) continue;
      if (reader_.code == 70)
      {
        flags = toInt(reader_.value);
      }
    }
    result.closed = (flags & 1) != 0;

    while (more && reader_.code == 0 && trim(reader_.value) == "VERTEX")
    {
      PolyVertex vertex;
      for (more = reader_.next(); more && reader_.code != 0; more = reader_.next())
      {
        switch (reader_.code)
        {
          case 10: vertex.point.x = toDouble(reader_.value); break;
          case 20: vertex.point.y = toDouble(reader_.value); break;
          case 42: vertex.bulge = toDouble(reader_.value); break;
          default: break;
        }
      }
      result.vertices.push_back(vertex);
    }
    if (more && reader_.code == 0 && trim(reader_.value) == "SEQEND")
    {
      for (more = reader_.next(); more && reader_.code != 0; more = reader_.next())
      {
      }
    }

    if ((flags & (16 | 64)) != 0)
    {
      ++drawing_.skipped["POLYLINE (mesh)"];
    }
    else if (result.vertices.size() >= 2 && visible(fields, result.color))
    {
      drawing_.polylines.push_back(std::move(result));
    }
    return more;
  }

  bool lightPolyline(bool more)
  {
    Common fields;
    Polyline result;
    for (; more && reader_.code != 0; more = reader_.next())
    {
      if (common(fields)) continue;
      switch (reader_.code)
      {
        case 70: result.closed = (toInt(reader_.value) & 1) != 0; break;
        case 10: result.vertices.push_back({{toDouble(reader_.value), 0.0}, 0.0}); break;
        case 20:
          if (!result.vertices.empty()) result.vertices.back().point.y = toDouble(reader_.value);
          break;
        case 42:
          if (!result.vertices.empty()) result.vertices.back().bulge = toDouble(reader_.value);
          break;
        default: break;
      }
    }
    if (result.vertices.size() >= 2 && visible(fields, result.color))
    {
      drawing_.polylines.push_back(std::move(result));
    }
    return more;
  }

  bool text(bool more)
  {
    Common fields;
    Label result;
    for (; more && reader_.code != 0; more = reader_.next())
    {
      if (common(fields)) continue;
      switch (reader_.code)
      {
        case 1: result.text = decodeText(reader_.value); break;
        case 10: result.insertion.x = toDouble(reader_.value); break;
        case 20: result.insertion.y = toDouble(reader_.value); break;
        case 11: result.alignment.x = toDouble(reader_.value); result.hasAlignment = true; break;
        case 21: result.alignment.y = toDouble(reader_.value); result.hasAlignment = true; break;
        case 40: result.height = toDouble(reader_.value); break;
        case 50: result.rotation = toDouble(reader_.value); break;
        case 72: result.horizontal = toInt(reader_.value); break;
        case 73: result.vertical = toInt(reader_.value); break;
        default: break;
      }
    }
    if (!result.text.empty() && result.height > 0.0 && visible(fields, result.color))
    {
      drawing_.labels.push_back(std::move(result));
    }
    return more;
  }

  Reader reader_;
  Drawing drawing_;
};

class Builder
{
public:
  Builder(const Drawing& drawing, Scene& scene) : drawing_(drawing), scene_(scene) {}

  void build()
  {
    measure();
    origin_ = {(min_.x + max_.x) * 0.5, (min_.y + max_.y) * 0.5};
    tolerance_ = std::max(std::hypot(max_.x - min_.x, max_.y - min_.y) * 1.0e-5, 1.0e-9);
    min_ = {1.0e300, 1.0e300};
    max_ = {-1.0e300, -1.0e300};

    for (const Polyline& polyline : drawing_.polylines)
    {
      const std::size_t count = polyline.vertices.size();
      const std::size_t spans = polyline.closed ? count : count - 1;
      for (std::size_t i = 0; i < spans; ++i)
      {
        const PolyVertex& from = polyline.vertices[i];
        const Point& to = polyline.vertices[(i + 1) % count].point;
        if (std::abs(from.bulge) < 1.0e-12)
        {
          segment(from.point, to, polyline.color);
        }
        else
        {
          bulge(from.point, to, from.bulge, polyline.color);
        }
      }
    }
    for (const Arc& arc : drawing_.arcs)
    {
      sweep(arc.center, arc.radius, arc.start, arc.sweep, arc.color);
    }
    const Point sizeMin = min_;
    const Point sizeMax = max_;
    for (const Label& label : drawing_.labels)
    {
      const bool aligned = label.hasAlignment && (label.horizontal != 0 || label.vertical != 0);
      const Point anchor = aligned ? label.alignment : label.insertion;
      extend(anchor);

      TextItem item;
      item.text = label.text;
      item.x = static_cast<float>(anchor.x - origin_.x);
      item.y = static_cast<float>(anchor.y - origin_.y);
      item.height = static_cast<float>(label.height);
      item.rotation = static_cast<float>(label.rotation);
      item.horizontal = label.horizontal == 1 || label.horizontal == 4 ? TextItem::Horizontal::Center
                        : label.horizontal == 2 ? TextItem::Horizontal::Right
                                                : TextItem::Horizontal::Left;
      item.vertical = label.horizontal == 4 || label.vertical == 2 ? TextItem::Vertical::Middle
                      : label.vertical == 1 ? TextItem::Vertical::Bottom
                      : label.vertical == 3 ? TextItem::Vertical::Top
                                            : TextItem::Vertical::Baseline;
      std::copy(label.color.begin(), label.color.end(), item.color);
      scene_.texts.push_back(std::move(item));
    }

    scene_.boundsMin = {static_cast<float>(min_.x - origin_.x), static_cast<float>(min_.y - origin_.y), 0.0f};
    scene_.boundsMax = {static_cast<float>(max_.x - origin_.x), static_cast<float>(max_.y - origin_.y), 0.0f};
    if (sizeMax.x >= sizeMin.x)
    {
      scene_.size = {sizeMax.x - sizeMin.x, sizeMax.y - sizeMin.y, 0.0};
    }
    scene_.flat = true;
  }

private:
  void extend(const Point& point)
  {
    min_ = {std::min(min_.x, point.x), std::min(min_.y, point.y)};
    max_ = {std::max(max_.x, point.x), std::max(max_.y, point.y)};
  }

  void measure()
  {
    for (const Polyline& polyline : drawing_.polylines)
    {
      for (const PolyVertex& vertex : polyline.vertices)
      {
        extend(vertex.point);
      }
    }
    for (const Arc& arc : drawing_.arcs)
    {
      extend({arc.center.x - arc.radius, arc.center.y - arc.radius});
      extend({arc.center.x + arc.radius, arc.center.y + arc.radius});
    }
    for (const Label& label : drawing_.labels)
    {
      extend(label.insertion);
    }
  }

  void segment(const Point& a, const Point& b, const Color& color)
  {
    extend(a);
    extend(b);
    EdgeSegment edge{};
    edge.start[0] = static_cast<float>(a.x - origin_.x);
    edge.start[1] = static_cast<float>(a.y - origin_.y);
    edge.end[0] = static_cast<float>(b.x - origin_.x);
    edge.end[1] = static_cast<float>(b.y - origin_.y);
    std::copy(color.begin(), color.end(), edge.color);
    scene_.edges.push_back(edge);
  }

  void sweep(const Point& center, double radius, double start, double angle, const Color& color)
  {
    const double ratio = std::clamp(1.0 - tolerance_ / radius, -1.0, 1.0);
    const double step = std::clamp(2.0 * std::acos(ratio), 2.0 * kPi / 720.0, 2.0 * kPi / 12.0);
    const int steps = std::max(1, static_cast<int>(std::ceil(std::abs(angle) / step)));
    Point previous{center.x + radius * std::cos(start), center.y + radius * std::sin(start)};
    for (int i = 1; i <= steps; ++i)
    {
      const double at = start + angle * i / steps;
      const Point current{center.x + radius * std::cos(at), center.y + radius * std::sin(at)};
      segment(previous, current, color);
      previous = current;
    }
  }

  void bulge(const Point& a, const Point& b, double value, const Color& color)
  {
    const double chord = std::hypot(b.x - a.x, b.y - a.y);
    if (chord < 1.0e-12)
    {
      return;
    }
    const double angle = 4.0 * std::atan(value);
    const double offset = chord * 0.5 * (1.0 - value * value) / (2.0 * value);
    const Point center{(a.x + b.x) * 0.5 - (b.y - a.y) / chord * offset,
                       (a.y + b.y) * 0.5 + (b.x - a.x) / chord * offset};
    const double radius = std::hypot(a.x - center.x, a.y - center.y);
    sweep(center, radius, std::atan2(a.y - center.y, a.x - center.x), angle, color);
  }

  const Drawing& drawing_;
  Scene& scene_;
  Point origin_;
  Point min_{1.0e300, 1.0e300};
  Point max_{-1.0e300, -1.0e300};
  double tolerance_ = 1.0e-6;
};

const char* unitName(int units)
{
  switch (units)
  {
    case 1: return "in";
    case 2: return "ft";
    case 4: return "mm";
    case 5: return "cm";
    case 6: return "m";
    default: return "";
  }
}

LoadResult fail(std::string message)
{
  LoadResult result;
  result.error = std::move(message);
  return result;
}
}

LoadResult loadDxf(const std::filesystem::path& path, LoadProgress& progress)
{
  LoadResult result;
  auto start = Clock::now();

  std::ifstream file(path, std::ios::binary);
  if (!file)
  {
    return fail("the file cannot be opened for reading");
  }
  std::string data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  if (data.starts_with("AutoCAD Binary DXF"))
  {
    return fail("binary DXF files are not supported");
  }

  Drawing drawing = Parser(data, progress).parse();
  result.timings.parseMs = std::chrono::duration<double, std::milli>(Clock::now() - start).count();

  start = Clock::now();
  progress.fraction.store(0.0f);
  progress.stage.store(LoadProgress::Stage::Preparing);

  for (const auto& [type, count] : drawing.skipped)
  {
    result.note += (result.note.empty() ? "" : ", ") + type + " ×" + std::to_string(count);
  }
  if (drawing.polylines.empty() && drawing.arcs.empty() && drawing.labels.empty())
  {
    return fail(result.note.empty() ? "not a valid DXF file, or it contains no drawing"
                                    : "it contains only unsupported entities (" + result.note + ")");
  }

  Scene scene;
  Builder(drawing, scene).build();
  scene.unit = unitName(drawing.units);
  result.timings.buildMs = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
  result.scene = std::move(scene);
  return result;
}
