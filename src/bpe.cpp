#include "bpe.hpp"

#include <algorithm>
#include <climits>
#include <fstream>
#include <queue>
#include <sstream>
#include <stdexcept>

// ---------------------------------------------------------------------------------------
// Unicode helpers
// ---------------------------------------------------------------------------------------

struct CodeRange
{
    uint32_t lo, hi;
};
#include "unicode_tables.inc" // kLetterRanges (\p{L}) and kNumberRanges (\p{N})

static bool in_ranges(const CodeRange *r, size_t n, uint32_t cp)
{
    // Binary search for the last range whose lo <= cp, then check hi.
    size_t lo = 0, hi = n;
    while (lo < hi)
    {
        size_t mid = (lo + hi) / 2;
        if (r[mid].lo <= cp)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo > 0 && cp <= r[lo - 1].hi;
}

static bool is_letter(uint32_t cp)
{
    if (cp < 128)
        return (cp | 32) >= 'a' && (cp | 32) <= 'z'; // ASCII fast path
    return in_ranges(kLetterRanges, sizeof(kLetterRanges) / sizeof(CodeRange), cp);
}

static bool is_number(uint32_t cp)
{
    if (cp < 128)
        return cp >= '0' && cp <= '9';
    return in_ranges(kNumberRanges, sizeof(kNumberRanges) / sizeof(CodeRange), cp);
}

// Unicode White_Space property, which is what the regex's \s means.
static bool is_space(uint32_t cp)
{
    return (cp >= 9 && cp <= 13) || cp == 32 || cp == 0x85 || cp == 0xA0 || cp == 0x1680 ||
           (cp >= 0x2000 && cp <= 0x200A) || cp == 0x2028 || cp == 0x2029 || cp == 0x202F ||
           cp == 0x205F || cp == 0x3000;
}

// Decode one UTF-8 code point at s[pos]. Invalid bytes are returned as a 1-byte "other"
// character (cp = 0xFFFFFFFF) so the scanner never gets stuck.
static uint32_t decode_utf8(const std::string &s, size_t pos, size_t &len)
{
    unsigned char c = s[pos];
    int n = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2
                       : (c >> 4) == 0xE   ? 3
                       : (c >> 3) == 0x1E  ? 4
                                           : 0;
    if (n == 0 || pos + n > s.size())
    {
        len = 1;
        return 0xFFFFFFFF;
    }
    uint32_t cp = n == 1 ? c : c & (0xFF >> (n + 1));
    for (int i = 1; i < n; i++)
    {
        unsigned char cc = s[pos + i];
        if ((cc & 0xC0) != 0x80)
        {
            len = 1;
            return 0xFFFFFFFF;
        }
        cp = (cp << 6) | (cc & 0x3F);
    }
    len = n;
    return cp;
}

static void append_utf8(std::string &out, uint32_t cp)
{
    if (cp < 0x80)
    {
        out += char(cp);
    }
    else if (cp < 0x800)
    {
        out += char(0xC0 | (cp >> 6));
        out += char(0x80 | (cp & 0x3F));
    }
    else if (cp < 0x10000)
    {
        out += char(0xE0 | (cp >> 12));
        out += char(0x80 | ((cp >> 6) & 0x3F));
        out += char(0x80 | (cp & 0x3F));
    }
    else
    {
        out += char(0xF0 | (cp >> 18));
        out += char(0x80 | ((cp >> 12) & 0x3F));
        out += char(0x80 | ((cp >> 6) & 0x3F));
        out += char(0x80 | (cp & 0x3F));
    }
}

// ---------------------------------------------------------------------------------------
// Loading vocab.json / merges.txt
// ---------------------------------------------------------------------------------------

static std::string read_file(const std::string &path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f)
        throw std::runtime_error("cannot open " + path);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static uint32_t hex4(const std::string &s, size_t i)
{
    return std::stoul(s.substr(i, 4), nullptr, 16);
}

// vocab.json is a flat object {"token": id, ...}; a tiny hand-written parser is enough.
static std::unordered_map<std::string, int32_t> parse_vocab(const std::string &s)
{
    std::unordered_map<std::string, int32_t> vocab;
    size_t i = 0, n = s.size();
    while (i < n)
    {
        if (s[i] != '"')
        {
            i++;
            continue;
        }
        // Parse a JSON string (the key), decoding escapes to UTF-8.
        std::string key;
        for (i++; i < n && s[i] != '"'; i++)
        {
            if (s[i] != '\\')
            {
                key += s[i];
                continue;
            }
            char e = s[++i];
            switch (e)
            {
            case 'n':
                key += '\n';
                break;
            case 't':
                key += '\t';
                break;
            case 'r':
                key += '\r';
                break;
            case 'b':
                key += '\b';
                break;
            case 'f':
                key += '\f';
                break;
            case 'u':
            {
                uint32_t cp = hex4(s, i + 1);
                i += 4;
                // Surrogate pair: 😀 -> one code point
                if (cp >= 0xD800 && cp < 0xDC00 && s[i + 1] == '\\' && s[i + 2] == 'u')
                {
                    uint32_t lo = hex4(s, i + 3);
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    i += 6;
                }
                append_utf8(key, cp);
                break;
            }
            default:
                key += e; // \" \\ \/
            }
        }
        i++; // closing quote
        while (i < n && (s[i] == ':' || s[i] == ' '))
            i++;
        int32_t id = 0;
        for (; i < n && s[i] >= '0' && s[i] <= '9'; i++)
            id = id * 10 + (s[i] - '0');
        vocab[key] = id;
    }
    return vocab;
}

void Tokenizer::load(const std::string &vocab_path, const std::string &merges_path)
{
    auto vocab = parse_vocab(read_file(vocab_path));
    vocab_size_ = vocab.size();

    // GPT-2 stores raw bytes as printable unicode characters (e.g. space -> U+0120 'Ġ').
    // Printable bytes map to themselves; the other bytes map to 256, 257, ... in order.
    int next = 0;
    for (int b = 0; b < 256; b++)
    {
        bool printable = (b >= 33 && b <= 126) || (b >= 161 && b <= 172) || (b >= 174 && b <= 255);
        std::string ch;
        append_utf8(ch, printable ? b : 256 + next++);
        byte_to_id_[b] = vocab.at(ch);
    }

    // Each merge line is "left right"; its line number is the rank. The merged token's id is
    // the vocab entry of the concatenated strings.
    std::istringstream in(read_file(merges_path));
    std::string line;
    int32_t rank = 0;
    while (std::getline(in, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty() || line.rfind("#version", 0) == 0)
            continue;
        size_t sp = line.find(' ');
        std::string a = line.substr(0, sp), b = line.substr(sp + 1);
        merges_[pair_key(vocab.at(a), vocab.at(b))] = {rank++, vocab.at(a + b)};
    }
}

// ---------------------------------------------------------------------------------------
// Pre-tokenization (GPT-2 regex, hand-written)
// ---------------------------------------------------------------------------------------

std::vector<Span> Tokenizer::pretokenize(const std::string &text)
{
    std::vector<Span> words;
    const size_t n = text.size();
    size_t pos = 0;
    size_t len;

    while (pos < n)
    {
        // 1) Contractions: 's 't 're 've 'm 'll 'd (case-sensitive)
        if (text[pos] == '\'' && pos + 1 < n)
        {
            char c1 = text[pos + 1];
            char c2 = pos + 2 < n ? text[pos + 2] : '\0';
            size_t m = 0;
            if (c1 == 's' || c1 == 't' || c1 == 'm' || c1 == 'd')
                m = 2;
            else if ((c1 == 'r' && c2 == 'e') || (c1 == 'v' && c2 == 'e') || (c1 == 'l' && c2 == 'l'))
                m = 3;
            if (m)
            {
                words.push_back({pos, m});
                pos += m;
                continue;
            }
        }

        // 2-4) An optional single leading space, then a run of letters / numbers / symbols.
        size_t p = pos + (text[pos] == ' ' ? 1 : 0);
        if (p < n)
        {
            uint32_t cp = decode_utf8(text, p, len);
            if (!is_space(cp))
            {
                bool L = is_letter(cp), N = !L && is_number(cp);
                size_t end = p;
                while (end < n)
                {
                    cp = decode_utf8(text, end, len);
                    bool same = L ? is_letter(cp) : N ? is_number(cp)
                                                      : !(is_space(cp) || is_letter(cp) || is_number(cp));
                    if (!same)
                        break;
                    end += len;
                }
                words.push_back({pos, end - pos});
                pos = end;
                continue;
            }
        }

        // 5-6) Whitespace run. `\s+(?!\S)` leaves the last space for the next word
        // (so " world" keeps its leading space); a lone space or one at the end is taken whole.
        size_t end = pos, last = pos;
        while (end < n)
        {
            uint32_t cp = decode_utf8(text, end, len);
            if (!is_space(cp))
                break;
            last = end;
            end += len;
        }
        if (end < n && last > pos)
            end = last; // give the last whitespace char back
        words.push_back({pos, end - pos});
        pos = end;
    }
    return words;
}

// ---------------------------------------------------------------------------------------
// BPE encoding of one word (Sennrich et al. 2016, O(n^2) version)
// ---------------------------------------------------------------------------------------

void Tokenizer::encode_word(const char *data, size_t len, std::vector<int32_t> &out) const
{
    // Start from one token per byte.
    std::vector<int32_t> ids(len);
    for (size_t i = 0; i < len; i++)
        ids[i] = byte_to_id_[(unsigned char)data[i]];

    while (ids.size() >= 2)
    {
        // Find the adjacent pair with the lowest rank (rescan the whole word every round).
        int32_t best_rank = INT_MAX, best_a = 0, best_b = 0, best_result = 0;
        for (size_t i = 0; i + 1 < ids.size(); i++)
        {
            auto it = merges_.find(pair_key(ids[i], ids[i + 1]));
            if (it != merges_.end() && it->second.rank < best_rank)
            {
                best_rank = it->second.rank;
                best_a = ids[i];
                best_b = ids[i + 1];
                best_result = it->second.result;
            }
        }
        if (best_rank == INT_MAX)
            break; // nothing left to merge

        // Merge every (non-overlapping, left-to-right) occurrence of that pair, in place.
        size_t w = 0;
        for (size_t i = 0; i < ids.size();)
        {
            if (i + 1 < ids.size() && ids[i] == best_a && ids[i + 1] == best_b)
            {
                ids[w++] = best_result;
                i += 2;
            }
            else
            {
                ids[w++] = ids[i++];
            }
        }
        ids.resize(w);
    }
    out.insert(out.end(), ids.begin(), ids.end());
}

// ---------------------------------------------------------------------------------------
// BPE encoding of one word, O(n log n) version: linked list + min-heap
// ---------------------------------------------------------------------------------------
//
// Instead of rescanning the whole word every round, keep every candidate pair in a min-heap
// ordered by (rank, position). After a merge only the two pairs touching the new token change,
// so only those two are looked up and pushed. Heap entries whose tokens have since changed are
// stale and are simply skipped when popped (lazy deletion).
//
// Same output as encode_word: equal-rank pairs are merged left to right, and a token created by
// merge k can only form pairs with rank > k, so processing by (rank, position) is equivalent.

void Tokenizer::encode_word_heap(const char *data, size_t len, std::vector<int32_t> &out) const
{
    if (len == 0)
        return;

    // Doubly linked list over byte positions. id[i] = -1 marks a node that was merged away.
    std::vector<int32_t> id(len), prev(len), next(len);
    for (size_t i = 0; i < len; i++)
    {
        id[i] = byte_to_id_[(unsigned char)data[i]];
        prev[i] = int32_t(i) - 1;
        next[i] = i + 1 < len ? int32_t(i) + 1 : -1;
    }

    // A candidate merge of node `pos` with its right neighbour (whose tokens were left/right).
    struct Cand
    {
        int32_t rank, pos, left, right, result;
    };
    auto later = [](const Cand &a, const Cand &b)
    { return a.rank != b.rank ? a.rank > b.rank : a.pos > b.pos; };
    std::priority_queue<Cand, std::vector<Cand>, decltype(later)> heap(later);

    // Look up the pair (p, next[p]); push it if it is mergeable.
    auto push_pair = [&](int32_t p)
    {
        int32_t q = next[p];
        if (q < 0)
            return;
        auto it = merges_.find(pair_key(id[p], id[q]));
        if (it != merges_.end())
            heap.push({it->second.rank, p, id[p], id[q], it->second.result});
    };

    for (size_t i = 0; i + 1 < len; i++)
        push_pair(int32_t(i));

    while (!heap.empty())
    {
        Cand c = heap.top();
        heap.pop();

        // Skip stale entries: the tokens at this position are no longer the ones we looked up.
        int32_t p = c.pos, q = next[p];
        if (id[p] != c.left || q < 0 || id[q] != c.right)
            continue;

        // Merge: node p becomes the new token, node q is unlinked.
        id[p] = c.result;
        id[q] = -1;
        next[p] = next[q];
        if (next[q] >= 0)
            prev[next[q]] = p;

        // Only the pairs touching the new token changed.
        if (prev[p] >= 0)
            push_pair(prev[p]);
        push_pair(p);
    }

    // Node 0 is never removed (only right-hand nodes are), so the list always starts there.
    for (int32_t i = 0; i >= 0; i = next[i])
        out.push_back(id[i]);
}

std::vector<int32_t> Tokenizer::encode(const std::string &text) const
{
    std::vector<int32_t> out;
    out.reserve(text.size() / 3);
    for (const Span &w : pretokenize(text))
        encode_word(text.data() + w.offset, w.length, out);
    return out;
}
