#include "ViGemController.hpp"


#include <ViGEm/Client.h>

#include <algorithm>

namespace GoySDK {

ViGemController::ViGemController() {}

ViGemController::~ViGemController() {
    Shutdown();
}

bool ViGemController::Initialize() {
    if (client_) return true;

    client_ = vigem_alloc();
    if (!client_) return false;

    VIGEM_ERROR err = vigem_connect(client_);
    if (!VIGEM_SUCCESS(err)) {
        vigem_free(client_);
        client_ = nullptr;
        return false;
    }
    return true;
}

void ViGemController::Shutdown() {
    DisconnectAll();
    if (client_) {
        vigem_disconnect(client_);
        vigem_free(client_);
        client_ = nullptr;
    }
}

bool ViGemController::ConnectPad(int slotIdx) {
    if (slotIdx < 0 || slotIdx >= kMaxPads) return false;
    if (!client_) return false;
    if (connected_[slotIdx]) return true;

    PVIGEM_TARGET target = vigem_target_x360_alloc();
    if (!target) return false;

    VIGEM_ERROR err = vigem_target_add(client_, target);
    if (!VIGEM_SUCCESS(err)) {
        vigem_target_free(target);
        return false;
    }

    targets_[slotIdx] = target;
    connected_[slotIdx] = true;
    return true;
}

void ViGemController::DisconnectPad(int slotIdx) {
    if (slotIdx < 0 || slotIdx >= kMaxPads) return;
    if (!connected_[slotIdx]) return;

    if (targets_[slotIdx] && client_) {
        vigem_target_remove(client_, targets_[slotIdx]);
        vigem_target_free(targets_[slotIdx]);
    }
    targets_[slotIdx] = nullptr;
    connected_[slotIdx] = false;
}

void ViGemController::DisconnectAll() {
    for (int i = 0; i < kMaxPads; i++) {
        DisconnectPad(i);
    }
}

bool ViGemController::IsPadConnected(int slotIdx) const {
    if (slotIdx < 0 || slotIdx >= kMaxPads) return false;
    return connected_[slotIdx];
}

int ViGemController::ConnectedPadCount() const {
    int count = 0;
    for (int i = 0; i < kMaxPads; i++) {
        if (connected_[i]) count++;
    }
    return count;
}

void ViGemController::SendInput(int slotIdx, float throttle, float steer, float pitch, float yaw, float roll,
                                 bool jump, bool boost, bool handbrake) {
    if (slotIdx < 0 || slotIdx >= kMaxPads || !connected_[slotIdx]) return;

    XUSB_REPORT report = {};
    XUSB_REPORT_INIT(&report);

   
    bool wantAirRoll = handbrake || (std::abs(roll) > 0.1f);
    float stickX = wantAirRoll ? roll : steer;

    report.sThumbLX = static_cast<SHORT>(std::clamp(stickX, -1.f, 1.f) * 32767.f);
    report.sThumbLY = static_cast<SHORT>(std::clamp(-pitch, -1.f, 1.f) * 32767.f);

   
    if (throttle > 0.f) {
        report.bRightTrigger = static_cast<BYTE>(std::clamp(throttle, 0.f, 1.f) * 255.f);
        report.bLeftTrigger = 0;
    } else {
        report.bRightTrigger = 0;
        report.bLeftTrigger = static_cast<BYTE>(std::clamp(-throttle, 0.f, 1.f) * 255.f);
    }

   
    if (jump)         report.wButtons |= XUSB_GAMEPAD_A;
    if (boost)        report.wButtons |= XUSB_GAMEPAD_B;
    if (wantAirRoll)  report.wButtons |= XUSB_GAMEPAD_X; 

    VIGEM_ERROR err = vigem_target_x360_update(client_, targets_[slotIdx], report);
    if (!VIGEM_SUCCESS(err)) {
        connected_[slotIdx] = false;
    }
}

void ViGemController::SendNeutral(int slotIdx) {
    if (slotIdx < 0 || slotIdx >= kMaxPads || !connected_[slotIdx]) return;

    XUSB_REPORT report = {};
    XUSB_REPORT_INIT(&report);
    VIGEM_ERROR err = vigem_target_x360_update(client_, targets_[slotIdx], report);
    if (!VIGEM_SUCCESS(err)) {
        connected_[slotIdx] = false;
    }
}

void ViGemController::PressJoin(int slotIdx) {
    if (slotIdx < 0 || slotIdx >= kMaxPads || !connected_[slotIdx]) return;

    XUSB_REPORT report = {};
    XUSB_REPORT_INIT(&report);
    report.wButtons |= XUSB_GAMEPAD_START;
    VIGEM_ERROR err = vigem_target_x360_update(client_, targets_[slotIdx], report);
    if (!VIGEM_SUCCESS(err)) {
        connected_[slotIdx] = false;
    }
}

} 
