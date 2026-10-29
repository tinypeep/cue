#pragma once
#include <windows.h>

#include <string>
#include <vector>

// Kept separate from the engine: subsequent launches never load mpv.
class SingleInstance {
 public:
  enum class Result { Primary, Forwarded, Error };
  ~SingleInstance();
  SingleInstance() = default;
  SingleInstance(const SingleInstance&) = delete;
  SingleInstance& operator=(const SingleInstance&) = delete;
  Result route(const std::vector<std::string>& args);
  static bool decode(const COPYDATASTRUCT* data, std::vector<std::string>& files);

 private:
  HANDLE mutex_ = nullptr;
  bool owned_ = false;
};
