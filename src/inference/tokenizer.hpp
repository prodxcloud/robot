#pragma once

/// @file tokenizer.hpp
/// @brief BPE tokenizer for encoding/decoding text.

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include "common/types.hpp"

namespace prodxcloud::inference {

class BPETokenizer {
public:
    static constexpr int32_t BOS_TOKEN = 1;
    static constexpr int32_t EOS_TOKEN = 2;
    static constexpr int32_t PAD_TOKEN = 0;
    static constexpr int32_t UNK_TOKEN = 3;

    BPETokenizer() = default;
    Result<void> load_vocab(const std::string& path);
    std::vector<int32_t> encode(const std::string& text, bool add_bos = true, bool add_eos = false) const;
    std::string decode(const std::vector<int32_t>& tokens) const;
    [[nodiscard]] size_t vocab_size() const { return token_to_id_.size(); }
    [[nodiscard]] bool is_loaded() const { return !token_to_id_.empty(); }

private:
    std::unordered_map<std::string, int32_t> token_to_id_;
    std::unordered_map<int32_t, std::string> id_to_token_;
    std::vector<std::pair<std::string, std::string>> merge_rules_;
};

}  // namespace prodxcloud::inference
