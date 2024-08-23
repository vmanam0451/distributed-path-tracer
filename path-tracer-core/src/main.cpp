#include "pch.hpp"

#include <models/work_info.hpp>
#include <processors/application.hpp>
#include <processors/master/master.hpp>
#include <processors/worker/worker.hpp>

using json = nlohmann::json;

aws::lambda_runtime::invocation_response my_handler(aws::lambda_runtime::invocation_request const& request) {
	Aws::SDKOptions options;
   	Aws::InitAPI(options);
   
	const std::string& payload = request.payload;
	json work_info_json = json::parse(payload);
	models::work_info info = work_info_json.get<models::work_info>();

	const std::string& worker_id = info.worker_id;
	std::unique_ptr<processors::application> app;
	if (worker_id == "master") {
		app = std::make_unique<processors::master>(info);
	}
	else {
		app = std::make_unique<processors::worker>(info);
	}

	app->run();
	Aws::ShutdownAPI(options);
	return aws::lambda_runtime::invocation_response::success("Render Complete!", "application/json");
}


int main() {
	aws::lambda_runtime::run_handler(my_handler);
	return 0;
}
