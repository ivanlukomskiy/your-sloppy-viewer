#pragma once

#include "Loader.h"

LoadResult loadDxf(const std::filesystem::path& path, LoadProgress& progress);
