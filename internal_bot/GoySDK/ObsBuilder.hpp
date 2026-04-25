#pragma once
#include "../pch.hpp"
#include "Config.hpp"
#include <vector>
#include <array>
#include <chrono>

namespace GoySDK {


struct RotMat {
    FVector forward, right, up;

   
    FVector Dot(const FVector& v) const {
        return {
            forward.X * v.X + forward.Y * v.Y + forward.Z * v.Z,
            right.X   * v.X + right.Y   * v.Y + right.Z   * v.Z,
            up.X      * v.X + up.Y      * v.Y + up.Z      * v.Z
        };
    }
};


struct PhysSnapshot {
    FVector pos{};
    FVector vel{};
    FVector angVel{};
    RotMat  rotMat{};
    FRotator rotator{};
};


struct PlayerSnapshot {
    PhysSnapshot phys;
    float  boost       = 0.f;  
    bool   isOnGround  = true;
    bool   hasFlipOrJump = true;
    bool   isDemoed    = false;
    bool   hasJumped   = false;
    int    teamIndex   = 0;    
    uint32_t carId     = 0;
    uint64_t traceCarKey = 0;
    bool   rawOnGround = true;
    bool   rawJumped = false;
    bool   rawDoubleJumped = false;
    bool   rawHasAvailableDoubleJump = false;
    bool   rawWithinDodgeTimeWindow = false;
    bool   rawIsDemoed = false;
    bool   rawHasWorldContact = false;
    FVector rawWorldContactNormal{};
    std::array<float, 8> prevAction{};
};


struct BoostPadState {
    bool  available = true;
    float timer     = 0.f; 
};

class ObsBuilder {
public:
    explicit ObsBuilder(const Config& cfg);

   
    std::vector<float> BuildObs(
        const PlayerSnapshot& localPlayer,
        const PhysSnapshot& ball,
        const std::vector<PlayerSnapshot>& allPlayers,
        const std::array<BoostPadState, 34>& pads);

   
    static RotMat RotatorToMatrix(const FRotator& rot);

   
    static PhysSnapshot InvertPhys(const PhysSnapshot& phys, bool shouldInvert);

private:
    void AddPlayerToObs(std::vector<float>& obs, const PlayerSnapshot& player,
                        bool inv, const PhysSnapshot& ball);

    static constexpr float POS_COEF     = 1.f / 5000.f;
    static constexpr float VEL_COEF     = 1.f / 2300.f;
    static constexpr float ANG_VEL_COEF = 1.f / 3.f;

    Config cfg_;
};

} 
