#pragma once

#include <processors/application.hpp>
#include <models/work_info.hpp>

namespace processors {
    class master : public processors::application {
    public:
        master(const models::work_info& work_info);
        void run() override;
        ~master() override;
    };
}