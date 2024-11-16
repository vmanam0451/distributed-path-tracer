#include "s3.hpp"

namespace cloud {
    void s3_download_object(const std::string& bucket, const std::string& key, std::variant<std::filesystem::path, std::vector<uint8_t>>& output) {
        spdlog::info("Attempting to download object from s3://{}/{}", bucket, key);
        Aws::S3::S3Client s3_client;

        Aws::S3::Model::GetObjectRequest object_request;
        object_request.SetBucket(bucket.c_str());
        object_request.SetKey(key.c_str());

        auto get_object_outcome = s3_client.GetObject(object_request);

        if (get_object_outcome.IsSuccess()) {
            Aws::IOStream& object_result = get_object_outcome.GetResultWithOwnership().GetBody();
            if (std::holds_alternative<std::filesystem::path>(output)) {
                const std::string& file_path = std::get<std::filesystem::path>(output).string();
                std::ofstream file(file_path, std::ios::binary);
                file << object_result.rdbuf();
                file.close();
            }
            else {
                std::vector<uint8_t>& data = std::get<std::vector<uint8_t>>(output);

                object_result.seekg(0, std::ios::end);
                data.resize(object_result.tellg());
                object_result.seekg(0, std::ios::beg);

                object_result.read(reinterpret_cast<char*>(data.data()), data.size());
            }
            spdlog::info("Downloaded object from s3://{}/{}", bucket, key);
        }
        else {
            auto error = get_object_outcome.GetError();
            spdlog::error("Error: Unable to download {} {}", bucket, key);
            spdlog::error("Error: {}: {}", error.GetExceptionName(), error.GetMessage());
        }
    }
}