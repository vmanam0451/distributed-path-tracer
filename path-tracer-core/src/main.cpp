#include "models/messaging.hpp"
#include "models/work_info.hpp"
#include "pch.hpp"
#include "processors/application.hpp"
#include "processors/master/master.hpp"
#include "processors/worker/worker.hpp"

using json = nlohmann::json;

aws::lambda_runtime::invocation_response my_handler(aws::lambda_runtime::invocation_request const &request)
{
    Aws::SDKOptions options;
    try
    {
        Aws::InitAPI(options);

        const std::string &payload = request.payload;
        json worker_info_json = json::parse(payload);
        models::worker_info info = worker_info_json.get<models::worker_info>();

        const std::string &worker_id = info.worker_id;
        std::unique_ptr<processors::application> app;

        if (worker_id == models::MASTER_ID)
            app = std::make_unique<processors::master>(info);
        else
            app = std::make_unique<processors::worker>(info);

        app->run();
        Aws::ShutdownAPI(options);
        return aws::lambda_runtime::invocation_response::success("Render Complete!", "application/json");
    }
    catch (const std::system_error &e)
    {
        spdlog::error("System error: {}, error code: {}, category: {}", e.what(), e.code().value(),
                      e.code().category().name());
        Aws::ShutdownAPI(options);
        return aws::lambda_runtime::invocation_response::failure(fmt::format("System error: {}", e.what()),
                                                                 "SystemError");
    }
    catch (const std::exception &e)
    {
        spdlog::error("Exception caught: {}", e.what());
        Aws::ShutdownAPI(options);
        return aws::lambda_runtime::invocation_response::failure(fmt::format("Exception: {}", e.what()),
                                                                 "RuntimeError");
    }
    catch (...)
    {
        spdlog::error("Unknown exception caught");
        Aws::ShutdownAPI(options);
        return aws::lambda_runtime::invocation_response::failure("Unknown exception occurred", "UnknownError");
    }
}

int main()
{
    aws::lambda_runtime::run_handler(my_handler);
    return 0;
}
