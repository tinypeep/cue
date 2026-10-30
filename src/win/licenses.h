#pragma once
#include <windows.h>
#include <string>
void open_licenses(HWND owner, const std::wstring& directory);
void relocalize_licenses();  // the interface language changed: the open window follows
bool licenses_message(MSG* message);
