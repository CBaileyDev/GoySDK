#pragma once
#include "Modules/Module.hpp"
#include "Components/Includes.hpp"
#include <string>
#include <vector>
#include <mutex>

struct CarBoostData {
    FVector screenPosition;
    float boostAmount;
    bool isLocalCar;
    bool isEnemy;
};

/// One sample along the predicted ball path (world + projected screen; Z==0 means on-screen).
struct BallPredictionSample {
    FVector worldPosition;
    FVector screenPosition;
    FVector velocity;
    float timeSeconds;
    bool isBounce;
    bool isEndpoint;
};

/// Respawn countdown only (available pads do not produce a badge).
struct BoostTimerBadge {
    FVector screenPosition;
    float timeRemaining;
    std::string label;
};

class OverlayRenderer : public Module
{
public:
    OverlayRenderer();
    ~OverlayRenderer() override;

    // P3/10: removed empty OnCreate/OnDestroy declarations + their
    // constructor/destructor calls (Module has no virtual hooks for them).
    void OnRender();

    static void Hook();

    static void OnGameEventStart(PreEvent& event);
    static void OnGameEventDestroyed(PreEvent& event);
    static void PlayerTickCalled(const PostEvent& event);

    static void Initialize();

    /// P1/07: thread-safe query for "is the overlay tracking a live game event?"
    /// Acquires dataMutex internally. Use this instead of reading a public flag.
    static bool IsInGameActive();

    // Overlay toggles
    static bool showMyBoost;
    static bool showEnemyBoost;
    static bool drawBallCenter;
    static bool drawHitbox;
    static bool drawBallPrediction;
    static bool drawBoostTimers;

    // Ball prediction config
    static float predLineThickness;  // 1.0 - 5.0
    static float predTimeSeconds;    // 0.5 - 5.0
    static int   predColorPreset;    // 0=Cyan, 1=Green, 2=Yellow, 3=Red, 4=White

    static std::mutex dataMutex;

    static std::vector<CarBoostData> carBoostData;
    static std::vector<FVector> ballScreenPositions;
    static std::vector<BallPredictionSample> ballPredictionSamples;
    static std::vector<BoostTimerBadge> boostTimerBadges;
    static APRI_TA* localPlayerPRI;

private:
    // P1/07: previously public statics read without locking. Now only touched
    // while holding dataMutex. Atomic alone wouldn't help since CurrentGameEvent
    // points to an Unreal-owned object whose lifetime we don't control.
    static bool isInGame_;
    static AGameEvent_Soccar_TA* currentGameEvent_;
};

extern class OverlayRenderer OverlayMod;
