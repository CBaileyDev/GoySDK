#include "Components/Includes.hpp"

// P3/03: trampoline with the correct LPTHREAD_START_ROUTINE signature.
// Previously the call site `reinterpret_cast<LPTHREAD_START_ROUTINE>(Core.InitializeGlobals)`
// (a) used instance-style syntax for a static method, and (b) cast a
// `void(HMODULE)` to a `DWORD WINAPI(LPVOID)` thread proc. It happened to
// work but was UB-adjacent and hid the leaked thread handle.
static DWORD WINAPI InitializeGlobalsTrampoline(LPVOID param)
{
    CoreComponent::InitializeGlobals(static_cast<HMODULE>(param));
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    if (ul_reason_for_call == DLL_PROCESS_ATTACH)
    {
        // Set DLL search directory so LibTorch/ViGEm DLLs resolve from our location.
        WCHAR dllDir[1024];
        DWORD len = GetModuleFileNameW(hModule, dllDir, _countof(dllDir));
        if (len > 0 && len < _countof(dllDir)) {
            WCHAR* lastSlash = wcsrchr(dllDir, L'\\');
            if (lastSlash) *lastSlash = L'\0';
            SetDllDirectoryW(dllDir);
        }

        DisableThreadLibraryCalls(hModule);

        // P3/03: typed trampoline + close the handle. Closing the handle does
        // not stop the thread; it only releases this DLL's handle reference.
        HANDLE initThread = CreateThread(nullptr, 0, InitializeGlobalsTrampoline, hModule, 0, nullptr);
        if (initThread) {
            CloseHandle(initThread);
        }
    }
    if (ul_reason_for_call == DLL_PROCESS_DETACH)
    {
        GUI.Unload();
    }

    return TRUE;
}
