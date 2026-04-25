#pragma once
#include "../Modules/Module.hpp"
#include "../Components/Includes.hpp"
#include "Config.hpp"
#include "ObsBuilder.hpp"
#include "Humanizer.hpp"
#include "ViGemController.hpp"
#include <memory>
#include <array>
#include <chrono>
#include <atomic>
#include <mutex>

namespace RLInference { class Bot; struct ActionOutput; }

namespace GoySDK {

/// Soccer-field boost pad world positions (shared with ObsBuilder / overlay).
extern const FVector BOOST_LOCATIONS[34];

struct PlayerBotSlot {
    int modelIdx = -1; 
    std::unique_ptr<RLInference::Bot> bot;
    std::unique_ptr<ObsBuilder> obsBuilder;
    Config config;
    Humanizer humanizer;
    std::array<float, 8> lastAction{};
    int tickCounter = 0;

    /// Backend used when `bot` was last built (for reload when only inference device changes).
    InferenceBackend inferenceWhenBotLoaded = InferenceBackend::CPU;

    PlayerSnapshot localSnapshot{};
    APlayerController_TA* assignedPC = nullptr; 

    bool IsBot() const { return modelIdx >= 0 && bot != nullptr; }
};

class BotModule : public Module {
public:
    BotModule();
    ~BotModule() override;

    void OnCreate();
    void OnDestroy();

    static void Initialize();
    static void Hook();

   
    static void OnGameEventStart(PreEvent& event);
    static void OnGameEventDestroyed(PreEvent& event);
    static void PlayerTickCalled(const PostEvent& event);

   
    static void ToggleBot();

   
    static bool SwitchModel(int profileIdx);
    static bool AssignModel(int slotIdx, int modelIdx); 
    static int GetCurrentModelIdx() { return playerSlots_[0].modelIdx; }

    static bool IsActive() { return botActive_.load(); }

   
    static std::unique_lock<std::mutex> LockGuiState() {
        return std::unique_lock<std::mutex>(guiMutex_);
    }
    static Config& GetConfig() { return playerSlots_[0].config; }
    static PlayerBotSlot& GetSlot(int idx) { return playerSlots_[idx]; }
    static int GetLocalPlayerCount() { return localPlayerCount_; }
    static bool IsInGame() { return gameEvent_ != nullptr; }
    static bool IsModelLoading() { return modelLoading_; }

    static InputMethod GetInputMethod() { return inputMethod_; }
    static void SetInputMethod(InputMethod m);

    /// True after ViGEm client connected to the bus (driver installed). If false, ViGEm input is unavailable; Internal still works.
    static bool IsViGEmInputAvailable();

    /// True when LibTorch was built with CUDA and a usable NVIDIA stack is present.
    static bool IsCudaInferenceAvailable();

    static void SetInferenceBackend(InferenceBackend backend);

    static void AddSplitscreen();
    static void RemoveSplitscreen();
    static void TickJoinCountdowns();

   
    static constexpr int kMaxSlots = 4;

private:
   
    static void RunSlotInferenceTick(int slotIdx, APlayerController_TA* pc);

    static bool IsGameEventValid();

   
    static void ReadGameState(APlayerController_TA* anyPC);

    static void SyncLocalPlayers();

    static void ReadBoostPads();

   
    static bool LoadBotForSlot(int slotIdx, int modelIdx);

   
    static std::array<PlayerBotSlot, 4> playerSlots_;
    static int localPlayerCount_;

   
    static AGameEvent_Soccar_TA*         gameEvent_;
    static std::atomic<bool>             botActive_;

   
    static std::mutex                    guiMutex_;

    static PhysSnapshot                  ballSnapshot_;
    static std::vector<PlayerSnapshot>   allPlayers_;
    static std::array<BoostPadState, 34> padStates_;

   
    static std::array<std::chrono::steady_clock::time_point, 34> padPickupTimes_;
    static std::array<bool, 34> padWasAvailable_;

   
    static bool isKickoff_;
    static float kickoffTimer_;

   
    static int lastFrameTickCount_;

   
    static bool boostDiagDone_;

   
    static ViGemController vigemCtrl_;
    static InputMethod     inputMethod_;
    static std::array<int, 4> joinPressCountdown_;

   
    static void ApplySlotInput(int slotIdx, APlayerController_TA* pc,
                               float throttle, float steer, float pitch, float yaw, float roll,
                               bool jump, bool boost, bool handbrake);

   
    static bool autoSkipReplay_;
    static bool autoForfeit_;
    static int  autoForfeitScoreDiff_;
    static int  autoForfeitTimeSec_;
    static bool autoRequeue_;      
    static bool autoChat_;         
    static int  skipReplayCooldown_;
    static bool forfeitVotedThisMatch_;
    static bool modelLoading_;

public:
    static bool& AutoSkipReplay()       { return autoSkipReplay_; }
    static bool& AutoForfeit()          { return autoForfeit_; }
    static int&  AutoForfeitScoreDiff() { return autoForfeitScoreDiff_; }
    static int&  AutoForfeitTimeSec()   { return autoForfeitTimeSec_; }
    static bool& AutoRequeue()          { return autoRequeue_; }
    static bool& AutoChat()             { return autoChat_; }
    static const std::array<BoostPadState, 34>& GetPadStates() { return padStates_; }
};

} 

extern GoySDK::BotModule BotMod;
