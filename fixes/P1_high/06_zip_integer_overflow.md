# P1 / 06 — ZIP-parser bounds checks use `a + b > c` (overflow)

## TL;DR
The minimal ZIP parser in `repos/RLInference/src/Bot.cpp` validates buffer offsets with `localOffset + 30 > zipSize` and `rawData + compSize > end`. Both forms allow integer overflow on the LHS, after which the check passes for inputs that should be rejected. Combined with `compSize` and `localOffset` being attacker-controlled (in any future scenario where ZIPs come from outside the trusted embedded resources), this is an out-of-bounds-read primitive.

## Where
- File: `/Users/carterbarker/Documents/GoySDK/repos/RLInference/src/Bot.cpp`
- Function: anonymous `ExtractDataEntries`
- Lines: **76** (`if (localOffset + 30 > zipSize) continue;`), **88** (`if (rawData + compSize > end) continue;`)

## Problem
1. `localOffset` is `uint32_t`. If `localOffset == 0xFFFFFFFE`, then `localOffset + 30 == 0x0000001C` (wrap), which is less than any reasonable `zipSize`. The check passes. The subsequent `local = base + localOffset` reads ~4 GB past `base`, which on x64 may map to allocator metadata, on x86 overflows the address space and produces an access violation.
2. `rawData` is a pointer; `compSize` is `uint32_t`. `rawData + compSize` is computed in `size_t`. If `compSize == 0xFFFFFFFF` and `rawData` is high in the address space, the addition wraps and is less than `end`. The subsequent `assign(rawData, rawData + compSize)` reads 4 GB.
3. The same pattern exists for `cdOffset` at line 43 (`if (cdOffset >= zipSize) return false;`) — that one is OK because it's a `>=` direct comparison, not an addition.

## Why it matters
Today the ZIPs are embedded resources baked into the DLL, so the inputs are trusted. The moment someone adds "load model from disk" or "stream model from network" (an obvious next feature), this becomes an OOB-read sink that an attacker can hit by crafting a ZIP with sentinel offset values.

## Root cause
Author wrote the checks the natural-language way ("offset plus header size must not exceed file size") without thinking about overflow on the unsigned types being summed.

## Fix

### Step 1 — Rewrite both checks as subtractions

Edit `/Users/carterbarker/Documents/GoySDK/repos/RLInference/src/Bot.cpp`. Find:
```cpp
        // Jump to the local file header to find the actual data bytes.
        if (localOffset + 30 > zipSize) continue;
```

Replace with:
```cpp
        // Jump to the local file header to find the actual data bytes.
        // Use subtraction to avoid 32-bit overflow on attacker-controlled localOffset.
        if (zipSize < 30 || localOffset > zipSize - 30) continue;
```

Find (a few lines later):
```cpp
        // Use the size from the Central Directory (always correct).
        if (rawData + compSize > end) continue;
```

Replace with:
```cpp
        // Use the size from the Central Directory (always correct).
        // Re-express as subtraction so a malicious compSize / rawData can't wrap.
        if (rawData < base || rawData > end) continue;
        const size_t remaining = static_cast<size_t>(end - rawData);
        if (compSize > remaining) continue;
```

### Step 2 — Validate `localFnLen + localExLen` doesn't overflow `local + 30 + ...`

Find this line (around line 85):
```cpp
        const uint8_t* rawData = local + 30 + localFnLen + localExLen;
```

`localFnLen` and `localExLen` are both `uint16_t`, so their sum fits in `uint32_t`. But adding to `local + 30` could push past `end`. Add a check immediately after the line:

```cpp
        const uint8_t* rawData = local + 30 + localFnLen + localExLen;
        // Ensure the local-header walk landed inside the buffer before we trust rawData.
        if (rawData < local || rawData > end) continue;
```

(The `rawData < local` check catches pointer-arithmetic wrap on systems where pointer subtraction is undefined for OOB pointers — pragmatically, on Windows x64 this would be a UBSan finding.)

### Step 3 — Bound the EOCD scan

The EOCD scan at lines 31–37 iterates backwards from `end - 22` to `base`. For a giant non-ZIP buffer this is O(zipSize), which is wasted work. ZIP spec caps the EOCD comment at 65,535 bytes, so the scan should stop after `22 + 65535 = 65557` bytes from the end. Replace:

```cpp
    // 1) Find EOCD (signature 0x06054B50) by scanning backwards.
    const uint8_t* eocd = nullptr;
    for (const uint8_t* s = end - 22; s >= base; --s) {
        uint32_t sig;
        std::memcpy(&sig, s, 4);
        if (sig == 0x06054B50) { eocd = s; break; }
    }
    if (!eocd) return false;
```

With:

```cpp
    // 1) Find EOCD (signature 0x06054B50) by scanning backwards.
    // Spec caps the EOCD comment at 65535 bytes, so the EOCD itself is at most
    // 65557 bytes from the end. Bound the scan accordingly.
    const uint8_t* eocd = nullptr;
    constexpr size_t kMaxEocdScan = 22 + 0xFFFF;
    const uint8_t* scanFloor = (zipSize > kMaxEocdScan) ? (end - kMaxEocdScan) : base;
    for (const uint8_t* s = end - 22; s >= scanFloor; --s) {
        uint32_t sig;
        std::memcpy(&sig, s, 4);
        if (sig == 0x06054B50) { eocd = s; break; }
    }
    if (!eocd) return false;
```

### Step 4 — Defend `std::stoi`

Line 73:
```cpp
        int idx = std::stoi(numStr);
```

`std::stoi` throws `std::out_of_range` for inputs that don't fit in `int`. The constructor's outer `catch(...)` block in `Bot::Bot` swallows it and the bot enters stub mode. That's "safe" but it means a malformed entry name is treated identically to a working ZIP with no data. After P0/02 lands this becomes visible to the user, which is correct behavior — but you can also defensively bound it:

```cpp
        int idx = -1;
        try {
            idx = std::stoi(numStr);
        } catch (...) {
            continue;  // bad entry name — skip without aborting the whole archive
        }
        if (idx < 0) continue;
```

## Verification

1. **Build** — both `RLInference` standalone and `GoySDKCore`.
2. **Crafted-ZIP fuzz** — write a small test that hands `ExtractDataEntries` a series of crafted buffers:
   - Empty buffer (size 0) → returns false, no AV.
   - All-zero buffer of 1 MB → returns false, no AV.
   - Valid EOCD but `localOffset = 0xFFFFFFFE` → entry skipped, no AV.
   - Valid EOCD but `compSize = 0xFFFFFFFF` → entry skipped, no AV.
   - Valid EOCD but `localFnLen = 0xFFFF` → entry skipped, no AV.
3. **Regression** — existing models still load. Build, inject, confirm `[GoySDK] Slot 0: Loaded ABUSE` still fires.

## Don't do

- Do not switch to `(int64_t)localOffset + 30 > (int64_t)zipSize` "to avoid overflow." That works but obscures intent and breaks if anyone later changes `zipSize` to a `size_t`. Subtraction-form is the canonical fix.
- Do not delete the EOCD scan bound (Step 3) thinking "the inputs are trusted." Same reasoning as the rest of this fix: the moment an external-input feature is added, the bound matters.
- Do not assume `rawData + compSize > end` is "fine on x64 because addresses are 64-bit." The pointer arithmetic happens in `size_t`, but the *operand* `compSize` can still cause logical overflow at the comparison boundary.

## Related
- **P0/01** — same parser, the DEFLATE fix has overlapping changes. Apply both in one editing pass.
