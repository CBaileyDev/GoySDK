#pragma once
#define WIN32_LEAN_AND_MEAN

// P3/06: removed unused `nullcheck` macro. It had no `do { ... } while(0)`
// wrapping, which made `if (cond) nullcheck(p); else foo();` silently misparse
// the `else`. The macro had zero call sites in the codebase.

#include <Windows.h>
#include <unordered_map>
#include <map>
#include <iostream>
#include <iomanip>
#include <functional>
#include <sstream>
#include <fstream>
#include <stdlib.h>
#include <stdio.h>
#include <chrono>
#include <thread>
#include <string>
#include <numeric>
#include <string_view>
#include <cmath>
#include <random>
#include <inttypes.h>
#include <filesystem>
#include <detours.h>

#include "RLSDK/SdkHeaders.hpp"
