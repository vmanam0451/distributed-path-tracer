#include "models/messaging.hpp"
#include "models/work_info.hpp"
#include "pch.hpp"
#include "processors/application.hpp"
#include "processors/master/master.hpp"
#include "processors/worker/worker.hpp"
#include <stdexcept>

using json = nlohmann::json;

void run()
{
    Aws::SDKOptions options;
    try
    {
        Aws::InitAPI(options);

        const char *env_p = std::getenv("WORKER_INFO");
        if (env_p == nullptr)
        {
            throw std::runtime_error("WORKER_INFO environment variable is not set");
        }

        const std::string &payload = env_p;
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
    }
    catch (const std::exception &e)
    {
        spdlog::error("Exception caught: {}", e.what());
        Aws::ShutdownAPI(options);
        std::runtime_error ex(fmt::format("Exception caught: {}", e.what()));
        throw ex;
    }
    catch (...)
    {
        spdlog::error("Unknown exception caught");
        Aws::ShutdownAPI(options);
        throw std::runtime_error("Unknown exception caught");
    }
}

int main()
{
    run();
    return 0;
}
