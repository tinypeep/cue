#pragma once
#include <windows.h>
#include <string>
void open_licenses(HWND owner, const std::wstring& directory);
bool licenses_message(MSG* message);
