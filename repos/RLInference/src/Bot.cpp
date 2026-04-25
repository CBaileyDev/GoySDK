// RLInference Bot implementation.
//
// Strategy: load model weights from embedded TorchScript ZIP files.
// If loading fails for ANY reason, we fall back to a stub that returns
// neutral controls.  This guarantees the game never crashes.

#include <RLInference.hpp>
#include <torch/torch.h>
#include "miniz_tinfl.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

// ---- Minimal ZIP Central-Directory parser --------------------------------
// TorchScript .lt files are ZIP archives.  Local headers have data-descriptor
// flags (bit 3), so sizes are zero there.  We read sizes from the Central
// Directory at the end of the file instead.
namespace {

bool HasRange(size_t total, size_t offset, size_t length) {
    return offset <= total && length <= total - offset;
}

uint16_t ReadU16(const uint8_t* p) {
    uint16_t v = 0;
    std::memcpy(&v, p, sizeof(v));
    return v;
}

uint32_t ReadU32(const uint8_t* p) {
    uint32_t v = 0;
    std::memcpy(&v, p, sizeof(v));
    return v;
}

bool ExtractDataEntries(const void* zipData, size_t zipSize,
                        std::unordered_map<int, std::vector<uint8_t>>& out)
{
    const uint8_t* base = static_cast<const uint8_t*>(zipData);

    constexpr size_t kEocdMinSize = 22;
    constexpr size_t kMaxZipComment = 0xFFFF;
    constexpr size_t kMaxEocdSearch = kEocdMinSize + kMaxZipComment;
    if (!base || zipSize < kEocdMinSize) return false;

    // 1) Find EOCD (signature 0x06054B50) by scanning backwards.
    const size_t searchStart = zipSize - kEocdMinSize;
    const size_t searchFloor =
        (zipSize > kMaxEocdSearch) ? (zipSize - kMaxEocdSearch) : 0;

    size_t eocdOffset = static_cast<size_t>(-1);
    for (size_t off = searchStart;; --off) {
        if (ReadU32(base + off) == 0x06054B50) {
            eocdOffset = off;
            break;
        }
        if (off == searchFloor) break;
    }
    if (eocdOffset == static_cast<size_t>(-1)) return false;

    const uint8_t* eocd = base + eocdOffset;
    const uint16_t numEntries = ReadU16(eocd + 10);
    const size_t cdSize = static_cast<size_t>(ReadU32(eocd + 12));
    const size_t cdStart = static_cast<size_t>(ReadU32(eocd + 16));
    if (numEntries == 0 || cdSize == 0) return false;
    if (!HasRange(zipSize, cdStart, cdSize)) return false;

    // 2) Walk Central Directory entries (sig 0x02014B50).
    size_t cdOff = cdStart;
    const size_t cdEnd = cdStart + cdSize;
    for (uint16_t e = 0; e < numEntries; ++e) {
        if (cdOff > cdEnd || !HasRange(cdEnd, cdOff, 46)) return false;

        const uint8_t* cd = base + cdOff;
        if (ReadU32(cd) != 0x02014B50) return false;

        const uint16_t compression = ReadU16(cd + 10);
        const uint32_t compSize32 = ReadU32(cd + 20);
        const uint32_t uncompSize32 = ReadU32(cd + 24);
        const uint16_t fnLen = ReadU16(cd + 28);
        const uint16_t extraLen = ReadU16(cd + 30);
        const uint16_t commentLen = ReadU16(cd + 32);
        const uint32_t localOffset32 = ReadU32(cd + 42);

        const size_t nameOffset = cdOff + 46;
        const size_t variableLen = static_cast<size_t>(fnLen) +
                                   static_cast<size_t>(extraLen) +
                                   static_cast<size_t>(commentLen);
        if (!HasRange(cdEnd, nameOffset, variableLen)) return false;

        std::string name(reinterpret_cast<const char*>(base + nameOffset), fnLen);
        cdOff = nameOffset + variableLen;

        // We only care about "archive/data/<number>"
        if (name.size() <= 13 || name.compare(0, 13, "archive/data/") != 0)
            continue;
        std::string numStr = name.substr(13);
        bool isNum = !numStr.empty();
        for (char c : numStr) if (c < '0' || c > '9') { isNum = false; break; }
        if (!isNum) continue;

        int idx = -1;
        try {
            const unsigned long parsed = std::stoul(numStr);
            if (parsed > static_cast<unsigned long>(std::numeric_limits<int>::max()))
                return false;
            idx = static_cast<int>(parsed);
        } catch (...) {
            return false;
        }

        // Jump to the local file header to find the actual data bytes.
        const size_t localOffset = static_cast<size_t>(localOffset32);
        if (!HasRange(zipSize, localOffset, 30)) return false;
        const uint8_t* local = base + localOffset;
        if (ReadU32(local) != 0x04034B50) return false;

        const uint16_t localFnLen = ReadU16(local + 26);
        const uint16_t localExLen = ReadU16(local + 28);
        const size_t localDataStart = localOffset + 30;
        const size_t localVariableLen =
            static_cast<size_t>(localFnLen) + static_cast<size_t>(localExLen);
        if (!HasRange(zipSize, localDataStart, localVariableLen)) return false;

        // Use the size from the Central Directory (always correct).
        const size_t rawOffset = localDataStart + localVariableLen;
        const size_t compSize = static_cast<size_t>(compSize32);
        const size_t uncompSize = static_cast<size_t>(uncompSize32);
        if (!HasRange(zipSize, rawOffset, compSize)) return false;
        const uint8_t* rawData = base + rawOffset;

        std::vector<uint8_t> bytes;
        if (compression == 0) {
            if (compSize != uncompSize) return false;
            bytes.assign(rawData, rawData + compSize);
        } else if (compression == 8) {
            if (uncompSize > bytes.max_size()) return false;
            bytes.resize(uncompSize);
            const size_t outBytes = tinfl_decompress_mem_to_mem(
                bytes.data(), bytes.size(), rawData, compSize, 0);
            if (outBytes == TINFL_DECOMPRESS_MEM_TO_MEM_FAILED ||
                outBytes != uncompSize) {
                return false;
            }
        } else {
            return false;
        }

        out[idx] = std::move(bytes);
    }
    return !out.empty();
}

} // anon

// ---- RLInference ---------------------------------------------------------
namespace RLInference {

// The discrete action table is host-owned and injected via BotConfig::discrete_actions.
// See P0/06: keeping a second copy here would silently desync from GoySDK::ActionMask.hpp.

// ------ Impl: holds MLP weights and runs inference -----------------------
struct Bot::Impl {
    struct Layer {
        torch::Tensor w, b;          // Linear
        torch::Tensor nw, nb;        // LayerNorm (may be empty)
        bool hasNorm = false;
    };

    std::vector<Layer> shared, policy;
    bool useLeaky = false;
    bool ready = false;               // true only after successful load
    std::mt19937 rng{std::random_device{}()};

    // ---- load weights from a ZIP blob -----------------------------------
    bool LoadMLP(const void* data, size_t size,
                 std::vector<Layer>& layers, int inSize) {
        std::unordered_map<int, std::vector<uint8_t>> entries;
        if (!ExtractDataEntries(data, size, entries))
            return false;

        int di = 0;
        int curIn = inSize;
        while (entries.count(di) && entries.count(di+1)) {
            Layer L;
            auto& wB = entries[di];
            auto& bB = entries[di+1];
            int outSz = static_cast<int>(bB.size() / 4);
            if (outSz <= 0) break;
            int expW = outSz * curIn;
            if (static_cast<int>(wB.size()/4) != expW) break;

            auto mkT = [](const std::vector<uint8_t>& v, std::vector<int64_t> sh) {
                return torch::from_blob(
                    const_cast<uint8_t*>(v.data()), sh,
                    torch::kFloat32).clone();
            };
            L.w = mkT(wB, {outSz, curIn});
            L.b = mkT(bB, {outSz});
            di += 2;

            // optional LayerNorm
            if (entries.count(di) && entries.count(di+1)) {
                int n1 = static_cast<int>(entries[di].size()/4);
                int n2 = static_cast<int>(entries[di+1].size()/4);
                if (n1 == outSz && n2 == outSz) {
                    L.nw = mkT(entries[di],   {outSz});
                    L.nb = mkT(entries[di+1], {outSz});
                    L.hasNorm = true;
                    di += 2;
                }
            }
            layers.push_back(std::move(L));
            curIn = outSz;
        }
        return !layers.empty();
    }

    // ---- MLP forward pass -----------------------------------------------
    torch::Tensor RunMLP(const torch::Tensor& x0,
                         const std::vector<Layer>& layers) {
        torch::Tensor x = x0;
        for (size_t i = 0; i < layers.size(); i++) {
            x = torch::linear(x, layers[i].w, layers[i].b);
            if (layers[i].hasNorm)
                x = torch::layer_norm(x, {layers[i].nw.size(0)},
                                       layers[i].nw, layers[i].nb);
            if (i + 1 < layers.size())  // activation on all but last
                x = useLeaky ? torch::leaky_relu(x, 0.01) : torch::relu(x);
        }
        return x;
    }

    int Sample(const torch::Tensor& logits) {
        auto p = torch::softmax(logits.squeeze(), 0);
        auto c = torch::cumsum(p, 0);
        float u = std::uniform_real_distribution<float>(0.f, 1.f)(rng);
        auto idx = torch::where(c >= u)[0];
        return idx.numel() > 0
            ? static_cast<int>(idx[0].item<int64_t>())
            : static_cast<int>(p.size(0) - 1);
    }
};

// ------ Bot public API ---------------------------------------------------
Bot::Bot(const BotConfig& cfg, void* /*reserved*/)
    : config_(cfg), impl_(std::make_unique<Impl>())
{
    try {
        impl_->useLeaky = cfg.use_leaky_relu;
        bool ok1 = impl_->LoadMLP(cfg.primary_model_data,
                                   cfg.primary_model_size,
                                   impl_->shared,
                                   cfg.expected_obs_count);
        if (!ok1) return;   // stay ready=false → stub mode

        int policyIn = static_cast<int>(
            impl_->shared.back().b.size(0));
        bool ok2 = impl_->LoadMLP(cfg.secondary_model_data,
                                   cfg.secondary_model_size,
                                   impl_->policy,
                                   policyIn);
        if (!ok2) { impl_->shared.clear(); return; }

        impl_->ready = true;
    } catch (...) {
        // If anything throws, stay in stub mode.
        impl_->shared.clear();
        impl_->policy.clear();
        impl_->ready = false;
    }
}

Bot::~Bot() = default;

bool Bot::is_initialized() const {
    // True only after model weights load successfully. Constructed-but-stub
    // bots must report false so callers can surface load failures.
    return impl_ != nullptr && impl_->ready;
}

bool Bot::forward() {
    return forward_impl(nullptr);
}

bool Bot::forward(const std::vector<uint8_t>& actionMask) {
    return forward_impl(&actionMask);
}

bool Bot::forward_impl(const std::vector<uint8_t>* actionMask) {
    if (!impl_ || obs_.empty()) return false;

    // ---- Stub mode: model didn't load → neutral output ------------------
    if (!impl_->ready) {
        last_output_ = {};
        last_debug_.available = false;
        last_debug_.action_index = -1;
        last_debug_.logits.clear();
        return true;
    }

    // ---- Real inference -------------------------------------------------
    try {
        torch::NoGradGuard ng;
        auto inp = torch::from_blob(
            obs_.data(), {1, static_cast<int64_t>(obs_.size())},
            torch::kFloat32).clone();

        auto h = impl_->RunMLP(inp, impl_->shared);
        // Activation between shared head output and policy head input
        h = impl_->useLeaky ? torch::leaky_relu(h, 0.01) : torch::relu(h);
        auto logits = impl_->RunMLP(h, impl_->policy);

        auto flat = logits.squeeze().contiguous();
        const int nAct = static_cast<int>(flat.numel());

        // Validate the host-injected discrete-action table dimension first.
        // A model/table mismatch is a configuration bug and must be visible.
        const auto& acts = config_.discrete_actions;
        if (static_cast<int>(acts.size()) != nAct) {
            last_output_ = {};
            last_debug_.available = false;
            last_debug_.action_index = -1;
            last_debug_.logits.clear();
            return false;
        }

        // Then validate the mask shape against the policy output dim.
        if (actionMask && static_cast<int>(actionMask->size()) != nAct) {
            last_output_ = {};
            last_debug_.available = false;
            last_debug_.action_index = -1;
            last_debug_.logits.clear();
            return false;
        }

        // Apply the mask BEFORE argmax/sampling so we sample from the
        // softmax-over-allowed-actions distribution the policy was trained on.
        if (actionMask) {
            auto* logitsData = flat.data_ptr<float>();
            bool anyAllowed = false;
            for (int i = 0; i < nAct; ++i) {
                if ((*actionMask)[static_cast<size_t>(i)] == 0) {
                    logitsData[i] = -std::numeric_limits<float>::infinity();
                } else {
                    anyAllowed = true;
                }
            }
            if (!anyAllowed) {
                last_output_ = {};
                last_debug_.available = false;
                last_debug_.action_index = -1;
                last_debug_.logits.clear();
                return false;
            }
        }

        last_debug_.available = true;
        last_debug_.logits.resize(nAct);
        std::memcpy(last_debug_.logits.data(), flat.data_ptr<float>(),
                    static_cast<size_t>(nAct) * sizeof(float));

        const int ai = config_.deterministic
            ? static_cast<int>(flat.argmax().item<int64_t>())
            : impl_->Sample(flat);
        last_debug_.action_index = ai;

        last_output_ = (ai >= 0 && ai < static_cast<int>(acts.size()))
            ? acts[static_cast<size_t>(ai)]
            : ActionOutput{};
        return true;
    } catch (...) {
        // On any error during inference, return neutral.
        last_output_ = {};
        last_debug_.available = false;
        last_debug_.action_index = -1;
        last_debug_.logits.clear();
        return true;
    }
}

} // namespace RLInference
