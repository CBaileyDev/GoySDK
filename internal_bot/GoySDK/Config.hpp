#pragma once
#include <array>
#include <string>
#include <vector>

namespace GoySDK {

enum class InputMethod { Internal, ViGem };

/// Where neural policy inference runs. Cuda0 requires NVIDIA GPU + driver and a CUDA-enabled LibTorch build.
enum class InferenceBackend : std::uint8_t { CPU = 0, Cuda0 = 1 };


struct ModelProfile {
    const char* name;
    const char* description;
    const char* supportedModes;  
    const char* sharedHeadPath;
    const char* policyPath;
    int sharedHeadResId;
    int policyResId;
    int sharedHeadSizes[5];      
    int policySizes[5];          
    int sharedHeadLayerCount;
    int policyLayerCount;
    bool useLeakyRelu;
    int tickSkip;
    int teamSize;
    float recommendedHumanize;

    std::vector<int> GetSharedHeadSizes() const {
        return std::vector<int>(sharedHeadSizes, sharedHeadSizes + sharedHeadLayerCount);
    }

    std::vector<int> GetPolicySizes() const {
        return std::vector<int>(policySizes, policySizes + policyLayerCount);
    }
};


constexpr std::array<ModelProfile, 2> kModelProfiles = {{
    {
        "ABUSE",
        "8B steps | 3v3 | Aggressive",
        "3v3",
        "models/ABUSE/SHARED_HEAD.lt",
        "models/ABUSE/POLICY.lt",
        101,   // IDR_ABUSE_HEAD
        102,   // IDR_ABUSE_POLICY
        {1024, 1024, 1024, 1024, 512},
        {1024, 1024, 1024, 1024, 512},
        5,
        5,
        false,
        8,
        3,
        0.0f
    },
    {
        "Frost",
        "305M steps | ~GC1 | Defensive",
        "1v1",
        "models/Frost/SHARED_HEAD.lt",
        "models/Frost/POLICY.lt",
        103,   // IDR_FROST_HEAD
        104,   // IDR_FROST_POLICY
        {1024, 1024, 1024, 1024, 0},
        {1024, 1024, 1024, 1024, 0},
        4,
        4,
        false,
        8,
        1,
        0.0f
    }
}};

struct Config {
   
    int activeProfileIdx = 0;

   
    std::string sharedHeadPath = kModelProfiles[0].sharedHeadPath;
    std::string policyPath     = kModelProfiles[0].policyPath;
    int sharedHeadResId        = kModelProfiles[0].sharedHeadResId;
    int policyResId            = kModelProfiles[0].policyResId;

   
    std::vector<int> sharedHeadSizes;
    std::vector<int> policySizes;
    bool useLeakyRelu = kModelProfiles[0].useLeakyRelu;

   
    int teamSize = 1;

   
    int tickSkip = 8;
    int kickoffTickSkip = 4;

   
    float smoothFactor = 0.30f;
    // P2/01: deadzone defaults to 0.0f because the input is the policy's
    // discrete output, not analog physical-controller noise. The previous
    // 0.02f silently clipped small intentional EMA-ramp values.
    float deadzone = 0.00f;
    float jitterAmount = 0.005f;
    bool humanize = false;

    InferenceBackend inferenceBackend = InferenceBackend::CPU;

   
    int GetExpectedObsCount() const {
        return 9 + 8 + 34 + 29 * (teamSize * 2);
    }

   
    void LoadProfile(int idx) {
        if (idx < 0 || idx >= static_cast<int>(kModelProfiles.size())) return;
        activeProfileIdx = idx;
        const auto& p = kModelProfiles[idx];
        sharedHeadPath = p.sharedHeadPath;
        policyPath = p.policyPath;
        sharedHeadResId = p.sharedHeadResId;
        policyResId = p.policyResId;
        sharedHeadSizes = p.GetSharedHeadSizes();
        policySizes = p.GetPolicySizes();
        useLeakyRelu = p.useLeakyRelu;
        tickSkip = p.tickSkip;
        teamSize = p.teamSize;
    }

   
    const char* GetProfileName() const {
        if (activeProfileIdx >= 0 && activeProfileIdx < static_cast<int>(kModelProfiles.size()))
            return kModelProfiles[activeProfileIdx].name;
        return "Unknown";
    }
};

} 
