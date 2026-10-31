#pragma once
#include <atomic>
#include <memory>
#include <string>
#include <vector>
#include <cstdint>
struct FileThumbnailJob { std::atomic<bool> done{false}; std::vector<uint32_t> pixels; };
std::shared_ptr<FileThumbnailJob> request_file_thumbnail(std::string path);
