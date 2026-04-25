#include "Core.hpp"
#include "../Includes.hpp"
#include "../Offsets.hpp"
#include <filesystem>
#include <psapi.h>
#include <vector>
#include <sstream>

CoreComponent::CoreComponent() : Component("Core", "Initializes globals, components, and modules.")
{
    OnCreate();
}

CoreComponent::~CoreComponent() {
    OnDestroy();
}

void CoreComponent::OnCreate()
{
    MainThread = nullptr;
}

void CoreComponent::OnDestroy() {
    DestroyThread();
}

void CoreComponent::InitializeThread()
{
    MainThread = CreateThread(nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(InitializeGlobals), nullptr, 0, nullptr);
}

void CoreComponent::DestroyThread()
{
    if (MainThread) {
        CloseHandle(MainThread);
        MainThread = nullptr;
    }
}


uintptr_t CoreComponent::FindPattern(const char* pattern)
{
    const auto moduleHandle = GetModuleHandle(NULL);
    if (!moduleHandle)
    {
        return 0;
    }

    MODULEINFO moduleInfo;
    if (!GetModuleInformation(GetCurrentProcess(), moduleHandle, &moduleInfo, sizeof(MODULEINFO)))
    {
        return 0;
    }

    const uintptr_t base = reinterpret_cast<uintptr_t>(moduleHandle);
    const uintptr_t size = moduleInfo.SizeOfImage;

   
    std::vector<int> patternBytes;
    std::stringstream ss(pattern);
    std::string byteStr;
    while (ss >> byteStr)
    {
        if (byteStr == "??")
        {
            patternBytes.push_back(-1);
        }
        else
        {
            patternBytes.push_back(std::stoi(byteStr, nullptr, 16));
        }
    }

    const size_t patternSize = patternBytes.size();
    const int* patternData = patternBytes.data();

   
    for (uintptr_t i = 0; i < size - patternSize; ++i)
    {
        bool found = true;
        for (size_t j = 0; j < patternSize; ++j)
        {
            const auto memoryByte = *reinterpret_cast<const uint8_t*>(base + i + j);
            if (patternData[j] != -1 && patternData[j] != memoryByte)
            {
                found = false;
                break;
            }
        }

        if (found)
        {
            return base + i;
        }
    }

    return 0;
}

void CoreComponent::InitializeGlobals(HMODULE hModule)
{
    char dllPath[MAX_PATH];
    GetModuleFileNameA(hModule, dllPath, MAX_PATH);
    char* lastSlash = strrchr(dllPath, '\\');
    if (lastSlash) *lastSlash = '\0';
    std::filesystem::path logDir(dllPath);

    Console.Initialize(logDir, "GoySDK.log");
    Console.Write("[Core Module] Initializing globals...");

    uintptr_t BaseAddress = reinterpret_cast<uintptr_t>(GetModuleHandle(NULL));
    if (!BaseAddress)
    {
        Console.Error("[Core Module] Failed to resolve game base address.");
        return;
    }
    Console.Notify("[Core Module] Entry Point " + Format::ToHex(reinterpret_cast<void*>(BaseAddress)));

    auto tryGlobals = [&](uintptr_t gnamesAddress, uintptr_t gobjectsAddress, const char* sourceLabel) -> bool
    {
        Console.Write(std::string("[Core Module] Trying globals from ") + sourceLabel + "...");
        Console.Write("[Core Module] Candidate GNames: " + Format::ToHex(reinterpret_cast<void*>(gnamesAddress)));
        Console.Write("[Core Module] Candidate GObjects: " + Format::ToHex(reinterpret_cast<void*>(gobjectsAddress)));

        GNames = reinterpret_cast<TArray<FNameEntry*>*>(gnamesAddress);
        GObjects = reinterpret_cast<TArray<UObject*>*>(gobjectsAddress);

        if (AreGlobalsValidSafe())
        {
            Console.Notify(std::string("[Core Module] Global validation passed via ") + sourceLabel + ".");
            return true;
        }

        Console.Warning(std::string("[Core Module] Global validation failed via ") + sourceLabel + ".");
        return false;
    };

    // The game engine may not have populated GNames/GObjects yet at DLL load time.
    // Retry the globals resolution with delays to give the engine time to start up.
    // Total wait: up to ~30 seconds (15 attempts * 2 seconds each).
    const int kMaxRetries = 15;
    const int kRetryDelayMs = 2000;
    bool globalsInitialized = false;

    for (int retry = 0; retry < kMaxRetries && !globalsInitialized; retry++)
    {
        if (retry > 0)
        {
            char msg[128];
            snprintf(msg, sizeof(msg), "[Core Module] Globals not ready, retrying in %dms... (attempt %d/%d)",
                     kRetryDelayMs, retry + 1, kMaxRetries);
            Console.Write(msg);
            Sleep(kRetryDelayMs);
        }

        // Try SDK offsets first
        const uintptr_t gnamesFromSdk = BaseAddress + GNAMES_OFFSET;
        const uintptr_t gobjectsFromSdk = BaseAddress + GOBJECTS_OFFSET;
        globalsInitialized = tryGlobals(gnamesFromSdk, gobjectsFromSdk, "SDK offsets");

        // Pattern scan fallback: try several known GNames signatures across client builds.
        if (!globalsInitialized)
        {
            struct PatternEntry {
                const char* pattern;
                uintptr_t gobjectsOffset;
                const char* label;
            };

            static const PatternEntry patterns[] = {
                { "?? ?? ?? ?? ?? ?? 00 00 ?? ?? 01 00 35 25 02 00", 0x48, "GNames v1" },
                { "?? ?? ?? ?? ?? ?? 00 00 ?? ?? 01 00 34 25 02 00", 0x48, "GNames v2" },
                { "?? ?? ?? ?? ?? ?? 00 00 ?? ?? 01 00 36 25 02 00", 0x48, "GNames v3" },
                { "?? ?? ?? ?? ?? ?? 00 00 ?? ?? 01 00 ?? 25 02 00", 0x48, "GNames broad" },
            };

            for (const auto& entry : patterns)
            {
                if (globalsInitialized) break;

                Console.Write(std::string("[Core Module] Trying pattern: ") + entry.label);
                const uintptr_t gnamesFromPattern = FindPattern(entry.pattern);
                if (gnamesFromPattern)
                {
                    const uintptr_t gobjectsFromPattern = gnamesFromPattern + entry.gobjectsOffset;
                    globalsInitialized = tryGlobals(gnamesFromPattern, gobjectsFromPattern, entry.label);
                }
                else
                {
                    Console.Warning(std::string("[Core Module] Pattern scan failed: ") + entry.label);
                }
            }
        }
    }

    if (!globalsInitialized)
    {
        Console.Error("[Core Module] Failed to initialize globals after all retries.");
        Console.Error("[Core Module] SDK offsets and patterns may need to be regenerated for this game build.");
        Console.Error("[Core Module] Game base address: " + Format::ToHex(reinterpret_cast<void*>(BaseAddress)));
        Console.Error("[Core Module] Expected GNames at base + 0x" + Format::ToHex(reinterpret_cast<void*>(GNAMES_OFFSET)));
        Console.Error("[Core Module] Expected GObjects at base + 0x" + Format::ToHex(reinterpret_cast<void*>(GOBJECTS_OFFSET)));
        Console.Error("[Core Module] Also possible: game was not at the main menu when injection occurred.");
        Console.Error("[Core Module] Try injecting after reaching the main menu.");
        return;
    }

    if (AreGlobalsValidSafe())
    {
        Console.Notify("[Core Module] Global Objects: " + Format::ToHex(GObjects));
        Console.Notify("[Core Module] Global Names: " + Format::ToHex(GNames));
        Console.Write("[Core Module] Initialized!");

        void** UnrealVTable = reinterpret_cast<void**>(UObject::StaticClass()->VfTableObject.Dummy);
        EventsComponent::AttachDetour(reinterpret_cast<ProcessEventType>(UnrealVTable[67]));

        Instances.Initialize();
        Events.Initialize();
        GUI.Initialize();
        Main.Initialize();
    }
    else
    {
        Console.Error("[Core Module] GObjects/GNames resolved but failed final validation.");
        Console.Error("[Core Module] Regenerate SDK offsets/patterns for the current game build.");
    }
}

bool CoreComponent::AreGlobalsValid()
{
    return (AreGObjectsValid() && AreGNamesValid());
}

bool CoreComponent::AreGlobalsValidSafe()
{
#if defined(_MSC_VER)
    __try
    {
        return AreGlobalsValid();
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
#else
    try
    {
        return AreGlobalsValid();
    }
    catch (...)
    {
        return false;
    }
#endif
}

bool CoreComponent::AreGObjectsValid()
{
    if (GObjects
        && UObject::GObjObjects()->size() > 0
        && UObject::GObjObjects()->capacity() > UObject::GObjObjects()->size())
    {
        return true;
    }
    return false;
}

bool CoreComponent::AreGNamesValid()
{
    if (GNames
        && FName::Names()->size() > 0
        && FName::Names()->capacity() > FName::Names()->size())
    {
        return true;
    }
    return false;
}

CoreComponent Core;
