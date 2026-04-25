#pragma once

#include "ObsBuilder.hpp"
#include <ActionOutput.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace GoySDK {

struct DiscreteAction {
  float throttle = 0.0f;
  float steer = 0.0f;
  float pitch = 0.0f;
  float yaw = 0.0f;
  float roll = 0.0f;
  bool jump = false;
  bool boost = false;
  bool handbrake = false;
};

struct DefaultDiscreteActionTables {
  std::vector<DiscreteAction> actions;
  std::vector<uint8_t> groundMask;
  std::vector<uint8_t> airMask;
  std::vector<uint8_t> jumpMask;
  std::vector<uint8_t> boostMask;
};

inline bool IsTurtled(const PlayerSnapshot &player) {
  return player.rawHasWorldContact && player.rawWorldContactNormal.Z > 0.9f;
}

inline const DefaultDiscreteActionTables &GetDefaultDiscreteActionTables() {
  static const DefaultDiscreteActionTables tables = [] {
    DefaultDiscreteActionTables built;
    constexpr float kBinary[] = {0.0f, 1.0f};
    constexpr float kTernary[] = {-1.0f, 0.0f, 1.0f};

    for (float throttle : kTernary) {
      for (float steer : kTernary) {
        for (float boost : kBinary) {
          for (float handbrake : kBinary) {
            if (boost == 1.0f && throttle != 1.0f) {
              continue;
            }
            built.actions.push_back({throttle, steer, 0.0f, steer, 0.0f, false,
                                     boost == 1.0f, handbrake == 1.0f});
          }
        }
      }
    }

    const int numGroundActions = static_cast<int>(built.actions.size());

    for (float pitch : kTernary) {
      for (float yaw : kTernary) {
        for (float roll : kTernary) {
          for (float jump : kBinary) {
            for (float boost : kBinary) {
              if (jump == 1.0f && yaw != 0.0f) {
                continue;
              }
              if (pitch == roll && roll == jump && jump == 0.0f) {
                continue;
              }

              const bool jumpBool = jump == 1.0f;
              const bool boostBool = boost == 1.0f;
              const bool handbrakeBool =
                  jumpBool && (pitch != 0.0f || yaw != 0.0f || roll != 0.0f);
              built.actions.push_back({boost, yaw, pitch, yaw, roll, jumpBool,
                                       boostBool, handbrakeBool});
            }
          }
        }
      }
    }

    built.groundMask.resize(built.actions.size(), 0);
    built.airMask.resize(built.actions.size(), 0);
    built.jumpMask.resize(built.actions.size(), 0);
    built.boostMask.resize(built.actions.size(), 0);

    for (int i = 0; i < static_cast<int>(built.actions.size()); ++i) {
      const DiscreteAction &action = built.actions[i];

      if (action.jump) {
        built.jumpMask[i] = 1;
      }
      if (action.boost) {
        built.boostMask[i] = 1;
      }
      if (i < numGroundActions) {
        built.groundMask[i] = 1;
      }
      if (i >= numGroundActions && !action.jump) {
        built.airMask[i] = 1;
      }
      if (i < numGroundActions) {
        const bool yawMatchesHandbrake =
            ((action.yaw != 0.0f) == action.handbrake);
        if (action.throttle == (action.boost ? 1.0f : 0.0f) &&
            yawMatchesHandbrake) {
          built.airMask[i] = 1;
        }
      }
    }

    return built;
  }();

  return tables;
}

inline const std::vector<DiscreteAction> &GetDefaultDiscreteActions() {
  return GetDefaultDiscreteActionTables().actions;
}

// Hot-path-friendly overload: writes into a caller-owned buffer so the per-
// inference-tick call site doesn't allocate a fresh std::vector each time.
inline void BuildDefaultActionMask(const PlayerSnapshot &player,
                                   std::vector<uint8_t> &out) {
  const auto &tables = GetDefaultDiscreteActionTables();
  out.assign(tables.actions.size(), 0);

  auto applyMask = [&](const std::vector<uint8_t> &source, bool add) {
    for (size_t i = 0; i < source.size(); ++i) {
      if (add) {
        out[i] = static_cast<uint8_t>(out[i] | source[i]);
      } else {
        out[i] =
            static_cast<uint8_t>(out[i] & static_cast<uint8_t>(!source[i]));
      }
    }
  };

  if (player.isOnGround) {
    applyMask(tables.groundMask, true);
  } else {
    applyMask(tables.airMask, true);
  }

  if (player.boost <= 0.0f) {
    applyMask(tables.boostMask, false);
  }

  if (player.hasFlipOrJump || IsTurtled(player)) {
    applyMask(tables.jumpMask, true);
  }
}

inline std::vector<uint8_t>
BuildDefaultActionMask(const PlayerSnapshot &player) {
  std::vector<uint8_t> mask;
  BuildDefaultActionMask(player, mask);
  return mask;
}

inline int SelectMaskedDiscreteActionIndex(const std::vector<float> &logits,
                                           const PlayerSnapshot &player) {
  const auto &actions = GetDefaultDiscreteActions();
  if (logits.size() != actions.size()) {
    return -1;
  }

  const std::vector<uint8_t> mask = BuildDefaultActionMask(player);
  int bestIndex = -1;
  float bestValue = 0.0f;
  for (int i = 0; i < static_cast<int>(logits.size()); ++i) {
    if (!mask[i]) {
      continue;
    }
    if (bestIndex < 0 || logits[i] > bestValue) {
      bestIndex = i;
      bestValue = logits[i];
    }
  }
  return bestIndex;
}

inline RLInference::ActionOutput ToActionOutput(const DiscreteAction &action) {
  RLInference::ActionOutput output;
  output.throttle = action.throttle;
  output.steer = action.steer;
  output.pitch = action.pitch;
  output.yaw = action.yaw;
  output.roll = action.roll;
  output.jump = action.jump;
  output.boost = action.boost;
  output.handbrake = action.handbrake;
  return output;
}

inline RLInference::ActionOutput ToActionOutput(int actionIndex) {
  const auto &actions = GetDefaultDiscreteActions();
  if (actionIndex < 0 || actionIndex >= static_cast<int>(actions.size())) {
    return {};
  }
  return ToActionOutput(actions[static_cast<size_t>(actionIndex)]);
}

}
