#pragma once

#include "application.hpp"
#include <models/work_info.hpp>

class master : public application {
public:
    master(const work_info& work_info);
    void run() override;
    ~master() override;
};