#pragma once

#include <string>

namespace lune {
namespace gfx {

bool InitializeGraphicsContext(const char *name, std::string& err);
void DestroyGraphicsContext();

}
}
