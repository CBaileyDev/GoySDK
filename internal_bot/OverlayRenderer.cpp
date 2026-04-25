#include "OverlayRenderer.hpp"
#include "Offsets.hpp"
#include "Modules/Mods/Drawing.hpp"
#include "GoySDK/BotModule.hpp"
#include "GoySDK/UEHookStrings.hpp"
#include "ImGui/imgui.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>
#ifndef IM_PI
#define IM_PI 3.14159265358979323846f
#endif

namespace {

constexpr float kFallbackPredictionStep = 1.0f / 30.0f;
constexpr float kMinBounceSpeed = 250.0f;
constexpr float kBounceDirectionDotThreshold = 0.94f;
constexpr float kBoostBadgeLift = 36.0f;
constexpr float kBounceMarkerRadius = 4.0f;
constexpr float kEndpointMarkerRadius = 5.5f;

float VectorLength(const FVector& value)
{
    return std::sqrt((value.X * value.X) + (value.Y * value.Y) + (value.Z * value.Z));
}

float DotProduct(const FVector& a, const FVector& b)
{
    return (a.X * b.X) + (a.Y * b.Y) + (a.Z * b.Z);
}

bool IsScreenPointVisible(const FVector& point)
{
    return point.Z == 0.0f;
}

bool IsBounceTransition(const FVector& previousVelocity, const FVector& currentVelocity)
{
    const float previousSpeed = VectorLength(previousVelocity);
    const float currentSpeed = VectorLength(currentVelocity);
    if (previousSpeed < kMinBounceSpeed || currentSpeed < kMinBounceSpeed) {
        return false;
    }

    const float denom = previousSpeed * currentSpeed;
    if (denom <= 0.0f) {
        return false;
    }

    const float dot = DotProduct(previousVelocity, currentVelocity) / denom;
    return dot < kBounceDirectionDotThreshold;
}

std::string FormatBoostTimerLabel(float timerSeconds)
{
    char buffer[16];
    const float clamped = std::max(timerSeconds, 0.0f);
    if (clamped >= 5.0f) {
        std::snprintf(buffer, sizeof(buffer), "%.0fs", std::ceil(clamped));
    } else {
        std::snprintf(buffer, sizeof(buffer), "%.1fs", clamped);
    }
    return std::string(buffer);
}

void AppendPredictionSample(std::vector<BallPredictionSample>& samples,
                            APlayerController_TA* localPlayerController,
                            const FVector& worldPosition,
                            const FVector& velocity,
                            float timeSeconds)
{
    BallPredictionSample sample{};
    sample.worldPosition = worldPosition;
    sample.screenPosition = Drawing::CalculateScreenCoordinate(worldPosition, localPlayerController);
    sample.velocity = velocity;
    sample.timeSeconds = timeSeconds;
    sample.isBounce = !samples.empty() && IsBounceTransition(samples.back().velocity, velocity);
    sample.isEndpoint = false;
    samples.push_back(sample);
}

void MarkPredictionEndpoint(std::vector<BallPredictionSample>& samples)
{
    for (auto& sample : samples) {
        sample.isEndpoint = false;
    }

    for (auto it = samples.rbegin(); it != samples.rend(); ++it) {
        if (IsScreenPointVisible(it->screenPosition)) {
            it->isEndpoint = true;
            break;
        }
    }
}

std::vector<BallPredictionSample> BuildPredictionSamples(ABall_TA* ball,
                                                         APlayerController_TA* localPlayerController,
                                                         float predictionSeconds)
{
    std::vector<BallPredictionSample> samples;
    if (!ball || !localPlayerController || predictionSeconds <= 0.0f) {
        return samples;
    }

    samples.reserve(64);
    AppendPredictionSample(samples, localPlayerController, ball->Location, ball->Velocity, 0.0f);

    bool usedTrajectoryPoints = false;
    if (ball->CanEverShowTrajectory()) {
        ball->UpdateTrajectoryEnabled();
        ball->UpdateTrajectoryPredictionPoints(true);

        const float trajectoryStep = ball->PredictionTimestep;
        const auto& predictedPositions = ball->PredictedPositions;
        if (trajectoryStep > 0.0f && predictedPositions.size() > 0) {
            for (size_t i = 0; i < predictedPositions.size(); ++i) {
                const float timeSeconds = trajectoryStep * static_cast<float>(i + 1);
                if (timeSeconds > predictionSeconds) {
                    break;
                }

                const auto& predicted = predictedPositions[i];
                AppendPredictionSample(samples, localPlayerController, predicted.Location, predicted.Velocity, timeSeconds);
            }
            usedTrajectoryPoints = samples.size() > 1;
        }
    }

    if (!usedTrajectoryPoints) {
        samples.clear();
        AppendPredictionSample(samples, localPlayerController, ball->Location, ball->Velocity, 0.0f);

        const float sampleStep = (ball->PredictionTimestep > 0.0f) ? ball->PredictionTimestep : kFallbackPredictionStep;
        for (float timeSeconds = sampleStep; timeSeconds <= predictionSeconds + 0.001f; timeSeconds += sampleStep) {
            FBallPredictionInfo prediction{};
            ball->PredictPosition(timeSeconds, prediction);
            AppendPredictionSample(samples, localPlayerController, prediction.Location, prediction.Velocity, timeSeconds);
        }
    }

    MarkPredictionEndpoint(samples);
    return samples;
}

} // namespace

// P1/07: private state, accessed only under dataMutex.
bool OverlayRenderer::isInGame_ = false;
AGameEvent_Soccar_TA* OverlayRenderer::currentGameEvent_ = nullptr;

bool OverlayRenderer::IsInGameActive() {
    std::lock_guard<std::mutex> lock(dataMutex);
    return isInGame_;
}

std::vector<CarBoostData> OverlayRenderer::carBoostData;
std::vector<FVector> OverlayRenderer::ballScreenPositions;
APRI_TA* OverlayRenderer::localPlayerPRI = nullptr;

bool OverlayRenderer::showMyBoost = true;
bool OverlayRenderer::showEnemyBoost = true;
bool OverlayRenderer::drawBallCenter = true;
bool OverlayRenderer::drawHitbox = false;
bool OverlayRenderer::drawBallPrediction = false;
bool OverlayRenderer::drawBoostTimers = false;

float OverlayRenderer::predLineThickness = 2.0f;
float OverlayRenderer::predTimeSeconds = 2.0f;
int   OverlayRenderer::predColorPreset = 0;

std::vector<BallPredictionSample> OverlayRenderer::ballPredictionSamples;
std::vector<BoostTimerBadge> OverlayRenderer::boostTimerBadges;
std::mutex OverlayRenderer::dataMutex;

void OverlayRenderer::Hook() {
    Events.HookEventPre(uehstr::fn_event_post_begin(), OnGameEventStart);
    Events.HookEventPre(uehstr::fn_event_destroyed(), OnGameEventDestroyed);
    Events.HookEventPre(uehstr::fn_event_active_begin(), OnGameEventStart);
    Events.HookEventPre(uehstr::fn_event_countdown_begin(), OnGameEventStart);
    Events.HookEventPost(uehstr::fn_player_controller_tick(), PlayerTickCalled);
}

void OverlayRenderer::OnGameEventDestroyed(PreEvent& event)
{
    (void)event;
    try
    {
        std::lock_guard<std::mutex> lock(dataMutex);
        // P1/07: clear isInGame_ before nulling the pointer so even an
        // imaginary unlocked reader sees "not in game" first.
        isInGame_ = false;
        currentGameEvent_ = nullptr;
        localPlayerPRI = nullptr;
        carBoostData.clear();
        ballScreenPositions.clear();
        ballPredictionSamples.clear();
        boostTimerBadges.clear();
    }
    catch (...) { Console.Error("GameEventHook: Exception in OnGameEventDestroyed"); }
}

void OverlayRenderer::OnGameEventStart(PreEvent& event)
{
    try
    {
        Console.Write("GameEventHook: Game event started: " + std::string(event.Function()->GetName()));
        if (event.Caller() && event.Caller()->IsA(AGameEvent_Soccar_TA::StaticClass()))
        {
            // P1/07: only flip isInGame_ when we actually captured the event.
            std::lock_guard<std::mutex> lock(dataMutex);
            currentGameEvent_ = static_cast<AGameEvent_Soccar_TA*>(event.Caller());
            isInGame_ = true;
            Console.Write("GameEventHook: Stored GameEvent instance");
        }
    }
    catch (...) { Console.Error("GameEventHook: Exception in OnGameEventStart"); }
}

void OverlayRenderer::PlayerTickCalled(const PostEvent& event) {
    if (!event.Caller() || !event.Caller()->IsA(APlayerController_TA::StaticClass())) {
        return;
    }

    // P1/07: take the lock FIRST, then snapshot state, so a concurrent destroy
    // can't null currentGameEvent_ between a check and the dereference.
    std::lock_guard<std::mutex> lock(dataMutex);
    if (!isInGame_ || !currentGameEvent_) {
        return;
    }
    AGameEvent_Soccar_TA* gevt = currentGameEvent_;

    carBoostData.clear();
    ballScreenPositions.clear();
    ballPredictionSamples.clear();
    boostTimerBadges.clear();

    TArray<APlayerController_TA*> localPlayers = gevt->LocalPlayers;
    if (localPlayers.size() == 0 || !localPlayers[0]) {
        return;
    }
    APlayerController_TA* localPlayerController = localPlayers[0];
    localPlayerPRI = localPlayerController->PRI;

    TArray<ACar_TA*> cars = gevt->Cars;
    TArray<ABall_TA*> balls = gevt->GameBalls;

    // P3/02: dead empty-body for-loop removed — used to read car/PRI per
    // local player into unused locals on every render frame.

    int localTeamIdx = -1;
    if (localPlayerPRI && localPlayerPRI->Team) {
        localTeamIdx = localPlayerPRI->Team->TeamIndex;
    }

   
    for (ACar_TA* car : cars) {
        if (!car) continue;

        FVector carLocation = car->Location;

        FVector boostCircleLocation = carLocation;
        boostCircleLocation.Z += 100.0f; 

        FVector screenPos = Drawing::CalculateScreenCoordinate(boostCircleLocation, localPlayerController);


        ACarComponent_Boost_TA* boostComponent = car->BoostComponent;
        float boostAmount = 0.0f;
        try {
            if (boostComponent) {
                boostAmount = boostComponent->CurrentBoostAmount * 100;
            }
            else {
                boostAmount = 0.0f;
            }
        }
        catch (...) {
            boostAmount = 0.0f;
        }

       
        bool isLocal = false;
        bool isEnemy = false;
        for (APlayerController_TA* lp : localPlayers) {
            if (lp && lp->Car == car) { isLocal = true; break; }
        }
        if (!isLocal && localTeamIdx >= 0) {
            APRI_TA* carPRI = car->PRI;
            if (carPRI && carPRI->Team) {
                isEnemy = (carPRI->Team->TeamIndex != localTeamIdx);
            }
        }

        CarBoostData data;
        data.screenPosition = screenPos;
        data.boostAmount = boostAmount;
        data.isLocalCar = isLocal;
        data.isEnemy = isEnemy;
        carBoostData.push_back(data);
    }


    for (ABall_TA* ball : balls) {
        if (!ball) continue;
        FVector ballLocation = ball->Location;
        FVector screenPos = Drawing::CalculateScreenCoordinate(ballLocation, localPlayerController);
        ballScreenPositions.push_back(screenPos);

        if (drawBallPrediction) {
            ballPredictionSamples = BuildPredictionSamples(ball, localPlayerController, predTimeSeconds);
        }
    }

    if (drawBoostTimers) {
        // P1/01: GetPadStates now returns a snapshot taken under guiMutex_.
        const std::array<BoostPadState, 34> padStates = BotMod.GetPadStates();
        for (int i = 0; i < 34; i++) {
            if (padStates[i].available) {
                continue;
            }

            FVector worldPos = GoySDK::BOOST_LOCATIONS[i];
            worldPos.Z += kBoostBadgeLift;

            BoostTimerBadge badge{};
            badge.screenPosition = Drawing::CalculateScreenCoordinate(worldPos, localPlayerController);
            badge.timeRemaining = padStates[i].timer;
            badge.label = FormatBoostTimerLabel(padStates[i].timer);
            boostTimerBadges.push_back(std::move(badge));
        }
    }

}

void OverlayRenderer::OnRender() {
    // P1/07: lock first, then check state. The previous unlocked check could
    // see a stale "true" while caches were being torn down by OnGameEventDestroyed.
    std::lock_guard<std::mutex> lock(dataMutex);
    if (!isInGame_) {
        return;
    }

    ImDrawList* drawList = ImGui::GetBackgroundDrawList();

   
    for (const CarBoostData& data : carBoostData) {
       
        if (data.isLocalCar && !showMyBoost) continue;
        if (data.isEnemy && !showEnemyBoost) continue;
       
        if (!data.isLocalCar && !data.isEnemy) continue;

        if (data.screenPosition.Z == 0) {
            float boostPercentage = data.boostAmount / 100.0f;
            if (boostPercentage < 0.0f) boostPercentage = 0.0f;
            if (boostPercentage > 1.0f) boostPercentage = 1.0f;

            ImVec2 center(data.screenPosition.X, data.screenPosition.Y);
            float radius = 25.0f; 

            drawList->AddCircleFilled(center, radius, IM_COL32(50, 50, 50, 180));

            if (boostPercentage > 0.0f) {
                float startAngle = -IM_PI * 0.5f;
                float endAngle = startAngle + (2.0f * IM_PI * boostPercentage);

                ImU32 boostColor;
                if (boostPercentage < 0.33f) {
                    boostColor = IM_COL32(255, 80, 80, 220);
                }
                else if (boostPercentage < 0.66f) {
                    boostColor = IM_COL32(255, 200, 0, 220);
                }
                else {
                    boostColor = IM_COL32(80, 255, 80, 220);
                }

                drawList->PathClear();
                drawList->PathLineTo(center);

                const int numSegments = 32;
                for (int i = 0; i <= numSegments; i++) {
                    float angle = startAngle + (endAngle - startAngle) * ((float)i / numSegments);
                    float x = center.x + cosf(angle) * radius;
                    float y = center.y + sinf(angle) * radius;
                    drawList->PathLineTo(ImVec2(x, y));
                }

                drawList->PathLineTo(center);
                drawList->PathFillConvex(boostColor);
            }

            drawList->AddCircle(center, radius, IM_COL32(255, 255, 255, 150), 32, 2.0f);

            std::string boostText = std::to_string((int)data.boostAmount);
            ImVec2 textSize = ImGui::CalcTextSize(boostText.c_str());
            ImVec2 textPos(center.x - textSize.x * 0.5f, center.y - textSize.y * 0.5f);
            drawList->AddText(textPos, IM_COL32(255, 255, 255, 255), boostText.c_str());
        }
    }


    if (drawBallCenter) {
        for (const FVector& screenPos : ballScreenPositions) {
            if (screenPos.Z == 0) {
                drawList->AddCircleFilled(ImVec2(screenPos.X, screenPos.Y), 8.f, IM_COL32(0, 255, 0, 255));
            }
        }
    }

    if (drawBallPrediction && ballPredictionSamples.size() > 1) {
        static const ImU32 predColors[] = {
            IM_COL32(0, 220, 255, 200),
            IM_COL32(80, 255, 80, 200),
            IM_COL32(255, 220, 0, 200),
            IM_COL32(255, 80, 80, 200),
            IM_COL32(255, 255, 255, 200),
        };
        int ci = predColorPreset;
        if (ci < 0 || ci > 4) ci = 0;
        ImU32 lineColor = predColors[ci];

        constexpr float kMinSegPx = 1.5f;
        for (size_t i = 1; i < ballPredictionSamples.size(); i++) {
            const auto& a = ballPredictionSamples[i - 1];
            const auto& b = ballPredictionSamples[i];
            if (!IsScreenPointVisible(a.screenPosition) || !IsScreenPointVisible(b.screenPosition))
                continue;
            const float dx = b.screenPosition.X - a.screenPosition.X;
            const float dy = b.screenPosition.Y - a.screenPosition.Y;
            if ((dx * dx + dy * dy) < kMinSegPx * kMinSegPx)
                continue;
            float t = static_cast<float>(i) / static_cast<float>(ballPredictionSamples.size());
            ImU32 c = (lineColor & 0x00FFFFFF) | (static_cast<ImU32>((1.0f - t * 0.6f) * ((lineColor >> 24) & 0xFF)) << 24);
            drawList->AddLine(
                ImVec2(a.screenPosition.X, a.screenPosition.Y),
                ImVec2(b.screenPosition.X, b.screenPosition.Y),
                c,
                predLineThickness);
        }

        for (const auto& sample : ballPredictionSamples) {
            if (!IsScreenPointVisible(sample.screenPosition)) {
                continue;
            }

            const ImVec2 point(sample.screenPosition.X, sample.screenPosition.Y);
            if (sample.isBounce) {
                drawList->AddCircleFilled(point, kBounceMarkerRadius, lineColor);
                drawList->AddCircle(point, kBounceMarkerRadius + 2.0f, IM_COL32(255, 255, 255, 120), 24, 1.0f);
            }
            if (sample.isEndpoint) {
                drawList->AddCircleFilled(point, kEndpointMarkerRadius, lineColor);
                drawList->AddCircle(point, kEndpointMarkerRadius + 3.0f, IM_COL32(255, 255, 255, 160), 28, 1.5f);
            }
        }
    }

    // Hitbox: draw wireframe box around cars
    if (drawHitbox) {
        for (const CarBoostData& data : carBoostData) {
            if (data.screenPosition.Z != 0) continue;
            ImVec2 center(data.screenPosition.X, data.screenPosition.Y);
            // Approximate car hitbox as rectangle on screen
            float halfW = 22.0f, halfH = 12.0f;
            ImU32 hbColor = data.isEnemy ? IM_COL32(255, 100, 100, 180) : IM_COL32(100, 200, 255, 180);
            drawList->AddRect(
                ImVec2(center.x - halfW, center.y - halfH),
                ImVec2(center.x + halfW, center.y + halfH),
                hbColor, 2.0f, 0, 1.5f);
        }
    }

    if (drawBoostTimers) {
        for (const auto& badge : boostTimerBadges) {
            if (!IsScreenPointVisible(badge.screenPosition)) continue;

            const ImVec2 textSize = ImGui::CalcTextSize(badge.label.c_str());
            const ImVec2 center(badge.screenPosition.X, badge.screenPosition.Y - 8.0f);
            const ImVec2 rectMin(center.x - textSize.x * 0.5f - 5.0f, center.y - textSize.y * 0.5f - 3.0f);
            const ImVec2 rectMax(center.x + textSize.x * 0.5f + 5.0f, center.y + textSize.y * 0.5f + 3.0f);
            drawList->AddRectFilled(rectMin, rectMax, IM_COL32(8, 8, 12, 190), 4.0f);
            drawList->AddRect(rectMin, rectMax, IM_COL32(255, 180, 0, 72), 4.0f, 0, 1.0f);
            drawList->AddText(
                ImVec2(center.x - textSize.x * 0.5f, center.y - textSize.y * 0.5f),
                IM_COL32(255, 200, 0, 230),
                badge.label.c_str());
        }
    }

}

// P3/10: empty OnCreate/OnDestroy + their constructor/destructor calls removed.
// Module has no virtual OnCreate/OnDestroy hooks, so removing only the
// definitions would have produced a link error. Both ends are gone now.
OverlayRenderer::OverlayRenderer()
    : Module("GameEventHook", "Hooks into game events", States::STATES_All) {}

OverlayRenderer::~OverlayRenderer() = default;

void OverlayRenderer::Initialize() {
    Hook();
    Console.Write("OverlayRenderer Initialized.");
}


OverlayRenderer OverlayMod;
