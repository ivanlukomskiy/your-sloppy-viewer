#include "Loader.h"

#include "DxfLoader.h"
#include "StepLoader.h"

#include <algorithm>
#include <cctype>
#include <system_error>

namespace
{
std::string lowerExtension(const std::filesystem::path& path)
{
  std::string extension = path.extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return extension;
}

LoadResult fail(const char* message)
{
  LoadResult result;
  result.error = message;
  return result;
}
}

bool isSupportedFile(const std::filesystem::path& path)
{
  const std::string extension = lowerExtension(path);
  return extension == ".step" || extension == ".stp" || extension == ".dxf";
}

LoadResult loadModel(const std::filesystem::path& path, LoadProgress& progress)
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

  const std::string extension = lowerExtension(path);
  if (extension == ".dxf")
  {
    return loadDxf(path, progress);
  }
  if (extension == ".step" || extension == ".stp")
  {
    return loadStep(path, progress);
  }
  return fail("unsupported file type (expected .step, .stp or .dxf)");
}
