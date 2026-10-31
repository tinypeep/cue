#pragma once
#include <windows.h>
#include "../core/dib_surface.h"

struct WinDibOps {
  using DC = HDC;
  using Bitmap = HBITMAP;
  using Object = HGDIOBJ;
  static DC create_dc() { return CreateCompatibleDC(nullptr); }
  static Bitmap create_bitmap(int w, int h, void** bits) {
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    return CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, bits, nullptr, 0);
  }
  static Object select(DC dc, Object bitmap) { return SelectObject(dc, bitmap); }
  static bool valid(Object object) { return object && object != HGDI_ERROR; }
  static void delete_dc(DC dc) { DeleteDC(dc); }
  static void delete_bitmap(Bitmap bitmap) { DeleteObject(bitmap); }
};
using DibSurface = OwnedDibSurface<WinDibOps>;
