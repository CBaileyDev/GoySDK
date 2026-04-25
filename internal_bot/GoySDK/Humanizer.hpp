#pragma once
#include <cmath>
#include <random>
#include <array>

namespace GoySDK {


class Humanizer {
public:
    // P2/01: deadzone defaults to 0.0f because the input here is the policy's
    // discrete output, not analog physical-controller noise. Callers who want
    // controller-style input filtering can still pass a non-zero value.
    Humanizer(float smoothFactor = 0.80f, float deadzone = 0.00f, float jitter = 0.015f)
        : smooth_(smoothFactor), dead_(deadzone), jitter_(jitter),
          rng_(std::random_device{}()), dist_(-1.f, 1.f) {}

    float Smooth(float target, float& prev) const {
        // P2/01: deadzone applies to the TARGET (intent), not to the smoothed
        // value. Previously it ran post-smoothing, which clipped intentional
        // small intermediate values produced by the EMA ramp-up.
        if (fabsf(target) < dead_) target = 0.f;

        const float alpha = 1.f - smooth_;
        float val = prev + (target - prev) * alpha;

        if (fabsf(val) > 0.95f && jitter_ > 0.f) {
            val += dist_(rng_) * jitter_;
            if (val >  1.f) val =  1.f;
            if (val < -1.f) val = -1.f;
        }

        prev = val;
        return val;
    }

   
    void ProcessAnalog(float& throttle, float& steer, float& pitch, float& yaw, float& roll) {
        throttle = Smooth(throttle, prev_[0]);
        steer    = Smooth(steer,    prev_[1]);
        pitch    = Smooth(pitch,    prev_[2]);
        yaw      = Smooth(yaw,     prev_[3]);
        roll     = Smooth(roll,    prev_[4]);
    }

    void Reset() { prev_.fill(0.f); }

private:
    float smooth_, dead_, jitter_;
    mutable std::mt19937 rng_;
    mutable std::uniform_real_distribution<float> dist_;
    std::array<float, 5> prev_{};
};

} 
