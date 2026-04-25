#pragma once
#include "../Component.hpp"
#include <cstdint>


class CoreComponent : public Component
{
private:
	HANDLE MainThread;

public:
	CoreComponent();
	~CoreComponent() override;

public:
	void OnCreate() override;
	void OnDestroy() override;

public:
	void InitializeThread();
	void DestroyThread();
	static void InitializeGlobals(HMODULE hModule);

private:

	static uintptr_t FindPattern(const char* pattern);

	static bool AreGlobalsValid();
	static bool AreGlobalsValidSafe();
	static bool AreGObjectsValid();
	static bool AreGNamesValid();
};

extern class CoreComponent Core;
