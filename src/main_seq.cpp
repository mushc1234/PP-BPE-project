// Sequential BPE baseline.
//
// Usage: bpe_seq <input.txt> [-o ids.bin] [--algo naive|heap] [--vocab path] [--merges path]
//   Encodes the whole file, prints timing, and optionally writes the token ids as raw int32.
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "bpe.hpp"

using Clock = std::chrono::steady_clock;
static double ms_since(Clock::time_point t0)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

int main(int argc, char **argv)
{
    std::string input, out_path, algo = "naive"; // algo: "naive" = O(n^2), "heap" = O(n log n)
    std::string vocab = "data/gpt2/vocab.json", merges = "data/gpt2/merges.txt";
    for (int i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "-o") && i + 1 < argc)
            out_path = argv[++i];
        else if (!strcmp(argv[i], "--vocab") && i + 1 < argc)
            vocab = argv[++i];
        else if (!strcmp(argv[i], "--merges") && i + 1 < argc)
            merges = argv[++i];
        else if (!strcmp(argv[i], "--algo") && i + 1 < argc)
            algo = argv[++i];
        else
            input = argv[i];
    }
    if (input.empty() || (algo != "naive" && algo != "heap"))
    {
        std::cerr << "usage: " << argv[0] << " <input.txt> [-o ids.bin] [--algo naive|heap] [--vocab p] [--merges p]\n";
        return 1;
    }

    try
    {
        Tokenizer tok;
        tok.load(vocab, merges);

        std::ifstream f(input, std::ios::binary); // binary: do not translate line endings
        if (!f)
            throw std::runtime_error("cannot open " + input);
        std::stringstream ss;
        ss << f.rdbuf();
        const std::string text = ss.str();

        // Time the two stages separately; this is the profile of the baseline.
        auto t0 = Clock::now();
        auto words = Tokenizer::pretokenize(text);
        double t_pre = ms_since(t0);

        t0 = Clock::now();
        std::vector<int32_t> ids;
        ids.reserve(text.size() / 3);
        const bool use_heap = (algo == "heap");
        for (const Span &w : words)
        {
            if (use_heap)
                tok.encode_word_heap(text.data() + w.offset, w.length, ids);
            else
                tok.encode_word(text.data() + w.offset, w.length, ids);
        }
        double t_enc = ms_since(t0);

        double total = t_pre + t_enc;
        std::printf("algorithm  : %s\n", algo.c_str());
        std::printf("bytes      : %zu\n", text.size());
        std::printf("words      : %zu\n", words.size());
        std::printf("tokens     : %zu\n", ids.size());
        std::printf("pretokenize: %.2f ms\n", t_pre);
        std::printf("encode     : %.2f ms\n", t_enc);
        std::printf("total      : %.2f ms  (%.0f tokens/s)\n", total, ids.size() / (total / 1000.0));

        if (!out_path.empty())
        {
            std::ofstream o(out_path, std::ios::binary);
            o.write(reinterpret_cast<const char *>(ids.data()), ids.size() * sizeof(int32_t));
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
