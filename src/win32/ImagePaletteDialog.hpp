#pragma once
#include <windows.h>
#include "triggerlab/ImageColor.hpp"
namespace triggerlab {
std::optional<ImageColor> showImagePalette(HWND owner,const Frame& image,bool darkTheme=false);
#ifdef TRIGGERLAB_UI_TESTS
bool imagePaletteDialogSmoke();
#endif
}
