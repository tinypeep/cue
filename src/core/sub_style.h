// SubtitleRenderer integration: our five controls → libass properties via mpv.
#pragma once
#include <string>
#include <vector>

#include "model.h"

struct PropSet {
  std::vector<std::string> names;  // first is preferred; later ones are older mpv names
  std::string value;
};

extern const char* const kSubFontFamily[3];  // "Cue Omni", "Cue Clean", "Cue Readable"
extern const char* const kSubFontLabel[3];   // "Omni", "Clean", "Readable"

std::vector<PropSet> sub_style_props(const SubStyle& st, SubKind kind);
// What the style editor may show for the current track kind.
struct StyleCaps { bool font = true, size = true, opacity = true, position = true, ass_toggle = false; };
StyleCaps style_caps(const SubStyle& st, SubKind kind);
