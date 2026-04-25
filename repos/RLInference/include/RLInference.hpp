#pragma once

#include <ActionOutput.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>
#include <memory>

namespace RLInference {

enum class BotType {
    GigaLearn = 0,
    PpoPolicy = 1
};

struct InferenceDebugInfo {
    bool available = false;
    std::vector<float> logits{};
    int action_index = -1;
};

struct BotConfig {
    BotType bot_type = BotType::GigaLearn;
    std::string primary_model_path{};
    std::string secondary_model_path{};
    std::vector<int> primary_model_sizes{};
    int tick_skip = 1;
    bool deterministic = false;
    int expected_obs_count = 0;
    std::vector<int> secondary_model_sizes{};
    bool use_leaky_relu = false;

    const void* primary_model_data = nullptr;
    std::size_t primary_model_size = 0;
    const void* secondary_model_data = nullptr;
    std::size_t secondary_model_size = 0;

    /// Host-provided discrete action table. `discrete_actions[i]` is the controller
    /// output corresponding to policy logit index `i`. This must match the policy's
    /// training-time action ordering and the final policy output dimension.
    std::vector<ActionOutput> discrete_actions{};

    BotConfig() = default;

    BotConfig(BotType type,
              std::string primaryPath,
              std::string secondaryPath,
              std::vector<int> primarySizes,
              int tickSkip,
              bool isDeterministic,
              int expectedObsCount,
              std::vector<int> secondarySizes,
              bool useLeakyRelu)
        : bot_type(type),
          primary_model_path(std::move(primaryPath)),
          secondary_model_path(std::move(secondaryPath)),
          primary_model_sizes(std::move(primarySizes)),
          tick_skip(tickSkip),
          deterministic(isDeterministic),
          expected_obs_count(expectedObsCount),
          secondary_model_sizes(std::move(secondarySizes)),
          use_leaky_relu(useLeakyRelu) {}
};

class Bot {
public:
    Bot(const BotConfig& cfg, void* reserved);
    ~Bot();

    Bot(const Bot&) = delete;
    Bot& operator=(const Bot&) = delete;

    bool is_initialized() const;

    std::vector<float>& obs() { return obs_; }
    const std::vector<float>& obs() const { return obs_; }

    void push_obs(float v) { obs_.push_back(v); }

    bool forward();

    /// Runs inference after applying a per-call discrete action mask.
    ///
    /// `actionMask.size()` must equal the policy output dimension. Entries with
    /// value 0 are forbidden and get their logits set to -infinity before argmax
    /// or sampling. Returns false on shape mismatch or fully-masked input.
    bool forward(const std::vector<uint8_t>& actionMask);

    ActionOutput get_last_output() const { return last_output_; }
    InferenceDebugInfo get_last_debug() const { return last_debug_; }

private:
    bool forward_impl(const std::vector<uint8_t>* actionMask);
    void ResetLastInferenceState();

    BotConfig config_{};
    std::vector<float> obs_{};
    ActionOutput last_output_{};
    InferenceDebugInfo last_debug_{};

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace RLInference
