#include "ObsBuilder.hpp"
#include <cmath>

namespace GoySDK {


extern const FVector BOOST_LOCATIONS[34];


static constexpr int INV_PAD_ORDER[34] = {
    33, 31, 32, 29, 30, 27, 28, 26, 24, 25,
    22, 23, 19, 20, 21, 18, 17, 16, 15, 12,
    13, 14, 10, 11,  8,  9,  7,  5,  6,  3,
     4,  1,  2,  0
};


static inline void PushVec(std::vector<float>& obs, const FVector& v, float coef) {
    obs.push_back(v.X * coef);
    obs.push_back(v.Y * coef);
    obs.push_back(v.Z * coef);
}

static inline void PushVecRaw(std::vector<float>& obs, const FVector& v) {
    obs.push_back(v.X);
    obs.push_back(v.Y);
    obs.push_back(v.Z);
}


ObsBuilder::ObsBuilder(const Config& cfg) : cfg_(cfg) {}

RotMat ObsBuilder::RotatorToMatrix(const FRotator& rot) {
   
    constexpr float URU_TO_RAD = 3.14159265358979f / 32768.f;

    float pitch = static_cast<float>(rot.Pitch) * URU_TO_RAD;
    float yaw   = static_cast<float>(rot.Yaw)   * URU_TO_RAD;
    float roll  = static_cast<float>(rot.Roll)  * URU_TO_RAD;

    float cp = cosf(pitch), sp = sinf(pitch);
    float cy = cosf(yaw),   sy = sinf(yaw);
    float cr = cosf(roll),  sr = sinf(roll);

    RotMat m;
   
    m.forward = { cp * cy,  cp * sy,  sp };
   
    m.right = { sr * sp * cy - cr * sy,  sr * sp * sy + cr * cy, -sr * cp };
   
    m.up = { -(cr * sp * cy + sr * sy),  cy * sr - cr * sp * sy,  cr * cp };

    return m;
}

PhysSnapshot ObsBuilder::InvertPhys(const PhysSnapshot& phys, bool shouldInvert) {
    if (!shouldInvert) return phys;

    PhysSnapshot inv = phys;
   
    inv.pos    = { -phys.pos.X,    -phys.pos.Y,     phys.pos.Z };
    inv.vel    = { -phys.vel.X,    -phys.vel.Y,     phys.vel.Z };
    inv.angVel = { -phys.angVel.X, -phys.angVel.Y,  phys.angVel.Z };

   
    inv.rotMat.forward = { -phys.rotMat.forward.X, -phys.rotMat.forward.Y, phys.rotMat.forward.Z };
    inv.rotMat.right   = { -phys.rotMat.right.X,   -phys.rotMat.right.Y,   phys.rotMat.right.Z };
    inv.rotMat.up      = { -phys.rotMat.up.X,      -phys.rotMat.up.Y,      phys.rotMat.up.Z };

    return inv;
}

void ObsBuilder::AddPlayerToObs(std::vector<float>& obs, const PlayerSnapshot& player,
                                     bool inv, const PhysSnapshot& ball) {
    PhysSnapshot phys = InvertPhys(player.phys, inv);

    PushVec(obs, phys.pos, POS_COEF);
    PushVecRaw(obs, phys.rotMat.forward);
    PushVecRaw(obs, phys.rotMat.up);
    PushVec(obs, phys.vel, VEL_COEF);
    PushVec(obs, phys.angVel, ANG_VEL_COEF);

   
    FVector localAngVel = phys.rotMat.Dot(phys.angVel);
    PushVec(obs, localAngVel, ANG_VEL_COEF);

   
    FVector relBallPos = { ball.pos.X - phys.pos.X, ball.pos.Y - phys.pos.Y, ball.pos.Z - phys.pos.Z };
    FVector localBallPos = phys.rotMat.Dot(relBallPos);
    PushVec(obs, localBallPos, POS_COEF);

    FVector relBallVel = { ball.vel.X - phys.vel.X, ball.vel.Y - phys.vel.Y, ball.vel.Z - phys.vel.Z };
    FVector localBallVel = phys.rotMat.Dot(relBallVel);
    PushVec(obs, localBallVel, VEL_COEF);

   
    obs.push_back(player.boost / 100.f);
    obs.push_back(player.isOnGround ? 1.f : 0.f);
    obs.push_back(player.hasFlipOrJump ? 1.f : 0.f);
    obs.push_back(player.isDemoed ? 1.f : 0.f);
    obs.push_back(player.hasJumped ? 1.f : 0.f);
}

std::vector<float> ObsBuilder::BuildObs(
    const PlayerSnapshot& localPlayer,
    const PhysSnapshot& ball,
    const std::vector<PlayerSnapshot>& allPlayers,
    const std::array<BoostPadState, 34>& pads)
{
    std::vector<float> obs;
    obs.reserve(cfg_.GetExpectedObsCount());

    bool inv = (localPlayer.teamIndex == 1); 

   
    PhysSnapshot ballInv = InvertPhys(ball, inv);
    PushVec(obs, ballInv.pos, POS_COEF);
    PushVec(obs, ballInv.vel, VEL_COEF);
    PushVec(obs, ballInv.angVel, ANG_VEL_COEF);

   
    for (int i = 0; i < 8; i++) {
        obs.push_back(localPlayer.prevAction[i]);
    }

   
    for (int i = 0; i < 34; i++) {
        int idx = inv ? INV_PAD_ORDER[i] : i;
        if (pads[idx].available) {
            obs.push_back(1.f);
        } else {
            obs.push_back(1.f / (1.f + pads[idx].timer));
        }
    }

   
    AddPlayerToObs(obs, localPlayer, inv, ballInv);

   
    std::vector<const PlayerSnapshot*> teammates, opponents;
    for (const auto& p : allPlayers) {
        if (p.carId == localPlayer.carId) continue;
        if (p.teamIndex == localPlayer.teamIndex) {
            teammates.push_back(&p);
        } else {
            opponents.push_back(&p);
        }
    }

   
    int expectedTeammates = cfg_.teamSize - 1;
    for (int i = 0; i < expectedTeammates; i++) {
        if (i < (int)teammates.size()) {
            AddPlayerToObs(obs, *teammates[i], inv, ballInv);
        } else {
           
            for (int j = 0; j < 29; j++) obs.push_back(0.f);
        }
    }

   
    int expectedOpponents = cfg_.teamSize;
    if (opponents.empty()) {
       
        for (int i = 0; i < expectedOpponents; i++) {
            for (int j = 0; j < 29; j++) obs.push_back(0.f);
        }
    } else if ((int)opponents.size() <= expectedOpponents) {
       
        for (int i = 0; i < expectedOpponents; i++) {
            if (i < (int)opponents.size()) {
                AddPlayerToObs(obs, *opponents[i], inv, ballInv);
            } else {
                for (int j = 0; j < 29; j++) obs.push_back(0.f);
            }
        }
    } else {
       
        for (int i = 0; i < expectedOpponents; i++) {
            AddPlayerToObs(obs, *opponents[i], inv, ballInv);
        }
    }

    return obs;
}

} 
