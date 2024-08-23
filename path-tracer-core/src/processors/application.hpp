#pragma once

namespace processors {
    class application {
    public:
        virtual void run() = 0;
        virtual ~application() {}
    };
}