# P0 / 01 — DEFLATE'd model files load as a silent no-op bot

## TL;DR
The ZIP parser used to extract TorchScript model weights only handles entries with `compression == 0` (STORED). Any DEFLATE'd entry is silently skipped, the layer count comes up short, but the bot is still presented to the user as "loaded" and emits neutral controller output forever.

## Where
- File: `/Users/carterbarker/Documents/GoySDK/repos/RLInference/src/Bot.cpp`
- Function: anonymous namespace `ExtractDataEntries`
- Approximate line: **89** (`if (compression != 0) continue;`)

## Problem
TorchScript `.pt` / `.lt` archives commonly use DEFLATE (`compression == 8`) for tensor blobs unless the export code was specifically configured for STORED. The current parser:
```cpp
if (compression != 0) continue;   // only STORED
out[idx].assign(rawData, rawData + compSize);
```
…drops the entry. `LoadMLP` then fails to find the next `(weights, bias)` pair and `LoadMLP` returns false. The constructor catches this, sets `impl_->ready = false`, and the bot enters "stub mode."

Because **P0/02** (`is_initialized` lies) returns true regardless, `BotModule::LoadBotForSlot` thinks the model loaded fine, and every call to `forward()` returns the early-out neutral output at `Bot.cpp:251` (`last_output_ = {};`).

## Why it matters
Users export a new model, drop it into the resources, ship it, and the bot does nothing — no error in the console, no log entry, just a stationary car. The `Console.Notify(... Loaded ...)` line at `BotModule.cpp:305` even prints success.

## Root cause
The author wrote a minimal STORED-only parser because their reference TorchScript files happened to be uncompressed. There is no validation step that would have caught this for any other model. Compounded by P0/02 hiding the failure.

## Fix

### Step 1 — Add zlib (or miniz) dependency

The project already links `LibTorch`, which bundles `libzstd` but **not** `zlib` as a public link target. Use **miniz** because it is a single-file, header-only ZIP/DEFLATE library that can be vendored into `repos/RLInference` with no build-system surgery.

Add a vendored `miniz.h` and `miniz.c` from <https://github.com/richgel999/miniz> (use the single-source release, latest v3.x). Place them at:
```
/Users/carterbarker/Documents/GoySDK/repos/RLInference/third_party/miniz/miniz.h
/Users/carterbarker/Documents/GoySDK/repos/RLInference/third_party/miniz/miniz.c
```

Then update `/Users/carterbarker/Documents/GoySDK/repos/RLInference/CMakeLists.txt` to add the source and include path:
```cmake
target_sources(RLInference PRIVATE third_party/miniz/miniz.c)
target_include_directories(RLInference PRIVATE third_party/miniz)
```
(Insert these next to existing `target_sources` / `target_include_directories` calls. If those calls don't exist, search the file for the `add_library(RLInference ...)` declaration and add the lines immediately after it.)

### Step 2 — Inflate DEFLATE entries in `ExtractDataEntries`

Edit `/Users/carterbarker/Documents/GoySDK/repos/RLInference/src/Bot.cpp`. Add at the top of the file, with the other includes:
```cpp
#include "miniz.h"
```

Find this exact block (around line 87–92):
```cpp
        // Use the size from the Central Directory (always correct).
        if (rawData + compSize > end) continue;
        if (compression != 0) continue;   // only STORED

        out[idx].assign(rawData, rawData + compSize);
    }
```

Replace it with:
```cpp
        // Use the size from the Central Directory (always correct).
        if (rawData + compSize > end) continue;

        if (compression == 0) {
            // STORED — copy raw bytes directly.
            out[idx].assign(rawData, rawData + compSize);
        } else if (compression == 8) {
            // DEFLATE — inflate via miniz into a fresh buffer of uncompSize bytes.
            std::vector<uint8_t> inflated(uncompSize);
            mz_ulong dstLen = uncompSize;
            int status = mz_uncompress2(
                inflated.data(), &dstLen,
                rawData, reinterpret_cast<mz_ulong*>(&compSize));
            if (status != MZ_OK || dstLen != uncompSize) continue;
            out[idx] = std::move(inflated);
        } else {
            // Unsupported compression method — skip but DO NOT silently mark success.
            continue;
        }
    }
```

> ⚠️ `mz_uncompress2` expects the compressed stream to include the zlib wrapper. ZIP entries are **raw** DEFLATE without the zlib header. Use `mz_inflate` directly via `tinfl_decompress_mem_to_mem` instead. Replace the `else if` block above with:
> ```cpp
> } else if (compression == 8) {
>     std::vector<uint8_t> inflated(uncompSize);
>     size_t outBytes = tinfl_decompress_mem_to_mem(
>         inflated.data(), uncompSize,
>         rawData, compSize,
>         TINFL_FLAG_PARSE_ZLIB_HEADER ? 0 : 0);  // raw DEFLATE — no zlib header flag
>     if (outBytes == TINFL_DECOMPRESS_MEM_TO_MEM_FAILED || outBytes != uncompSize) continue;
>     out[idx] = std::move(inflated);
> }
> ```
> The `TINFL_FLAG_PARSE_ZLIB_HEADER` flag must be **omitted** (i.e., flags=0) for raw DEFLATE inside ZIP entries.

### Step 3 — Add a load-time error log so this can never happen silently again

In `Bot::Bot` constructor, **after** P0/02 is applied (`is_initialized` returns honest value), the caller `BotModule::LoadBotForSlot` already logs an error path. So no additional change here is needed if P0/02 ships in the same release.

If P0/02 is **not** shipping yet, add this inside the `catch (...)` at `Bot.cpp:231-236`:
```cpp
} catch (...) {
    impl_->shared.clear();
    impl_->policy.clear();
    impl_->ready = false;
    // TODO: pipe an error message back to the host. For now, force a stderr write.
    std::fprintf(stderr, "RLInference: Bot construction threw, entering stub mode.\n");
}
```

## Verification

1. **Unit test** — build `repos/RLInference` standalone (it has its own `CMakeLists.txt`). Add a test that loads a known DEFLATE'd `.pt` file and asserts `bot.is_initialized()` and that `forward()` produces a non-zero `logits.size()`.
2. **Integration test** — re-export one of the existing models with `torch.jit.save(..., _use_new_zipfile_serialization=True)` and the default DEFLATE; replace the embedded resource; build the DLL; inject; verify the bot moves.
3. **Negative test** — produce a ZIP with `compression = 12` (BZIP2) and confirm the `continue` branch is hit and the bot enters stub mode (combined with P0/02 the user will see an error).

## Don't do

- Do not switch the parser to use LibTorch's `torch::jit::load`. That pulls in the full IR module loader and breaks the "minimal embedded MLP weights" design.
- Do not use `mz_uncompress` on raw DEFLATE — see the warning above.
- Do not silently assume STORED-only models exist. Old releases of `torch.save` default to ZIP_DEFLATED.

## Related
- **P0/02** — without that fix, this fix only papers over half the problem.
- **P1/06** — same parser, integer overflow on offsets. Fix in the same pass.
