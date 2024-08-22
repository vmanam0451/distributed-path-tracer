#include "pch.hpp"

#include "core/pbr.hpp"
#include "core/renderer.hpp"
#include "image/image_texture.hpp"
#include <spdlog/spdlog.h>

#include <aws/lambda-runtime/runtime.h>
#include <aws/core/Aws.h>

#include <models/work_info.hpp>
#include <application.hpp>
#include <master/master.hpp>
#include <worker/worker.hpp>

#include <nlohmann/json.hpp>
#include <memory>

using json = nlohmann::json;

using namespace math;

aws::lambda_runtime::invocation_response my_handler(aws::lambda_runtime::invocation_request const& request) {
	// Aws::SDKOptions options;
   	// Aws::InitAPI(options);
   	// {
    //   // make your SDK calls here.
   	// }
   	// Aws::ShutdownAPI(options);

	const std::string& payload = request.payload;
	json work_info_json = json::parse(payload);
	work_info info = work_info_json.get<work_info>();

	const std::string& worker_id = info.worker_id;
	std::unique_ptr<application> app;
	if (worker_id == "master") {
		app = std::make_unique<master>(info);
	}
	else {
		app = std::make_unique<worker>(info);
	}

	app->run();
	return aws::lambda_runtime::invocation_response::success("Render Complete!", "application/json");
}


int main() {
	aws::lambda_runtime::run_handler(my_handler);
	return 0;
}
