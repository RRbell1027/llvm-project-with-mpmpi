#pragma once

#include <vector>
#include <cstdint>

namespace npu {
    std::vector<void*>& waiting_for_result();
    void send_input(uint32_t size, void* input);
}