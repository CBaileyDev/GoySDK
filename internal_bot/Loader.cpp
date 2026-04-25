#include <Windows.h>
#include <cstdio>
#include <string>
#include <fstream>

static std::wstring GetDllDirectory(HMODULE hModule)
{
    WCHAR dllPath[1024] = {};
    DWORD len = GetModuleFileNameW(hModule, dllPath, _countof(dllPath));
    if (len == 0 || len >= _countof(dllPath)) {
        return L"";
    }

    WCHAR* lastSlash = wcsrchr(dllPath, L'\\');
    if (!lastSlash) {
        return L"";
    }
    *lastSlash = L'\0';
    return dllPath;
}

static void WriteBridgeLog(const std::wstring& dllDir, const std::string& line)
{
    if (dllDir.empty()) return;
    const std::wstring logPath = dllDir + L"\\GoyLoaderBridge.log";
    std::ofstream out(logPath, std::ios::app);
    if (out.is_open()) {
        out << line << "\n";
    }
}

static HMODULE PreloadDll(const std::wstring& dllDir, const wchar_t* name)
{
    WCHAR path[1024];
    _snwprintf_s(path, _countof(path), _TRUNCATE, L"%s\\%s", dllDir.c_str(), name);
    HMODULE h = LoadLibraryW(path);
    if (!h) {
        DWORD err = GetLastError();
        char msg[512];
        char narrowName[256];
        WideCharToMultiByte(CP_ACP, 0, name, -1, narrowName, sizeof(narrowName), NULL, NULL);
        snprintf(msg, sizeof(msg), "Loader: Preload %s failed (error %lu / 0x%08X)",
                 narrowName, err, err);
        WriteBridgeLog(dllDir, msg);
    } else {
        char msg[256];
        char narrowName[128];
        WideCharToMultiByte(CP_ACP, 0, name, -1, narrowName, sizeof(narrowName), NULL, NULL);
        snprintf(msg, sizeof(msg), "Loader: Preloaded %s ok", narrowName);
        WriteBridgeLog(dllDir, msg);
    }
    return h;
}

static DWORD WINAPI LoaderInitThread(LPVOID param)
{
    HMODULE hModule = reinterpret_cast<HMODULE>(param);
    const std::wstring dllDir = GetDllDirectory(hModule);
    if (dllDir.empty()) {
        OutputDebugStringA("Loader: failed to resolve DLL directory");
        return 0;
    }

    WriteBridgeLog(dllDir, "Loader: init thread start");

    // Use AddDllDirectory + SetDefaultDllDirectories so all nested dependency
    // resolution also searches the payload directory.
    typedef DLL_DIRECTORY_COOKIE (WINAPI *AddDllDirectoryFunc)(PCWSTR);
    typedef BOOL (WINAPI *SetDefaultDllDirectoriesFunc)(DWORD);

    HMODULE hKernel = GetModuleHandleW(L"kernel32.dll");
    auto pAddDllDir = (AddDllDirectoryFunc)GetProcAddress(hKernel, "AddDllDirectory");
    auto pSetDefaultDirs = (SetDefaultDllDirectoriesFunc)GetProcAddress(hKernel, "SetDefaultDllDirectories");

    if (pAddDllDir && pSetDefaultDirs) {
        // LOAD_LIBRARY_SEARCH_DEFAULT_DIRS = application dir + system32 + user dirs
        pSetDefaultDirs(0x00001000); // LOAD_LIBRARY_SEARCH_DEFAULT_DIRS
        DLL_DIRECTORY_COOKIE cookie = pAddDllDir(dllDir.c_str());
        if (cookie) {
            WriteBridgeLog(dllDir, "Loader: AddDllDirectory ok");
        } else {
            WriteBridgeLog(dllDir, "Loader: AddDllDirectory failed, falling back to SetDllDirectoryW");
            SetDllDirectoryW(dllDir.c_str());
        }
    } else {
        // Fallback for older Windows
        SetDllDirectoryW(dllDir.c_str());
        WriteBridgeLog(dllDir, "Loader: SetDllDirectoryW (fallback) ok");
    }

    // Explicitly pre-load all LibTorch DLLs in dependency order (leaf-first).
    // This ensures every symbol is in-memory before GoySDKCore.dll is loaded,
    // preventing ERROR_PROC_NOT_FOUND (127) during nested dependency resolution.
    WriteBridgeLog(dllDir, "Loader: pre-loading LibTorch dependency chain...");
    const wchar_t* preloadOrder[] = {
        L"uv.dll",                // libuv (no torch deps)
        L"asmjit.dll",            // jit assembler (no torch deps)
        L"libiomp5md.dll",        // OpenMP (no torch deps)
        L"libiompstubs5md.dll",   // OpenMP stubs
        L"fbgemm.dll",            // depends on asmjit + libiomp5md
        L"c10.dll",               // core tensor library (system deps only)
        L"torch_global_deps.dll", // global dep loader (system deps only)
        L"torch.dll",             // thin torch shim
        L"torch_cpu.dll",         // depends on c10 + fbgemm + libiomp5md
    };
    int preloadFailed = 0;
    for (const wchar_t* dll : preloadOrder) {
        HMODULE h = PreloadDll(dllDir, dll);
        if (!h) preloadFailed++;
    }

    char preMsg[256];
    snprintf(preMsg, sizeof(preMsg), "Loader: pre-load complete (%d/%d succeeded)",
             (int)(sizeof(preloadOrder)/sizeof(preloadOrder[0])) - preloadFailed,
             (int)(sizeof(preloadOrder)/sizeof(preloadOrder[0])));
    WriteBridgeLog(dllDir, preMsg);

    // Now load GoySDKCore.dll — all dependencies should already be in memory.
    WCHAR corePath[1024];
    _snwprintf_s(corePath, _countof(corePath), _TRUNCATE, L"%s\\GoySDKCore.dll", dllDir.c_str());

    HMODULE hCore = nullptr;
    DWORD lastErr = 0;
    const int maxAttempts = 3;
    for (int attempt = 1; attempt <= maxAttempts; ++attempt)
    {
        hCore = LoadLibraryW(corePath);
        if (hCore) break;

        lastErr = GetLastError();
        char msg[512];
        snprintf(msg, sizeof(msg),
                 "Loader: LoadLibraryW attempt %d/%d failed (error %lu / 0x%08X)",
                 attempt, maxAttempts, lastErr, lastErr);
        OutputDebugStringA(msg);
        WriteBridgeLog(dllDir, msg);

        Sleep(1000);
    }

    if (!hCore)
    {
        char msg[512];
        snprintf(msg, sizeof(msg),
                 "Loader: Failed to load GoySDKCore.dll after %d attempts\nLast Error: %lu (0x%08X)",
                 maxAttempts, lastErr, lastErr);
        OutputDebugStringA(msg);
        WriteBridgeLog(dllDir, msg);
        MessageBoxA(NULL, msg, "Loader Error", MB_OK | MB_ICONERROR);
    }
    else {
        OutputDebugStringA("Loader: GoySDKCore.dll loaded successfully");
        WriteBridgeLog(dllDir, "Loader: GoySDKCore.dll loaded successfully");
    }

    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    if (ul_reason_for_call == DLL_PROCESS_ATTACH)
    {
        const std::wstring dllDir = GetDllDirectory(hModule);
        WriteBridgeLog(dllDir, "Loader: DllMain PROCESS_ATTACH");
        DisableThreadLibraryCalls(hModule);
        HANDLE hThread = CreateThread(nullptr, 0, LoaderInitThread, hModule, 0, nullptr);
        if (hThread) {
            WriteBridgeLog(dllDir, "Loader: init thread created");
            CloseHandle(hThread);
        } else {
            DWORD err = GetLastError();
            char msg[256];
            snprintf(msg, sizeof(msg),
                     "Loader: failed to create init thread\nError: %lu (0x%08X)",
                     err, err);
            OutputDebugStringA(msg);
            WriteBridgeLog(dllDir, msg);
        }
    }
    return TRUE;
}
