#include "App.h"

#include <Standard_Failure.hxx>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>

int main(int argc, char** argv)
{
  const bool wantsHelp =
    argc == 2 && (std::strcmp(argv[1], "-h") == 0 || std::strcmp(argv[1], "--help") == 0);
  if (argc > 2 || wantsHelp)
  {
    std::fprintf(wantsHelp ? stdout : stderr, "Usage: your-sloppy-viewer [file.step|file.stp]\n");
    return wantsHelp ? EXIT_SUCCESS : EXIT_FAILURE;
  }

  try
  {
    App app;
    return app.run(argc == 2 ? std::filesystem::path(argv[1]) : std::filesystem::path());
  }
  catch (const Standard_Failure& failure)
  {
    std::fprintf(stderr, "your-sloppy-viewer: OpenCascade error: %s\n", failure.GetMessageString());
  }
  catch (const std::exception& error)
  {
    std::fprintf(stderr, "your-sloppy-viewer: %s\n", error.what());
  }
  return EXIT_FAILURE;
}
