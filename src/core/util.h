#pragma once
#include <string>
#include <string_view>
#include <vector>

#include "model.h"

// "1:24:12", "31:09", "08:42"
std::string fmt_time(double sec, bool force_hours = false);
std::string fmt_delay(double sec);  // "+0.3s", "-1.2s", "0.0s"
std::string file_name(std::string_view path);
std::string ext_lower(std::string_view path);
bool is_subtitle_path(std::string_view path);

// Human, non-technical track names: "Português", "English", "faixa 2"
std::string track_label(const Track& t, const std::vector<Track>& same_type);
SubKind sub_kind(const std::string& codec);
