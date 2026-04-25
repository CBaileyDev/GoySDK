#include "Components/Includes.hpp"

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
        CreateThread(nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(Core.InitializeGlobals), hModule, 0, nullptr);
    }
    if (ul_reason_for_call == DLL_PROCESS_DETACH)
    {
        GUI.Unload();
    }

    return TRUE;
}
