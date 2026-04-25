#pragma once
#include "../pch.hpp"
#include "../Extensions/Includes.hpp"
#include "../Offsets.hpp"

template<typename T>
bool TryReadMemory(uintptr_t address, T* result) {
    __try {
        *result = *reinterpret_cast<T*>(address);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}


template <typename T>
T SafeRead(uintptr_t address) {
    T result = T{};
    TryReadMemory(address, &result);
    return result;
}


template <typename T>
bool SafeWrite(uintptr_t address, T value)
{
   
    if (address == 0)
    {
        return false;
    }

    __try
    {
       
        *reinterpret_cast<T*>(address) = value;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
       
        return false;
    }
}

template<typename T>
std::string to_hex(T i)
{
    std::stringstream stream;
    stream << "0x" << std::hex << i;
    return stream.str();
}


class Module
{
private:
	std::string Name;			
	std::string FormattedName;	
	std::string Description;	
	uint32_t AllowedStates;		
	bool Initialized;			

public:
	Module(const std::string& name, const std::string& description, uint32_t states);
	virtual ~Module();

public:
	std::string GetName() const;
	std::string GetNameFormatted() const;
	std::string GetDescription() const;
	uint32_t GetAllowedStates() const;
	bool IsAllowed() const;
	bool IsInitialized() const;
	void SetInitialized(bool bInitialized);
};
