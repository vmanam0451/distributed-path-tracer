#include "s3.hpp"

void s3_download_object(const std::string& bucket, const std::string& key, std::variant<std::filesystem::path, std::vector<uint8_t>>& output) {
    Aws::S3::S3Client s3_client;

    Aws::S3::Model::GetObjectRequest object_request;
    object_request.SetBucket(bucket.c_str());
    object_request.SetKey(key.c_str());

    auto get_object_outcome = s3_client.GetObject(object_request);

    if (get_object_outcome.IsSuccess()) {
        auto& object_result = get_object_outcome.GetResultWithOwnership().GetBody();
        if (std::holds_alternative<std::filesystem::path>(output)) {
            std::ofstream file(std::get<std::filesystem::path>(output), std::ios::binary);
            file << object_result.rdbuf();
        }
        else {
            std::vector<uint8_t> buffer;
            buffer.reserve(object_result.tellg());
            object_result.seekg(0, std::ios::beg);
            buffer.assign(std::istreambuf_iterator<char>(object_result), std::istreambuf_iterator<char>());
            output = buffer;
        }
        spdlog::info("Downloaded object from s3://{}/{}", bucket, key);
    }
    else {
        auto error = get_object_outcome.GetError();
        spdlog::error("Error: {}: {}", error.GetExceptionName(), error.GetMessage());
    }
}