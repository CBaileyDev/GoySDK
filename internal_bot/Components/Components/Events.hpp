#pragma once
#include "../Component.hpp"

class PreEvent
{
protected:
	class UObject* PE_Caller;	
	class UFunction* PE_Function;
	void* PE_Params;			
	bool PE_Detour;				

public:
	PreEvent();
	PreEvent(class UObject* caller, class UFunction* function, void* params);
	~PreEvent();

public:
	class UObject* Caller() const;
	template <typename T> T* GetCaller() const;
	class UFunction* Function() const;
	void* Params() const;
	template <typename T> T* GetParams() const;
	bool ShouldDetour() const;
	void SetDetour(bool bDetour);

public:
	PreEvent operator=(const PreEvent& other);
};

class PostEvent : public PreEvent
{
private:
	void* PE_Result;			

public:
	PostEvent();
	PostEvent(class UObject* caller, class UFunction* function, void* params, void* result);
	~PostEvent();

public:
	void* Result() const;
	template <typename T> T* GetResult() const;

public:
	PostEvent operator=(const PostEvent& other);
};

namespace Hooks
{
	void HUDPostRender(PreEvent& event);
	void HUDPostRenderPost(const PostEvent& event);
	void GameViewPortPostRender(PreEvent& event);
	void GFxDataMainMenuAdded(PreEvent& event);
	void PlayerControllerTick(PreEvent& event);
	void GameViewPortKeyPress(const PostEvent& event);
}

typedef void(*ProcessEventType)(class UObject*, class UFunction*, void*, void*);


class EventsComponent : public Component
{
private:
	static inline bool Detoured;
	static inline ProcessEventType ProcessEvent;
	static inline std::map<uint32_t, std::vector<std::function<void(PreEvent&)>>> PreHookedEvents;
	static inline std::map<uint32_t, std::vector<std::function<void(const PostEvent&)>>> PostHookedEvents;
	static inline std::vector<uint32_t> BlacklistedEvents;

public:
	EventsComponent();
	~EventsComponent() override;

public:
	void OnCreate() override;
	void OnDestroy() override;

public:
	static bool IsDetoured();
	static void AttachDetour(const ProcessEventType& detourFunction);
	static void DetachDetour();
	static void ProcessEventDetour(class UObject* caller, class UFunction* function, void* params, void* result);
	static bool IsEventBlacklisted(uint32_t functionIndex);
	
public:
	void BlacklistEvent(const std::string& functionName);
	void WhitelistEvent(const std::string& functionName);
	void HookEventPre(const std::string& functionName, std::function<void(PreEvent&)> preHook);
	void HookEventPre(uint32_t functionIndex, std::function<void(PreEvent&)> preHook);
	void HookEventPost(const std::string& functionName, std::function<void(const PostEvent&)> postHook);
	void HookEventPost(uint32_t functionIndex, std::function<void(const PostEvent&)> postHook);
	void Initialize();
};

extern class EventsComponent Events;
