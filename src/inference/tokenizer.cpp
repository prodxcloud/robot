#include "inference/tokenizer.hpp"
#include <fstream>
#include <sstream>
#include <spdlog/spdlog.h>

namespace prodxcloud::inference {

Result<void> BPETokenizer::load_vocab(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return std::unexpected(Error::not_found("Vocab not found: " + path));
    token_to_id_.clear(); id_to_token_.clear();
    id_to_token_[PAD_TOKEN] = "<pad>"; id_to_token_[BOS_TOKEN] = "<s>";
    id_to_token_[EOS_TOKEN] = "</s>";  id_to_token_[UNK_TOKEN] = "<unk>";
    token_to_id_["<pad>"] = PAD_TOKEN; token_to_id_["<s>"] = BOS_TOKEN;
    token_to_id_["</s>"] = EOS_TOKEN;  token_to_id_["<unk>"] = UNK_TOKEN;
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        auto tab = line.find('\t');
        if (tab == std::string::npos) continue;
        std::string tok = line.substr(0, tab);
        int32_t id      = std::stoi(line.substr(tab + 1));
        token_to_id_[tok] = id; id_to_token_[id] = tok;
    }
    spdlog::info("Loaded vocab: {} tokens from {}", token_to_id_.size(), path);
    return {};
}

std::vector<int32_t> BPETokenizer::encode(const std::string& text, bool add_bos, bool add_eos) const {
    std::vector<int32_t> tokens;
    if (add_bos) tokens.push_back(BOS_TOKEN);
    if (token_to_id_.empty()) {
        for (unsigned char c : text) tokens.push_back(static_cast<int32_t>(c) + 256);
    } else {
        std::istringstream stream(text); std::string word;
        while (stream >> word) {
            auto it = token_to_id_.find(word);
            if (it != token_to_id_.end()) { tokens.push_back(it->second); }
            else { for (char c : word) {
                auto ci = token_to_id_.find(std::string(1, c));
                tokens.push_back(ci != token_to_id_.end() ? ci->second : UNK_TOKEN);
            }}
        }
    }
    if (add_eos) tokens.push_back(EOS_TOKEN);
    return tokens;
}

std::string BPETokenizer::decode(const std::vector<int32_t>& tokens) const {
    std::string result;
    for (int32_t tid : tokens) {
        if (tid == BOS_TOKEN || tid == EOS_TOKEN || tid == PAD_TOKEN) continue;
        auto it = id_to_token_.find(tid);
        if (it != id_to_token_.end()) { if (!result.empty()) result += ' '; result += it->second; }
        else if (tid >= 256 && tid < 512) result += static_cast<char>(tid - 256);
    }
    return result;
}

}  // namespace prodxcloud::inference
