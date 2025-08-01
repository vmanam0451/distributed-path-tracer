#pragma once

#include <aws/core/Aws.h>
#include <aws/lambda-runtime/runtime.h>
#include <aws/s3/S3Client.h>
#include <aws/s3/model/GetObjectRequest.h>
#include <aws/s3/model/PutObjectRequest.h>
#include <cgltf/custom_cgltf.h>
#include <concurrentqueue/concurrentqueue.h>
#include <spdlog/spdlog.h>

#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <variant>
#include <vector>