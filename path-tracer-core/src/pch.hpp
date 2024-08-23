#pragma once

#include <string>
#include <filesystem>
#include <variant>
#include <spdlog/spdlog.h>
#include <map>
#include <string>
#include <vector>
#include <memory>
#include <fstream>

#include <cgltf/custom_cgltf.h>

#include <nlohmann/json.hpp>

#include <aws/core/Aws.h>
#include <aws/lambda-runtime/runtime.h>
#include <aws/s3/S3Client.h>
#include <aws/s3/model/GetObjectRequest.h>