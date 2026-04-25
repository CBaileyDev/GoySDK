#pragma once

namespace RLInference {

struct ActionOutput {
    float throttle = 0.0f;
    float steer = 0.0f;
    float pitch = 0.0f;
    float yaw = 0.0f;
    float roll = 0.0f;
    bool jump = false;
    bool boost = false;
    bool handbrake = false;
};

} // namespace RLInference
