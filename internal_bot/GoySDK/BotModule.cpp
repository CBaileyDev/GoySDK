#include "BotModule.hpp"
#include "UEHookStrings.hpp"
#include "ActionMask.hpp"
#include "GameState.hpp"
#include <RLInference.hpp>
#include <torch/torch.h>

#include <filesystem>
#include <cmath>
#include <cctype>
#include <sstream>

namespace GoySDK {

extern const FVector BOOST_LOCATIONS[34] = {
    {    0.f, -4240.f,   73.f}, 
    {-1792.f, -4184.f,   73.f}, 
    { 1792.f, -4184.f,   73.f}, 
    {-3072.f, -4096.f,   73.f}, 
    { 3072.f, -4096.f,   73.f}, 
    { -940.f, -3308.f,   73.f}, 
    {  940.f, -3308.f,   73.f}, 
    {    0.f, -2816.f,   73.f}, 
    {-3584.f, -2484.f,   73.f}, 
    { 3584.f, -2484.f,   73.f}, 
    {-1788.f, -2300.f,   73.f}, 
    { 1788.f, -2300.f,   73.f}, 
    {-2048.f, -1036.f,   73.f}, 
    {    0.f, -1024.f,   73.f}, 
    { 2048.f, -1036.f,   73.f}, 
    {-3584.f,     0.f,   73.f}, 
    {-1024.f,     0.f,   73.f}, 
    { 1024.f,     0.f,   73.f}, 
    { 3584.f,     0.f,   73.f}, 
    {-2048.f,  1036.f,   73.f}, 
    {    0.f,  1024.f,   73.f}, 
    { 2048.f,  1036.f,   73.f}, 
    {-1788.f,  2300.f,   73.f}, 
    { 1788.f,  2300.f,   73.f}, 
    {-3584.f,  2484.f,   73.f}, 
    { 3584.f,  2484.f,   73.f}, 
    {    0.f,  2816.f,   73.f}, 
    { -940.f,  3308.f,   73.f}, 
    {  940.f,  3308.f,   73.f}, 
    {-3072.f,  4096.f,   73.f}, 
    { 3072.f,  4096.f,   73.f}, 
    {-1792.f,  4184.f,   73.f}, 
    { 1792.f,  4184.f,   73.f}, 
    {    0.f,  4240.f,   73.f}, 
};

// Matches public arena sim big-pad order interleaved with small pads in our boost list.
static constexpr bool IsBigPad(int idx) {
    return idx == 3 || idx == 4 || idx == 15 || idx == 18 || idx == 29 || idx == 30;
}

std::array<PlayerBotSlot, 4>  BotModule::playerSlots_;
int                           BotModule::localPlayerCount_ = 0;
AGameEvent_Soccar_TA*         BotModule::gameEvent_ = nullptr;
std::atomic<bool>             BotModule::botActive_{false};
std::mutex                    BotModule::guiMutex_;
PhysSnapshot                  BotModule::ballSnapshot_{};
std::vector<PlayerSnapshot>   BotModule::allPlayers_;
std::array<BoostPadState, 34> BotModule::padStates_;
std::array<std::chrono::steady_clock::time_point, 34> BotModule::padPickupTimes_;
std::array<bool, 34>          BotModule::padWasAvailable_;
bool                          BotModule::isKickoff_ = false;
float                         BotModule::kickoffTimer_ = 0.0f;
int                           BotModule::lastFrameTickCount_ = -1;
bool                          BotModule::boostDiagDone_ = false;
ViGemController               BotModule::vigemCtrl_;
InputMethod                   BotModule::inputMethod_ = InputMethod::Internal;
std::array<int, 4>            BotModule::joinPressCountdown_ = {};

bool                          BotModule::autoSkipReplay_ = false;
bool                          BotModule::modelLoading_ = false;
bool                          BotModule::autoForfeit_ = false;
int                           BotModule::autoForfeitScoreDiff_ = 5;
int                           BotModule::autoForfeitTimeSec_ = 30;
bool                          BotModule::autoRequeue_ = false;
bool                          BotModule::autoChat_ = false;
int                           BotModule::skipReplayCooldown_ = 0;
bool                          BotModule::forfeitVotedThisMatch_ = false;



static std::filesystem::path GetBasePath() {
    char dllPath[MAX_PATH];
    HMODULE hm = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCSTR>(&BotModule::Initialize), &hm);
    GetModuleFileNameA(hm, dllPath, MAX_PATH);
    return std::filesystem::path(dllPath).parent_path();
}



void BotModule::ApplySlotInput(int slotIdx, APlayerController_TA* pc,
                                   float throttle, float steer, float pitch, float yaw, float roll,
                                   bool jump, bool boost, bool handbrake) {
    if (inputMethod_ == InputMethod::ViGem) {
       
        if (!vigemCtrl_.IsPadConnected(slotIdx)) {
            if (!vigemCtrl_.IsInitialized()) vigemCtrl_.Initialize();
            if (vigemCtrl_.ConnectPad(slotIdx)) {
                Console.Write("[GoySDK] ViGEm pad " + std::to_string(slotIdx) + " lazy-connected in ApplySlotInput");
            } else {
                Console.Error("[GoySDK] ViGEm pad " + std::to_string(slotIdx) + " failed to connect");
                return;
            }
        }
        vigemCtrl_.SendInput(slotIdx, throttle, steer, pitch, yaw, roll, jump, boost, handbrake);
    } else {
        ACar_TA* car = pc->Car;
        if (car) {
            pc->bOverrideInput = 1;
            pc->OverrideInput.Throttle = throttle;
            pc->OverrideInput.Steer    = steer;
            pc->OverrideInput.Pitch    = pitch;
            pc->OverrideInput.Yaw      = yaw;
            pc->OverrideInput.Roll     = roll;
            pc->OverrideInput.bJump          = jump ? 1 : 0;
            pc->OverrideInput.bActivateBoost = boost ? 1 : 0;
            pc->OverrideInput.bHoldingBoost  = boost ? 1 : 0;
            pc->OverrideInput.bHandbrake     = handbrake ? 1 : 0;
        }
    }
}

bool BotModule::IsViGEmInputAvailable() {
    return vigemCtrl_.IsInitialized();
}

bool BotModule::IsCudaInferenceAvailable() {
    return torch::cuda::is_available();
}

void BotModule::SetInferenceBackend(InferenceBackend backend) {
    for (int i = 0; i < kMaxSlots; ++i) {
        playerSlots_[i].config.inferenceBackend = backend;
        if (playerSlots_[i].modelIdx >= 0) {
            LoadBotForSlot(i, playerSlots_[i].modelIdx);
        }
    }
}

void BotModule::SetInputMethod(InputMethod m) {
    if (m == inputMethod_) return;

    InputMethod old = inputMethod_;
    inputMethod_ = m;

   
    if (old == InputMethod::ViGem) {
        for (int i = 0; i < kMaxSlots; i++) {
            vigemCtrl_.SendNeutral(i);
            vigemCtrl_.DisconnectPad(i);
        }
    }

   
    if (m == InputMethod::ViGem) {
        if (!vigemCtrl_.IsInitialized()) {
            if (!vigemCtrl_.Initialize()) {
                Console.Error("[GoySDK] Failed to initialize ViGEm bus. Is ViGEmBus driver installed?");
                inputMethod_ = InputMethod::Internal;
                return;
            }
        }
       
        if (!vigemCtrl_.IsPadConnected(0)) {
            vigemCtrl_.ConnectPad(0);
            Console.Write("[GoySDK] ViGEm pad 0 connected for primary player.");
        }
    }

   
    if (old == InputMethod::Internal) {
        for (auto& slot : playerSlots_) {
            if (slot.assignedPC) {
                slot.assignedPC->bOverrideInput = 0;
                memset(&slot.assignedPC->OverrideInput, 0, sizeof(FVehicleInputs));
            }
        }
    }

    const char* name = (m == InputMethod::ViGem) ? "ViGEm (Virtual Controller)" : "Internal (SDK Override)";
    Console.Notify(std::string("[GoySDK] Input method: ") + name);
}


BotModule::BotModule()
    : Module("GoySDK", "Neural policy bot for private matches", States::STATES_All) {
    OnCreate();
}

BotModule::~BotModule() { OnDestroy(); }

void BotModule::OnCreate() {
    padWasAvailable_.fill(true);
    auto now = std::chrono::steady_clock::now();
    padPickupTimes_.fill(now);
    boostDiagDone_ = false;
    lastFrameTickCount_ = -1;
}

void BotModule::OnDestroy() {
    vigemCtrl_.Shutdown();
    for (auto& slot : playerSlots_) {
        slot.bot.reset();
        slot.obsBuilder.reset();
    }
}

void BotModule::Hook() {
    Events.HookEventPre(uehstr::fn_event_post_begin(), OnGameEventStart);
    Events.HookEventPre(uehstr::fn_event_active_begin(), OnGameEventStart);
    Events.HookEventPre(uehstr::fn_event_countdown_begin(), OnGameEventStart);
    Events.HookEventPre(uehstr::fn_event_destroyed(), OnGameEventDestroyed);
    Events.HookEventPost(uehstr::fn_player_controller_tick(), PlayerTickCalled);
}


EXTERN_C IMAGE_DOS_HEADER __ImageBase;

bool BotModule::LoadBotForSlot(int slotIdx, int modelIdx) {
    if (slotIdx < 0 || slotIdx >= kMaxSlots) return false;
    if (modelIdx < 0 || modelIdx >= (int)kModelProfiles.size()) return false;

    modelLoading_ = true;
    auto& slot = playerSlots_[slotIdx];
    slot.bot.reset();
    slot.obsBuilder.reset();

    slot.config.LoadProfile(modelIdx);
    slot.modelIdx = modelIdx;

   
    HINSTANCE hInst = (HINSTANCE)&__ImageBase;

   
    auto load_res = [&](int id, const void*& data, size_t& size) {
        HRSRC hRes = FindResourceA(hInst, MAKEINTRESOURCEA(id), (LPCSTR)10);
        if (!hRes) return false;
        HGLOBAL hMem = LoadResource(hInst, hRes);
        if (!hMem) return false;
        size = SizeofResource(hInst, hRes);
        data = LockResource(hMem);
        return data != nullptr;
    };

    const void* sharedData = nullptr; size_t sharedSize = 0;
    const void* policyData = nullptr; size_t policySize = 0;

    if (!load_res(slot.config.sharedHeadResId, sharedData, sharedSize) ||
        !load_res(slot.config.policyResId, policyData, policySize)) {
        Console.Error("[GoySDK] Slot " + std::to_string(slotIdx) +
                      ": Embedded model files not found for " + std::string(slot.config.GetProfileName()));
        slot.modelIdx = -1;
        modelLoading_ = false;
        return false;
    }

    try {
        RLInference::BotConfig botCfg(
            RLInference::BotType::GigaLearn,
            "",
            "",
            slot.config.sharedHeadSizes,
            slot.config.tickSkip,
            false, 
            slot.config.GetExpectedObsCount(),
            slot.config.policySizes,
            slot.config.useLeakyRelu
        );
        botCfg.primary_model_data = sharedData;
        botCfg.primary_model_size = sharedSize;
        botCfg.secondary_model_data = policyData;
        botCfg.secondary_model_size = policySize;

        if (slot.config.inferenceBackend == InferenceBackend::Cuda0 && !torch::cuda::is_available()) {
            Console.Error("[GoySDK] CUDA inference requested but torch::cuda::is_available() is false — using CPU.");
        }
        // RLInference engines currently load/evaluate on CPU regardless of backend selection.

        slot.bot = std::make_unique<RLInference::Bot>(botCfg, nullptr);

        if (!slot.bot->is_initialized()) {
            Console.Error("[GoySDK] Slot " + std::to_string(slotIdx) +
                          ": Bot created but model initialization FAILED for " +
                          std::string(slot.config.GetProfileName()) +
                          " (data=" + std::to_string((uintptr_t)botCfg.primary_model_data) +
                          " size=" + std::to_string(botCfg.primary_model_size) + ")");
            slot.bot.reset();
            slot.modelIdx = -1;
            modelLoading_ = false;
            return false;
        }

        slot.obsBuilder = std::make_unique<ObsBuilder>(slot.config);
        slot.humanizer = Humanizer(slot.config.smoothFactor, slot.config.deadzone, slot.config.jitterAmount);
        slot.lastAction.fill(0.f);
        slot.tickCounter = 0;

        Console.Notify("[GoySDK] Slot " + std::to_string(slotIdx) +
                       ": Loaded " + std::string(slot.config.GetProfileName()) +
                       " (" + std::string(kModelProfiles[modelIdx].supportedModes) +
                       ", obs=" + std::to_string(slot.config.GetExpectedObsCount()) + ")");
        slot.inferenceWhenBotLoaded = slot.config.inferenceBackend;
        modelLoading_ = false;
        return true;
    } catch (const std::exception& e) {
        Console.Error("[GoySDK] Slot " + std::to_string(slotIdx) +
                      ": Failed to load model: " + std::string(e.what()));
        slot.modelIdx = -1;
        modelLoading_ = false;
        return false;
    }
}


void BotModule::AddSplitscreen() {
    if (gameEvent_) {
        Console.Error("[GoySDK] Cannot add splitscreen during an active game");
        return;
    }

   
    auto* gvc = Instances.GetInstanceOf<UGameViewportClient_TA>();
    if (!gvc) {
        Console.Error("[GoySDK] Cannot find GameViewportClient_TA");
        return;
    }

   
    gvc->bSplitScreenDisabled = 0;

   
    int nextId = -1;
    for (int id = 1; id < kMaxSlots; id++) {
        ULocalPlayer* existing = gvc->eventFindPlayerByControllerId(id);
        if (!existing) {
            nextId = id;
            break;
        }
    }
    if (nextId < 0) {
        Console.Error("[GoySDK] Max splitscreen slots reached");
        return;
    }

    FString outError;
    ULocalPlayer* newPlayer = gvc->eventCreatePlayer(nextId, true, outError);
    if (newPlayer) {
        Console.Notify("[GoySDK] Splitscreen player added (controller " + std::to_string(nextId) + ")");
    } else {
        std::string errMsg = outError.ToString();
        Console.Error("[GoySDK] Failed to create splitscreen player: " + (errMsg.empty() ? "unknown error" : errMsg));
        return;
    }

   
    if (inputMethod_ == InputMethod::ViGem) {
        if (!vigemCtrl_.IsInitialized()) {
            if (!vigemCtrl_.Initialize()) {
                Console.Error("[GoySDK] Failed to initialize ViGEm bus");
                return;
            }
        }
        if (!vigemCtrl_.IsPadConnected(nextId)) {
            if (vigemCtrl_.ConnectPad(nextId)) {
                Console.Write("[GoySDK] ViGEm pad " + std::to_string(nextId) + " connected for splitscreen input.");
            }
        }
    }
}

void BotModule::RemoveSplitscreen() {
    if (gameEvent_) {
        Console.Error("[GoySDK] Cannot remove splitscreen during an active game");
        return;
    }

    auto* gvc = Instances.GetInstanceOf<UGameViewportClient_TA>();
    if (!gvc) {
        Console.Error("[GoySDK] Cannot find GameViewportClient_TA");
        return;
    }

   
    int lastId = -1;
    ULocalPlayer* lastPlayer = nullptr;
    for (int id = kMaxSlots - 1; id >= 1; id--) {
        ULocalPlayer* lp = gvc->eventFindPlayerByControllerId(id);
        if (lp) {
            lastId = id;
            lastPlayer = lp;
            break;
        }
    }

    if (!lastPlayer) {
        Console.Write("[GoySDK] No splitscreen players to remove");
        return;
    }

    bool removed = gvc->eventRemovePlayer(lastPlayer);
    if (removed) {
        Console.Notify("[GoySDK] Splitscreen player removed (controller " + std::to_string(lastId) + ")");
    } else {
        Console.Error("[GoySDK] Failed to remove splitscreen player");
    }

   
    if (inputMethod_ == InputMethod::ViGem && vigemCtrl_.IsPadConnected(lastId)) {
        vigemCtrl_.SendNeutral(lastId);
        vigemCtrl_.DisconnectPad(lastId);
        Console.Write("[GoySDK] Disconnected ViGEm pad " + std::to_string(lastId));
    }
}

void BotModule::TickJoinCountdowns() {
    for (int i = 0; i < kMaxSlots; i++) {
        if (joinPressCountdown_[i] < 0) {
           
            joinPressCountdown_[i]++;
            if (joinPressCountdown_[i] == 0) {
               
                joinPressCountdown_[i] = 60;
            }
        } else if (joinPressCountdown_[i] > 0) {
           
            vigemCtrl_.PressJoin(i);
            joinPressCountdown_[i]--;
            if (joinPressCountdown_[i] == 0) {
                vigemCtrl_.SendNeutral(i);
            }
        }
    }
}


void BotModule::Initialize() {
    Console.Write("[GoySDK] Initializing...");


    if (vigemCtrl_.Initialize()) {
        Console.Write("[GoySDK] ViGEm bus client ready.");
    } else {
        Console.Write("[GoySDK] ViGEm bus not available (ViGEmBus driver not installed?). Internal input only.");
    }

   
    if (!LoadBotForSlot(0, 0)) {
        Console.Error("[GoySDK] Failed to load default model.");
        return;
    }

    Hook();
    Console.Notify("[GoySDK] Initialized. Press HOME to toggle bot on/off.");
}

void BotModule::ToggleBot() {
    botActive_ = !botActive_.load();
    if (botActive_) {
        for (auto& slot : playerSlots_) {
            slot.lastAction.fill(0.f);
            slot.tickCounter = 0;
            slot.humanizer.Reset();
        }
        Console.Notify("[GoySDK] BOT ACTIVE");
    } else {
       
        for (int i = 0; i < kMaxSlots; i++) {
            auto& slot = playerSlots_[i];
            if (inputMethod_ == InputMethod::ViGem) {
                vigemCtrl_.SendNeutral(i);
            }
            if (slot.assignedPC) {
                slot.assignedPC->bOverrideInput = 0;
                memset(&slot.assignedPC->OverrideInput, 0, sizeof(FVehicleInputs));
            }
        }
        Console.Notify("[GoySDK] BOT DISABLED");
    }
}

bool BotModule::SwitchModel(int profileIdx) {
   
    return AssignModel(0, profileIdx);
}

bool BotModule::AssignModel(int slotIdx, int modelIdx) {
    if (slotIdx < 0 || slotIdx >= kMaxSlots) return false;

    auto& slot = playerSlots_[slotIdx];

    if (modelIdx == -1) {
       
        slot.bot.reset();
        slot.obsBuilder.reset();
        slot.modelIdx = -1;
        if (inputMethod_ == InputMethod::ViGem) {
            vigemCtrl_.SendNeutral(slotIdx);
        }
        if (slot.assignedPC) {
            slot.assignedPC->bOverrideInput = 0;
            memset(&slot.assignedPC->OverrideInput, 0, sizeof(FVehicleInputs));
        }
        Console.Notify("[GoySDK] Slot " + std::to_string(slotIdx) + ": Set to HUMAN control");
        return true;
    }

    if (modelIdx == slot.modelIdx && slot.bot && slot.inferenceWhenBotLoaded == slot.config.inferenceBackend)
        return true;

    bool wasActive = botActive_;
    return LoadBotForSlot(slotIdx, modelIdx);
}


bool BotModule::IsGameEventValid() {
    if (!gameEvent_) return false;
    __try {
        return gameEvent_->IsA(AGameEvent_Soccar_TA::StaticClass());
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        gameEvent_ = nullptr;
        return false;
    }
}

void BotModule::OnGameEventStart(PreEvent& event) {
    try {
        if (event.Caller() && event.Caller()->IsA(AGameEvent_Soccar_TA::StaticClass())) {
            gameEvent_ = static_cast<AGameEvent_Soccar_TA*>(event.Caller());
            Console.Write("[GoySDK] Game event captured.");
        }
    } catch (const std::exception& e) {
        Console.Error(std::string("[GoySDK] Exception in OnGameEventStart: ") + e.what());
    } catch (...) {
        Console.Error("[GoySDK] Unknown exception in OnGameEventStart");
    }
}

void BotModule::OnGameEventDestroyed(PreEvent& event) {
    (void)event;
    gameEvent_ = nullptr;
    botActive_.store(false);
    localPlayerCount_ = 0;
    lastFrameTickCount_ = -1;
    boostDiagDone_ = false;
    isKickoff_ = false;
    kickoffTimer_ = 0.0f;

   
    if (inputMethod_ == InputMethod::ViGem) {
        for (int i = 0; i < kMaxSlots; i++) {
            vigemCtrl_.SendNeutral(i);
        }
    }
    joinPressCountdown_.fill(0);
    forfeitVotedThisMatch_ = false;
    skipReplayCooldown_ = 0;

    for (auto& slot : playerSlots_) {
        slot.assignedPC = nullptr;
        slot.localSnapshot = {};
        slot.lastAction.fill(0.f);
        slot.tickCounter = 0;
    }
}


void BotModule::SyncLocalPlayers() {
    localPlayerCount_ = 0;

    if (!gameEvent_) {
        for (auto& slot : playerSlots_) {
            slot.assignedPC = nullptr;
        }
        return;
    }

    TArray<APlayerController_TA*> localPlayers = gameEvent_->LocalPlayers;
    localPlayerCount_ = std::min(static_cast<int>(localPlayers.size()), kMaxSlots);

    for (int i = 0; i < kMaxSlots; ++i) {
        APlayerController_TA* nextPC = (i < localPlayerCount_) ? localPlayers[i] : nullptr;
        if (playerSlots_[i].assignedPC != nextPC && nextPC == nullptr) {
            playerSlots_[i].localSnapshot = {};
            playerSlots_[i].lastAction.fill(0.f);
            playerSlots_[i].tickCounter = 0;
        }
        playerSlots_[i].assignedPC = nextPC;
    }
}

void BotModule::PlayerTickCalled(const PostEvent& event) {
    if (!IsGameEventValid()) return;
    if (!event.Caller() || !event.Caller()->IsA(APlayerController_TA::StaticClass())) return;

    auto* pc = static_cast<APlayerController_TA*>(event.Caller());
    if (!pc) return;

    try {
       
        static int frameCounter = 0;
        bool isFirstSlotThisFrame = false;
        if (playerSlots_[0].assignedPC == pc || lastFrameTickCount_ == -1) {
            frameCounter++;
            if (lastFrameTickCount_ != frameCounter) {
                lastFrameTickCount_ = frameCounter;
                isFirstSlotThisFrame = true;
                {
                    std::lock_guard<std::mutex> lock(guiMutex_);
                    SyncLocalPlayers();
                    ReadGameState(pc);
                    ReadBoostPads();
                }
            }
        }

       
        if (playerSlots_[0].assignedPC == pc) {
            if (skipReplayCooldown_ > 0) skipReplayCooldown_--;

            if (autoSkipReplay_ && skipReplayCooldown_ <= 0) {
                try {
                    if (gameEvent_->IsInReplayPlayback()) {
                        pc->bOverrideInput = 1;
                        pc->OverrideInput.bJump = 1;
                        skipReplayCooldown_ = 30;
                    }
                } catch (const std::exception& e) {
                    Console.Error(std::string("[GoySDK] Exception in auto-skip replay: ") + e.what());
                } catch (...) {
                    Console.Error("[GoySDK] Unknown exception in auto-skip replay");
                }
            }

           
            if (autoForfeit_ && !forfeitVotedThisMatch_ && gameEvent_->bCanVoteToForfeit) {
                try {
                    APRI_TA* localPRI = pc->PRI;
                    int timeRemaining = gameEvent_->GameStateTimeRemaining;
                    auto teams = gameEvent_->Teams;
                    if (teams.size() >= 2 && localPRI && localPRI->Team) {
                        int myTeamIdx = localPRI->Team->TeamIndex;
                        int myScore = teams[myTeamIdx]->Score;
                        int oppScore = teams[1 - myTeamIdx]->Score;
                        int diff = oppScore - myScore;
                        if (diff >= autoForfeitScoreDiff_ && timeRemaining <= autoForfeitTimeSec_) {
                            auto* myTeam = static_cast<ATeam_TA*>(localPRI->Team);
                            if (myTeam) {
                                myTeam->VoteToForfeit(localPRI);
                                forfeitVotedThisMatch_ = true;
                                Console.Notify("[GoySDK] Auto-forfeit vote cast (down " + std::to_string(diff) + ", " + std::to_string(timeRemaining) + "s left)");
                            }
                        }
                    }
                } catch (const std::exception& e) {
                    Console.Error(std::string("[GoySDK] Exception in auto-forfeit: ") + e.what());
                } catch (...) {
                    Console.Error("[GoySDK] Unknown exception in auto-forfeit");
                }
            }
        }

        int slotIdx = -1;
        for (int i = 0; i < kMaxSlots; i++) {
            if (playerSlots_[i].assignedPC == pc) {
                slotIdx = i;
                break;
            }
        }

        if (slotIdx < 0) return;
        if (!botActive_) return;
        if (!pc->Car) return;

        auto& slot = playerSlots_[slotIdx];
        if (!slot.IsBot()) return;

        RunSlotInferenceTick(slotIdx, pc);

    } catch (const std::exception& e) {
        Console.Error(std::string("[GoySDK] Exception in tick: ") + e.what());
    } catch (...) {
        Console.Error("[GoySDK] Unknown exception in tick");
    }
}


void BotModule::ReadGameState(APlayerController_TA* anyPC) {
    (void)anyPC;
    if (!IsGameEventValid()) return;
    allPlayers_.clear();

   
    TArray<ABall_TA*> balls = gameEvent_->GameBalls;
    if (balls.size() > 0 && balls[0]) {
        ABall_TA* ball = balls[0];
        ballSnapshot_.pos    = ball->Location;
        ballSnapshot_.vel    = ball->Velocity;
        ballSnapshot_.angVel = ball->AngularVelocity;
        ballSnapshot_.rotator = ball->Rotation;
        ballSnapshot_.rotMat = ObsBuilder::RotatorToMatrix(ball->Rotation);
    }

   
    TArray<ACar_TA*> cars = gameEvent_->Cars;
    for (int i = 0; i < static_cast<int>(cars.size()); i++) {
        ACar_TA* car = cars[i];
        if (!car) continue;

        PlayerSnapshot snap;
        snap.phys.pos    = car->Location;
        snap.phys.vel    = car->Velocity;
        snap.phys.angVel = car->AngularVelocity;
        snap.phys.rotator = car->Rotation;
        snap.phys.rotMat = ObsBuilder::RotatorToMatrix(car->Rotation);
        snap.traceCarKey = reinterpret_cast<uint64_t>(car);

       
        auto* boostComp = car->BoostComponent;
        snap.boost = boostComp ? (boostComp->CurrentBoostAmount * 100.f) : 0.f;

       
        if (!boostDiagDone_ && boostComp) {
            Console.Write("[GoySDK] BOOST DIAG: raw=" +
                std::to_string(boostComp->CurrentBoostAmount) +
                " stored=" + std::to_string(snap.boost));
            boostDiagDone_ = true;
        }

       
        bool rawOnGround   = car->bOnGround != 0;
        snap.hasJumped     = car->bJumped != 0;
        bool doubleJumped  = car->bDoubleJumped != 0;
        snap.isDemoed      = car->IsDemolished();
        snap.rawOnGround = rawOnGround;
        snap.rawJumped = snap.hasJumped;
        snap.rawDoubleJumped = doubleJumped;
        snap.rawIsDemoed = snap.isDemoed;
        snap.rawHasWorldContact = car->WorldContact.bHasContact != 0;
        snap.rawWorldContactNormal = car->WorldContact.Normal;

        snap.isOnGround = ComputeObservedOnGround(rawOnGround);

        snap.hasFlipOrJump = !doubleJumped;

       
        snap.teamIndex = 0;
        if (car->PRI && car->PRI->Team) {
            snap.teamIndex = car->PRI->Team->TeamIndex;
        }

        snap.carId = static_cast<uint32_t>(i);

       
        for (int s = 0; s < kMaxSlots; s++) {
            if (playerSlots_[s].assignedPC && playerSlots_[s].assignedPC->Car == car) {
                snap.prevAction = playerSlots_[s].lastAction;
                playerSlots_[s].localSnapshot = snap;
                break;
            }
        }

        allPlayers_.push_back(snap);
    }
}

void BotModule::ReadBoostPads() {
    auto boostPickups = Instances.GetAllInstancesOf<AVehiclePickup_Boost_TA>();
    auto now = std::chrono::steady_clock::now();

    for (int i = 0; i < 34; i++) padStates_[i].available = true;

    for (auto* pickup : boostPickups) {
        if (!pickup) continue;

        FVector loc = pickup->Location;

        int bestIdx = -1;
        float bestDist = 500.f;
        for (int i = 0; i < 34; i++) {
            float dx = loc.X - BOOST_LOCATIONS[i].X;
            float dy = loc.Y - BOOST_LOCATIONS[i].Y;
            float dist = sqrtf(dx * dx + dy * dy);
            if (dist < bestDist) {
                bestDist = dist;
                bestIdx = i;
            }
        }

        if (bestIdx < 0) continue;

        bool available = true;
        uintptr_t pickupAddr = reinterpret_cast<uintptr_t>(pickup);
        uint32_t pickupFlags = SafeRead<uint32_t>(pickupAddr + 0x02B8);
        available = !(pickupFlags & 0x00000002);

        padStates_[bestIdx].available = available;

        if (!available && padWasAvailable_[bestIdx]) {
            padPickupTimes_[bestIdx] = now;
        }

        if (!available) {
            float respawnTime = IsBigPad(bestIdx) ? 10.f : 4.f;
            float elapsed = std::chrono::duration<float>(now - padPickupTimes_[bestIdx]).count();
            padStates_[bestIdx].timer = std::max(0.f, respawnTime - elapsed);
        }

        padWasAvailable_[bestIdx] = available;
    }
}


void BotModule::RunSlotInferenceTick(int slotIdx, APlayerController_TA* pc) {
    auto& slot = playerSlots_[slotIdx];
    if (!slot.bot || !slot.bot->is_initialized()) return;

    slot.tickCounter++;

   
    const float ballXY = sqrtf(ballSnapshot_.pos.X * ballSnapshot_.pos.X +
                                ballSnapshot_.pos.Y * ballSnapshot_.pos.Y);
    const float ballSpeed = sqrtf(ballSnapshot_.vel.X * ballSnapshot_.vel.X +
                                   ballSnapshot_.vel.Y * ballSnapshot_.vel.Y +
                                   ballSnapshot_.vel.Z * ballSnapshot_.vel.Z);

    bool wasKickoff = isKickoff_;
    isKickoff_ = (ballXY < 100.0f) && (ballSpeed < 50.0f);

    if (isKickoff_ && !wasKickoff) {
        kickoffTimer_ = 0.0f;
    }
    if (isKickoff_) {
        kickoffTimer_ += 1.0f / 120.0f;
    }

    int effectiveTickSkip = (isKickoff_ && kickoffTimer_ < 2.5f)
                            ? slot.config.kickoffTickSkip
                            : slot.config.tickSkip;

    bool shouldInfer = (slot.tickCounter % effectiveTickSkip == 0);

    if (!shouldInfer) {
       
        ApplySlotInput(slotIdx, pc,
            slot.lastAction[0], slot.lastAction[1], slot.lastAction[2],
            slot.lastAction[3], slot.lastAction[4],
            slot.lastAction[5] > 0.5f, slot.lastAction[6] > 0.5f, slot.lastAction[7] > 0.5f);
        return;
    }

   
    bool diag = (slot.tickCounter <= 24);

    if (diag) {
        Console.Write("[GoySDK] S" + std::to_string(slotIdx) +
            " tick=" + std::to_string(slot.tickCounter) +
            " players=" + std::to_string(allPlayers_.size()) +
            " pos=(" + std::to_string((int)slot.localSnapshot.phys.pos.X) + "," +
            std::to_string((int)slot.localSnapshot.phys.pos.Y) + ")");
    }

   
    std::vector<float> obs = slot.obsBuilder->BuildObs(
        slot.localSnapshot, ballSnapshot_, allPlayers_, padStates_);

    if (diag) Console.Write("[GoySDK] S" + std::to_string(slotIdx) +
                            " obs=" + std::to_string(obs.size()) +
                            " expected=" + std::to_string(slot.config.GetExpectedObsCount()));

   
    if ((int)obs.size() != slot.config.GetExpectedObsCount()) {
        if (diag) Console.Error("[GoySDK] S" + std::to_string(slotIdx) +
                                " OBS MISMATCH: " + std::to_string(obs.size()) +
                                " vs " + std::to_string(slot.config.GetExpectedObsCount()));
        return;
    }

   
    slot.bot->obs().clear();
    for (float val : obs) {
        slot.bot->push_obs(val);
    }

   
    if (!slot.bot->forward()) {
        if (diag) Console.Write("[GoySDK] S" + std::to_string(slotIdx) + " forward() false");
        return;
    }

    RLInference::ActionOutput action = slot.bot->get_last_output();
    const RLInference::ActionOutput policyAction = action;
    const RLInference::InferenceDebugInfo policyDebug = slot.bot->get_last_debug();

    if (diag) {
        Console.Write("[GoySDK] S" + std::to_string(slotIdx) +
            " fwd OK: actIdx=" + std::to_string(policyDebug.action_index) +
            " logits=" + std::to_string(policyDebug.logits.size()) +
            " thr=" + std::to_string(action.throttle) +
            " str=" + std::to_string(action.steer) +
            " jmp=" + std::to_string(action.jump) +
            " bst=" + std::to_string(action.boost));
    }

    if (policyDebug.available) {
        const int maskedActionIndex = SelectMaskedDiscreteActionIndex(policyDebug.logits, slot.localSnapshot);
        if (maskedActionIndex >= 0 && maskedActionIndex != policyDebug.action_index) {
            action = ToActionOutput(maskedActionIndex);
        }
    }

   
    if (slot.localSnapshot.boost <= 0.01f) {
        action.boost = false;
    }

   
    if (!slot.localSnapshot.hasFlipOrJump && !slot.localSnapshot.isOnGround) {
        action.jump = false;
    }

   
    slot.lastAction = {
        action.throttle, action.steer, action.pitch, action.yaw, action.roll,
        action.jump ? 1.f : 0.f, action.boost ? 1.f : 0.f, action.handbrake ? 1.f : 0.f
    };

   
    float throttle = action.throttle;
    float steer    = action.steer;
    float pitch    = action.pitch;
    float yaw      = action.yaw;
    float roll     = action.roll;

    if (slot.config.humanize) {
        slot.humanizer.ProcessAnalog(throttle, steer, pitch, yaw, roll);
    }

    ApplySlotInput(slotIdx, pc, throttle, steer, pitch, yaw, roll,
                   action.jump, action.boost, action.handbrake);
}

} 

GoySDK::BotModule BotMod;
