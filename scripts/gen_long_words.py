#!/usr/bin/env python3
"""Generate test files where every word has (about) the same length L.

Takes real English text, keeps only ASCII letters, and cuts it into chunks of L letters
separated by one space. The GPT-2 pre-tokenizer sees each chunk as one word of length L, so
we can study how the encoders scale with word length while the merge behaviour stays realistic.

Usage: python scripts/gen_long_words.py [source.txt] [total_bytes]
Output: data/long/words_<L>.txt
"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LENGTHS = [4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096]


def main():
    src = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "data" / "wikitext-103" / "test.txt"
    total = int(sys.argv[2]) if len(sys.argv) > 2 else 256 * 1024

    raw = src.read_bytes()
    letters = bytes(b for b in raw if (b | 32) >= ord("a") and (b | 32) <= ord("z"))
    out_dir = ROOT / "data" / "long"
    out_dir.mkdir(parents=True, exist_ok=True)

    for L in LENGTHS:
        n_chunks = total // (L + 1)
        need = n_chunks * L
        if need > len(letters):
            print(f"skip L={L}: source has only {len(letters)} letters")
            continue
        chunks = [letters[i * L:(i + 1) * L] for i in range(n_chunks)]
        dst = out_dir / f"words_{L}.txt"
        dst.write_bytes(b" ".join(chunks))
        print(f"{dst.name}: {n_chunks} words of length {L}")


if __name__ == "__main__":
    main()
