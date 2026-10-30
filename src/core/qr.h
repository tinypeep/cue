// A small QR code encoder for the remote's pairing screen: byte mode, versions 1-10, error
// correction L or M. Enough for a LAN address with a pairing code, and drawn as pixel art.
// Follows ISO/IEC 18004 (the structure of Project Nayuki's reference encoder, MIT).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct QrCode {
  int size = 0;                  // modules per side (21 + 4 * (version - 1)); 0 when encoding failed
  int version = 0, mask = -1;
  std::vector<uint8_t> modules;  // size * size, 1 = dark
  bool dark(int x, int y) const { return x >= 0 && y >= 0 && x < size && y < size && modules[(size_t)y * size + x]; }
};

enum class QrEcc { L, M };

// mask -1 picks the mask with the lowest penalty (the standard's rule); 0..7 forces one (tests).
// min_version lets tests ask for a bigger symbol than needed.
QrCode qr_encode(const std::string& bytes, QrEcc ecc = QrEcc::M, int mask = -1, int min_version = 1);
