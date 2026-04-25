#pragma once
#include <cstdint>


typedef struct _VIGEM_CLIENT_T* PVIGEM_CLIENT;
typedef struct _VIGEM_TARGET_T* PVIGEM_TARGET;

namespace GoySDK {


class ViGemController {
public:
    ViGemController();
    ~ViGemController();

   
    bool Initialize();
    void Shutdown();
    bool IsInitialized() const { return client_ != nullptr; }

   
    bool ConnectPad(int slotIdx);
    void DisconnectPad(int slotIdx);
    void DisconnectAll();
    bool IsPadConnected(int slotIdx) const;
    int  ConnectedPadCount() const;

   
    void SendInput(int slotIdx, float throttle, float steer, float pitch, float yaw, float roll,
                   bool jump, bool boost, bool handbrake);

   
    void SendNeutral(int slotIdx);

   
    void PressJoin(int slotIdx);

    static constexpr int kMaxPads = 4;

private:
    PVIGEM_CLIENT  client_ = nullptr;
    PVIGEM_TARGET  targets_[kMaxPads] = {};
    bool           connected_[kMaxPads] = {};
};

} 
