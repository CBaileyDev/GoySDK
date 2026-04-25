# P1 / 06 — ZIP-parser bounds checks use overflowing offset and pointer arithmetic

## TL;DR
The minimal ZIP parser in `repos/RLInference/src/Bot.cpp` validates several attacker-controlled offsets with expressions such as `localOffset + 30 > zipSize` and `rawData + compSize > end`. Those forms can overflow before the comparison happens. Some of the earlier proposed fixes still performed pointer arithmetic before proving the pointer was inside the buffer, which is not safe enough.

Rewrite the parser's central-directory and local-header validation to use integer offsets and subtraction-form bounds checks. Only convert an offset to a pointer after proving the whole range is inside `[0, zipSize)`.

## Where
- File: `/Users/carterbarker/Documents/GoySDK/repos/RLInference/src/Bot.cpp`
- Function: anonymous `ExtractDataEntries`
- Current risky checks:
  - `for (const uint8_t* s = end - 22; s >= base; --s)` — pointer loop can step before `base`.
  - `if (cdOffset >= zipSize) return false;` — only checks start, not central-directory record ranges.
  - `for (... && cd + 46 <= end; ...)` — pointer addition before robust validation.
  - `if (localOffset + 30 > zipSize) continue;` — 32-bit overflow.
  - `const uint8_t* rawData = local + 30 + localFnLen + localExLen;` — pointer computed before range validation.
  - `if (rawData + compSize > end) continue;` — pointer addition can overflow.

## Why It Matters
Today the model archives are embedded resources, so the immediate risk is mostly malformed-resource crashes. But the code is exactly where a future “load model from disk” or “download model update” feature would land. If that happens with the current parser, a crafted ZIP can turn offset metadata into out-of-bounds reads or access violations.

Even with trusted resources, robust parsing is required before adding **P0/01** DEFLATE support because decompression code must never be handed invalid source ranges.

## Correct Fix Strategy
Use these rules throughout `ExtractDataEntries`:

1. Keep positions as `size_t` offsets until validation is complete.
2. Use `if (offset > size - needed)` style checks, guarded by `size >= needed`.
3. Validate central-directory variable-length fields before constructing strings.
4. Validate local-header variable-length fields before computing the data offset.
5. Only build pointers after the integer range is proven valid.
6. Avoid pointer loops that compare `s >= base` after decrementing; use integer reverse loops.

## Step 1 — Add Small Helpers
Inside the anonymous namespace in `Bot.cpp`, above `ExtractDataEntries`, add helpers like these:

```cpp
static bool HasRange(size_t total, size_t offset, size_t length) {
    return offset <= total && length <= total - offset;
}

static uint16_t ReadU16(const uint8_t* p) {
    uint16_t v = 0;
    std::memcpy(&v, p, sizeof(v));
    return v;
}

static uint32_t ReadU32(const uint8_t* p) {
    uint32_t v = 0;
    std::memcpy(&v, p, sizeof(v));
    return v;
}
```

`HasRange` is the important part. It avoids `offset + length` overflow.

## Step 2 — Rewrite EOCD Scan With Integer Offsets
Replace the pointer-based EOCD scan:

```cpp
const uint8_t* eocd = nullptr;
for (const uint8_t* s = end - 22; s >= base; --s) {
    uint32_t sig;
    std::memcpy(&sig, s, 4);
    if (sig == 0x06054B50) { eocd = s; break; }
}
if (!eocd) return false;
```

with:

```cpp
constexpr size_t kEocdMinSize = 22;
constexpr size_t kMaxZipComment = 0xFFFF;
constexpr size_t kMaxEocdSearch = kEocdMinSize + kMaxZipComment;

if (zipSize < kEocdMinSize) {
    return false;
}

const size_t searchStart = zipSize - kEocdMinSize;
const size_t searchFloor =
    (zipSize > kMaxEocdSearch) ? (zipSize - kMaxEocdSearch) : 0;

size_t eocdOffset = static_cast<size_t>(-1);
for (size_t off = searchStart;; --off) {
    if (ReadU32(base + off) == 0x06054B50) {
        eocdOffset = off;
        break;
    }
    if (off == searchFloor) {
        break;
    }
}

if (eocdOffset == static_cast<size_t>(-1)) {
    return false;
}

const uint8_t* eocd = base + eocdOffset;
```

This keeps the scan inside the legal EOCD search window and never decrements a pointer below `base`.

## Step 3 — Validate Central Directory Offset
Current code only checks:

```cpp
if (cdOffset >= zipSize) return false;
```

Replace the central-directory setup with:

```cpp
const uint16_t numEntries = ReadU16(eocd + 10);
const uint32_t cdOffset32 = ReadU32(eocd + 16);
const size_t cdStart = static_cast<size_t>(cdOffset32);

if (!HasRange(zipSize, cdStart, 46)) {
    return false;
}

size_t cdOff = cdStart;
```

Do not create `const uint8_t* cd = base + cdOffset` until the range check passes.

## Step 4 — Walk Central Directory With Offset Math
Replace the existing loop header and early record reads:

```cpp
const uint8_t* cd = base + cdOffset;
for (int e = 0; e < numEntries && cd + 46 <= end; ++e) {
    uint32_t cdSig;
    std::memcpy(&cdSig, cd, 4);
    if (cdSig != 0x02014B50) break;
    ...
    std::string name(reinterpret_cast<const char*>(cd + 46), fnLen);
    cd += 46 + fnLen + extraLen + commentLen;
```

with:

```cpp
for (uint16_t e = 0; e < numEntries; ++e) {
    if (!HasRange(zipSize, cdOff, 46)) {
        break;
    }

    const uint8_t* cd = base + cdOff;
    if (ReadU32(cd) != 0x02014B50) {
        break;
    }

    const uint16_t compression = ReadU16(cd + 10);
    const uint32_t compSize32 = ReadU32(cd + 20);
    const uint32_t uncompSize32 = ReadU32(cd + 24);
    const uint16_t fnLen = ReadU16(cd + 28);
    const uint16_t extraLen = ReadU16(cd + 30);
    const uint16_t commentLen = ReadU16(cd + 32);
    const uint32_t localOffset32 = ReadU32(cd + 42);

    const size_t variableLen =
        static_cast<size_t>(fnLen) +
        static_cast<size_t>(extraLen) +
        static_cast<size_t>(commentLen);

    if (!HasRange(zipSize, cdOff + 46, variableLen)) {
        break;
    }

    std::string name(
        reinterpret_cast<const char*>(base + cdOff + 46),
        fnLen);

    cdOff += 46 + variableLen;
```

Important: if your compiler or analyzer complains about `cdOff + 46`, split it into a checked `nameOffset` first:

```cpp
const size_t nameOffset = cdOff + 46; // safe because HasRange(zipSize, cdOff, 46) passed
if (!HasRange(zipSize, nameOffset, variableLen)) break;
```

## Step 5 — Make Entry Index Parsing Non-Throwing
Current code:

```cpp
int idx = std::stoi(numStr);
```

Replace with:

```cpp
int idx = -1;
try {
    const unsigned long parsed = std::stoul(numStr);
    if (parsed > static_cast<unsigned long>(std::numeric_limits<int>::max())) {
        continue;
    }
    idx = static_cast<int>(parsed);
} catch (...) {
    continue;
}
```

This keeps a malformed file name from aborting the whole model constructor through an exception that hides the real parser error.

Add `#include <limits>` if it is not already present.

## Step 6 — Validate Local Header and Raw Data Range Before Pointers
Replace:

```cpp
if (localOffset + 30 > zipSize) continue;
const uint8_t* local = base + localOffset;
...
const uint8_t* rawData = local + 30 + localFnLen + localExLen;

if (rawData + compSize > end) continue;
```

with:

```cpp
const size_t localOffset = static_cast<size_t>(localOffset32);
if (!HasRange(zipSize, localOffset, 30)) {
    continue;
}

const uint8_t* local = base + localOffset;
if (ReadU32(local) != 0x04034B50) {
    continue;
}

const uint16_t localFnLen = ReadU16(local + 26);
const uint16_t localExLen = ReadU16(local + 28);

const size_t localVariableLen =
    static_cast<size_t>(localFnLen) + static_cast<size_t>(localExLen);
const size_t rawOffset = localOffset + 30 + localVariableLen;

if (!HasRange(zipSize, localOffset + 30, localVariableLen)) {
    continue;
}

const size_t compSize = static_cast<size_t>(compSize32);
const size_t uncompSize = static_cast<size_t>(uncompSize32);

if (!HasRange(zipSize, rawOffset, compSize)) {
    continue;
}

const uint8_t* rawData = base + rawOffset;
```

The `rawOffset` expression is safe because:
- `HasRange(zipSize, localOffset, 30)` proved `localOffset + 30` is inside `zipSize`.
- `HasRange(zipSize, localOffset + 30, localVariableLen)` proves the variable header area is inside `zipSize`.

If you want to be maximally strict, compute the offsets in two steps after each `HasRange` check to make the proof obvious:

```cpp
const size_t localDataStart = localOffset + 30;
if (!HasRange(zipSize, localDataStart, localVariableLen)) continue;
const size_t rawOffset = localDataStart + localVariableLen;
```

## Step 7 — Combine With Compression Handling
After the safe `rawData`, `compSize`, and `uncompSize` variables exist, apply **P0/01**:

```cpp
if (compression == 0) {
    if (compSize != uncompSize) continue;
    out[idx].assign(rawData, rawData + compSize);
} else if (compression == 8) {
    // raw DEFLATE via miniz tinfl_decompress_mem_to_mem(..., flags=0)
} else {
    continue;
}
```

At this point `rawData + compSize` is safe because `HasRange(zipSize, rawOffset, compSize)` already proved the range.

## Verification
1. Build `RLInference` and `GoySDKCore`.

2. Add a small parser-focused test if possible. The parser is currently in an anonymous namespace, so a clean test may require either:
   - moving the parser into a testable internal function, or
   - temporarily adding a debug-only test harness in `Bot.cpp`.

3. Test cases:
   - Empty buffer: returns false, no crash.
   - 1 MB all-zero buffer: returns false quickly, no crash.
   - Valid EOCD with `cdOffset` beyond EOF: returns false.
   - Valid central-directory record with `localOffset = 0xFFFFFFFE`: entry skipped, no crash.
   - Local header with `localFnLen = 0xFFFF` and insufficient file size: entry skipped, no crash.
   - Entry with `compSize = 0xFFFFFFFF`: entry skipped, no crash.
   - Valid STORED model: still loads.
   - Valid DEFLATE model after P0/01: loads.

## Don't Do
- Do not fix this with `(uint64_t)localOffset + 30 > zipSize` everywhere. It is better than the current code but still leaves pointer-range reasoning scattered and easy to regress.
- Do not compute `rawData` before proving the local header variable-length range is inside the buffer.
- Do not keep the pointer-based EOCD loop. It is easy to read past `base` and is unnecessary.
- Do not silently catch every parser problem at the constructor level and call that enough. The parser itself must not perform invalid memory access.

## Related
- **P0/01** — DEFLATE support must be layered on top of these safe ranges.
- **P0/02** — parser failures must surface as load failures, not stub success.
