#pragma once

#include "application.hpp"
#include <models/work_info.hpp>
#include <cloud/s3.hpp>

class worker : public application {
public:
    worker(const work_info& work_info);
    void run() override;
    ~worker() override;
};