// Byte-level BPE tokenizer (GPT-2), shared by the sequential, OpenMP and CUDA versions.
//
// Pipeline:  text --pretokenize--> words --encode_word--> token ids
//   * pretokenize : GPT-2 regex rules, hand-written (no regex library).
//   * encode_word : Sennrich et al. (2016) merge loop, applied to bytes instead of characters.
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// A word is a slice of the input text: [offset, offset + length) in bytes.
struct Span
{
    size_t offset;
    size_t length;
};

// Result of looking up an adjacent token pair in the merge table.
struct MergeInfo
{
    int32_t rank;   // priority: smaller = merged earlier
    int32_t result; // token id of the merged pair
};

class Tokenizer
{
public:
    // Load GPT-2 vocab.json and merges.txt. Throws std::runtime_error on failure.
    void load(const std::string &vocab_path, const std::string &merges_path);

    // Split text into words following the GPT-2 regex:
    //   's|'t|'re|'ve|'m|'ll|'d| ?\p{L}+| ?\p{N}+| ?[^\s\p{L}\p{N}]+|\s+(?!\S)|\s+
    static std::vector<Span> pretokenize(const std::string &text);

    // BPE-encode one word (raw bytes) and append its token ids to `out`.
    void encode_word(const char *data, size_t len, std::vector<int32_t> &out) const;

    // Same result as encode_word, but O(n log n): linked list + min-heap of candidate pairs.
    void encode_word_heap(const char *data, size_t len, std::vector<int32_t> &out) const;

    // Convenience: pretokenize + encode every word. This is the sequential baseline.
    std::vector<int32_t> encode(const std::string &text) const;

    size_t vocab_size() const { return vocab_size_; }
    size_t num_merges() const { return merges_.size(); }

private:
    static uint64_t pair_key(int32_t a, int32_t b)
    {
        return (uint64_t(uint32_t(a)) << 32) | uint32_t(b);
    }

    size_t vocab_size_ = 0;
    int32_t byte_to_id_[256] = {};                   // initial token id of each single byte
    std::unordered_map<uint64_t, MergeInfo> merges_; // (left id, right id) -> rank, merged id
};
