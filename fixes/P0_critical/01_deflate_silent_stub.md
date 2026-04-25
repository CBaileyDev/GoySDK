# P0 / 01 — DEFLATE'd model files load as a silent no-op bot

## TL;DR
The TorchScript ZIP reader in `repos/RLInference/src/Bot.cpp` only accepts ZIP entries whose compression method is `0` (`STORED`). Normal Torch/PyTorch archives may contain `archive/data/<n>` entries compressed with method `8` (`DEFLATE`). The current parser silently skips those entries, `LoadMLP` fails, `Bot` enters stub mode, and because `Bot::is_initialized()` currently lies (see **P0/02**) the host reports the model as loaded while the car outputs neutral controls forever.

This fix must make the ZIP reader support raw ZIP DEFLATE entries, make unsupported compression fail visibly through the normal load-failure path, and keep all offset validation overflow-safe. Apply **P1/06** in the same editing pass because the same code is being touched.

## Where
- ZIP parser: `/Users/carterbarker/Documents/GoySDK/repos/RLInference/src/Bot.cpp`, anonymous `ExtractDataEntries`, current lines around 23-94.
- Compression skip: `if (compression != 0) continue;`.
- Stub-mode visibility: `/Users/carterbarker/Documents/GoySDK/repos/RLInference/src/Bot.cpp`, `Bot::is_initialized()` current lines around 241-244.

## Current Code Shape
The parser:
1. Finds the End of Central Directory.
2. Walks central-directory records.
3. Selects entries named `archive/data/<number>`.
4. Jumps to the matching local file header.
5. Copies the raw file bytes only when `compression == 0`.

The relevant current block is:

```cpp
// Use the size from the Central Directory (always correct).
if (rawData + compSize > end) continue;
if (compression != 0) continue;   // only STORED

out[idx].assign(rawData, rawData + compSize);
```

That last line is valid for stored entries only. For DEFLATE entries, `rawData` points at compressed bytes and must be inflated into `uncompSize` bytes before the tensor byte vector is stored.

## Correct Fix Strategy
Use a small, vendored DEFLATE implementation and call the raw-DEFLATE API. Do **not** use a zlib-wrapper API such as `uncompress()` or `mz_uncompress()` for ZIP entry payloads. ZIP method 8 stores raw DEFLATE streams without a zlib header.

Recommended implementation: vendor `miniz` and use `tinfl_decompress_mem_to_mem(...)` with flags `0`.

## Step 1 — Vendor `miniz`
Add the single-source miniz release files:

```text
/Users/carterbarker/Documents/GoySDK/repos/RLInference/third_party/miniz/miniz.h
/Users/carterbarker/Documents/GoySDK/repos/RLInference/third_party/miniz/miniz.c
```

Use an upstream miniz v3.x release. Keep the upstream license header intact.

## Step 2 — Wire `miniz` into RLInference
Edit `/Users/carterbarker/Documents/GoySDK/repos/RLInference/CMakeLists.txt`.

Current file has:

```cmake
file(GLOB_RECURSE RLINFERENCE_SOURCES "src/*.cpp")

add_library(RLInference STATIC ${RLINFERENCE_SOURCES})

target_include_directories(RLInference PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
```

Replace or extend it so `miniz.c` is compiled as C/C++ compatible source and the include directory is private:

```cmake
file(GLOB_RECURSE RLINFERENCE_SOURCES CONFIGURE_DEPENDS "src/*.cpp")

add_library(RLInference STATIC
    ${RLINFERENCE_SOURCES}
    third_party/miniz/miniz.c
)

target_include_directories(RLInference
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniz
)
```

If your toolchain compiles `.c` files as C and warns on Torch/C++ flags, it is also acceptable to rename `miniz.c` to `miniz.cpp` after verifying the upstream file supports C++ compilation. Prefer the direct `.c` path first.

## Step 3 — Add Includes
Edit `/Users/carterbarker/Documents/GoySDK/repos/RLInference/src/Bot.cpp`.

Add these includes near the existing standard-library includes:

```cpp
#include <limits>
#include <stdexcept>
```

Add the miniz include after the Torch/RLInference includes:

```cpp
#include "miniz.h"
```

`<limits>` is useful for later mask work and defensive values. `<stdexcept>` is useful if you decide to convert malformed critical structures into visible load failure instead of silently skipping them.

## Step 4 — Replace Stored-Only Copy With Stored/DEFLATE Handling
After applying the overflow-safe checks from **P1/06**, replace the compression block with this exact shape:

```cpp
std::vector<uint8_t> bytes;

if (compression == 0) {
    // STORED: central-directory compressed and uncompressed sizes must agree.
    if (compSize != uncompSize) {
        continue;
    }
    bytes.assign(rawData, rawData + compSize);
} else if (compression == 8) {
    // ZIP method 8 uses raw DEFLATE, not zlib-wrapped DEFLATE.
    bytes.resize(uncompSize);
    const size_t outBytes = tinfl_decompress_mem_to_mem(
        bytes.data(),
        bytes.size(),
        rawData,
        compSize,
        0);

    if (outBytes == TINFL_DECOMPRESS_MEM_TO_MEM_FAILED ||
        outBytes != static_cast<size_t>(uncompSize)) {
        continue;
    }
} else {
    // Unsupported ZIP compression method. Skip the entry. If required model
    // tensors are skipped, LoadMLP returns false and P0/02 surfaces the failure.
    continue;
}

out[idx] = std::move(bytes);
```

Do not use this incorrect pattern:

```cpp
mz_uncompress(...);
```

`mz_uncompress` expects zlib-wrapped data. ZIP file payloads for method 8 are raw DEFLATE streams.

## Step 5 — Keep Failure Visible
This fix is only complete when **P0/02** is also applied:

```cpp
bool Bot::is_initialized() const {
    return impl_ != nullptr && impl_->ready;
}
```

Without P0/02, malformed, unsupported, or failed DEFLATE loads can still become invisible stub-mode bots.

Do not add a success log from inside `RLInference`. The host already has the right call-site error path at `BotModule::LoadBotForSlot` once `is_initialized()` is honest.

## Step 6 — Expected Behavior After Fix
- STORED Torch archives still load.
- DEFLATE Torch archives load.
- Unsupported methods such as BZIP2 do not crash and do not report as loaded.
- Corrupt DEFLATE streams do not crash and do not report as loaded.
- All failure modes produce the existing host-side error once P0/02 is applied.

## Verification
1. Build RLInference:
   ```bash
   cmake --build /Users/carterbarker/Documents/GoySDK/repos/RLInference/build
   ```
   If this project does not already have a configured build directory, configure it with the same Torch environment used by `internal_bot`.

2. Build the DLL project:
   ```bash
   cmake --build /Users/carterbarker/Documents/GoySDK/internal_bot/build
   ```

3. Positive model test:
   - Produce or locate a TorchScript ZIP where `archive/data/<n>` entries are DEFLATE-compressed.
   - Embed it or temporarily load it through the resource path.
   - Confirm `Bot::is_initialized()` returns true and `[GoySDK] Slot 0: Loaded ...` is printed.

4. STORED regression:
   - Test an existing known-good model from the current resources.
   - Confirm behavior is unchanged.

5. Negative corrupt-DEFLATE test:
   - Flip one byte inside a compressed `archive/data/<n>` entry.
   - Confirm the bot does not load and the host prints the model initialization failure.

6. Unsupported-compression test:
   - Create a ZIP entry with method 12 or another unsupported method.
   - Confirm there is no crash and load fails visibly.

## Don't Do
- Do not switch to `torch::jit::load`. This code intentionally extracts raw MLP tensors from embedded archives without loading the full TorchScript module.
- Do not use `mz_uncompress`, `uncompress`, or any API that requires a zlib header.
- Do not leave `Bot::is_initialized()` returning `impl_ != nullptr`.
- Do not “fix” by requiring all model exports to be STORED. The loader should accept normal Torch ZIPs.

## Related
- **P0/02** — required so load failures are no longer hidden.
- **P1/06** — same parser needs overflow-safe bounds checks before decompression is safe.
