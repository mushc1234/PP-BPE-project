#!/usr/bin/env python3
"""Check a C++ encoder's output against tiktoken's GPT-2 tokenizer.

Usage:
  python scripts/verify.py <input.txt> <ids.bin>
where ids.bin is the raw int32 output written by `bin/bpe_seq <input.txt> -o ids.bin`.
Needs: pip install tiktoken
"""
import array
import sys

import tiktoken


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    text = open(sys.argv[1], "rb").read().decode("utf-8")  # bytes -> str, no newline translation
    mine = array.array("i")
    with open(sys.argv[2], "rb") as f:
        mine.frombytes(f.read())

    # disallowed_special=() makes text like "<|endoftext|>" an ordinary string, as in our C++ code.
    ref = tiktoken.get_encoding("gpt2").encode(text, disallowed_special=())

    if list(mine) == ref:
        print(f"OK: all {len(ref)} token ids match tiktoken")
        return
    # Report the first mismatch with some context to make debugging easy.
    i = next((k for k in range(min(len(mine), len(ref))) if mine[k] != ref[k]), min(len(mine), len(ref)))
    enc = tiktoken.get_encoding("gpt2")
    print(f"MISMATCH: mine={len(mine)} tokens, tiktoken={len(ref)} tokens, first difference at index {i}")
    print("  mine    :", [enc.decode([t]) for t in mine[max(0, i - 3): i + 4]])
    print("  tiktoken:", [enc.decode([t]) for t in ref[max(0, i - 3): i + 4]])
    sys.exit(1)


if __name__ == "__main__":
    main()
