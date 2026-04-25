// RLInference Bot implementation.
//
// Strategy: load model weights from embedded TorchScript ZIP files.
// If loading fails for ANY reason, we fall back to a stub that returns
// neutral controls.  This guarantees the game never crashes.

#include <RLInference.hpp>
#include <torch/torch.h>

#include <random>
#include <algorithm>
#include <cstring>
#include <vector>
#include <string>
#include <unordered_map>

// ---- Minimal ZIP Central-Directory parser --------------------------------
// TorchScript .lt files are ZIP archives.  Local headers have data-descriptor
// flags (bit 3), so sizes are zero there.  We read sizes from the Central
// Directory at the end of the file instead.
namespace {

bool ExtractDataEntries(const void* zipData, size_t zipSize,
                        std::unordered_map<int, std::vector<uint8_t>>& out)
{
    const uint8_t* base = static_cast<const uint8_t*>(zipData);
    const uint8_t* end  = base + zipSize;
    if (zipSize < 22) return false;

    // 1) Find EOCD (signature 0x06054B50) by scanning backwards.
    const uint8_t* eocd = nullptr;
    for (const uint8_t* s = end - 22; s >= base; --s) {
        uint32_t sig;
        std::memcpy(&sig, s, 4);
        if (sig == 0x06054B50) { eocd = s; break; }
    }
    if (!eocd) return false;

    uint16_t numEntries;
    uint32_t cdOffset;
    std::memcpy(&numEntries, eocd + 10, 2);
    std::memcpy(&cdOffset,   eocd + 16, 4);
    if (cdOffset >= zipSize) return false;

    // 2) Walk Central Directory entries (sig 0x02014B50).
    const uint8_t* cd = base + cdOffset;
    for (int e = 0; e < numEntries && cd + 46 <= end; ++e) {
        uint32_t cdSig;
        std::memcpy(&cdSig, cd, 4);
        if (cdSig != 0x02014B50) break;

        uint16_t compression, fnLen, extraLen, commentLen;
        uint32_t compSize, uncompSize, localOffset;
        std::memcpy(&compression, cd + 10, 2);
        std::memcpy(&compSize,    cd + 20, 4);
        std::memcpy(&uncompSize,  cd + 24, 4);
        std::memcpy(&fnLen,       cd + 28, 2);
        std::memcpy(&extraLen,    cd + 30, 2);
        std::memcpy(&commentLen,  cd + 32, 2);
        std::memcpy(&localOffset, cd + 42, 4);

        std::string name(reinterpret_cast<const char*>(cd + 46), fnLen);
        cd += 46 + fnLen + extraLen + commentLen;   // advance to next CD entry

        // We only care about "archive/data/<number>"
        if (name.size() <= 13 || name.compare(0, 13, "archive/data/") != 0)
            continue;
        std::string numStr = name.substr(13);
        bool isNum = !numStr.empty();
        for (char c : numStr) if (c < '0' || c > '9') { isNum = false; break; }
        if (!isNum) continue;

        int idx = std::stoi(numStr);

        // Jump to the local file header to find the actual data bytes.
        if (localOffset + 30 > zipSize) continue;
        const uint8_t* local = base + localOffset;
        uint32_t localSig;
        std::memcpy(&localSig, local, 4);
        if (localSig != 0x04034B50) continue;

        uint16_t localFnLen, localExLen;
        std::memcpy(&localFnLen, local + 26, 2);
        std::memcpy(&localExLen, local + 28, 2);
        const uint8_t* rawData = local + 30 + localFnLen + localExLen;

        // Use the size from the Central Directory (always correct).
        if (rawData + compSize > end) continue;
        if (compression != 0) continue;   // only STORED

        out[idx].assign(rawData, rawData + compSize);
    }
    return !out.empty();
}

} // anon

// ---- RLInference ---------------------------------------------------------
namespace RLInference {

// Discrete action table (mirrors GoySDK::ActionMask.hpp exactly).
static const std::vector<ActionOutput>& GetDiscreteActions() {
    static const auto actions = [] {
        std::vector<ActionOutput> built;
        constexpr float B[] = {0.f, 1.f};
        constexpr float T[] = {-1.f, 0.f, 1.f};

        // Ground actions
        for (float th : T) for (float st : T) for (float bo : B) for (float hb : B) {
            if (bo == 1.f && th != 1.f) continue;
            built.push_back({th, st, 0.f, st, 0.f, false, bo==1.f, hb==1.f});
        }
        // Air actions
        for (float pi : T) for (float ya : T) for (float ro : T)
          for (float ju : B) for (float bo : B) {
            if (ju == 1.f && ya != 0.f) continue;
            if (pi == ro && ro == ju && ju == 0.f) continue;
            bool jb = ju==1.f, bb = bo==1.f;
            bool hb = jb && (pi!=0.f || ya!=0.f || ro!=0.f);
            built.push_back({bo, ya, pi, ya, ro, jb, bb, hb});
        }
        return built;
    }();
    return actions;
}

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
    // Always return true — even if model loading failed we still work (stub).
    return impl_ != nullptr;
}

bool Bot::forward() {
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
            obs_.data(), {1, (int64_t)obs_.size()}, torch::kFloat32).clone();

        auto h = impl_->RunMLP(inp, impl_->shared);
        // Activation between shared head output and policy head input
        h = impl_->useLeaky ? torch::leaky_relu(h, 0.01) : torch::relu(h);
        auto logits = impl_->RunMLP(h, impl_->policy);

        auto flat = logits.squeeze().contiguous();
        int nAct  = static_cast<int>(flat.numel());

        last_debug_.available = true;
        last_debug_.logits.resize(nAct);
        std::memcpy(last_debug_.logits.data(), flat.data_ptr<float>(),
                    nAct * sizeof(float));

        int ai = config_.deterministic
            ? static_cast<int>(flat.argmax().item<int64_t>())
            : impl_->Sample(flat);
        last_debug_.action_index = ai;

        const auto& acts = GetDiscreteActions();
        last_output_ = (ai >= 0 && ai < (int)acts.size()) ? acts[ai] : ActionOutput{};
        return true;
    } catch (...) {
        // On any error during inference, return neutral.
        last_output_ = {};
        last_debug_.available = false;
        return true;
    }
}

} // namespace RLInference
