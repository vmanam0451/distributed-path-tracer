#pragma once

#include <pch.hpp>

namespace cloud {
    void s3_download_object(const std::string& bucket, const std::string& key, std::variant<std::filesystem::path, std::vector<uint8_t>>& output);
    void s3_upload_object(const std::string& bucket, const std::string& key, std::variant<std::filesystem::path, std::vector<uint8_t>>& input);
}