#pragma once
#include "../Component.hpp"
#include <mutex>

class MainComponent : public Component
{
public:
	MainComponent();
	~MainComponent() override;

	void OnCreate() override;
	void OnDestroy() override;

	void Initialize();

public:
	static std::vector<std::function<void()>> GameFunctions;
	static std::mutex GameFunctionsMutex;

	static void Execute(std::function<void()> FunctionToExecute)
	{
		std::lock_guard<std::mutex> lock(GameFunctionsMutex);
		GameFunctions.push_back(FunctionToExecute);
	}

	static void SpawnNotification(const std::string& Title, const std::string& Content, int Duration, UClass* NotificationClass = nullptr);

};

extern class MainComponent Main;
