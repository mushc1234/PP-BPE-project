#!/usr/bin/env python3
"""Download the data needed by the BPE project into ./data.

  data/gpt2/vocab.json, data/gpt2/merges.txt   (GPT-2 merge table; stdlib only)
  data/wikitext-103/{test,validation,train}.txt (needs `pip install datasets`)

Usage:
  python scripts/download_data.py              # GPT-2 files + WikiText-103 test split (~1 MB, for local dev)
  python scripts/download_data.py --full       # GPT-2 files + all WikiText-103 splits (~540 MB, for the server)
  python scripts/download_data.py --gpt2-only  # GPT-2 files only
"""
import argparse
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DATA = ROOT / "data"

GPT2_BASE = "https://huggingface.co/openai-community/gpt2/resolve/main/"
GPT2_FILES = ["vocab.json", "merges.txt"]


def download_gpt2():
    out = DATA / "gpt2"
    out.mkdir(parents=True, exist_ok=True)
    for name in GPT2_FILES:
        dst = out / name
        if dst.exists() and dst.stat().st_size > 0:
            print(f"[skip] {dst}")
            continue
        print(f"[get ] {GPT2_BASE + name}")
        urllib.request.urlretrieve(GPT2_BASE + name, dst)
        print(f"       -> {dst} ({dst.stat().st_size} bytes)")


def download_wikitext(splits):
    try:
        from datasets import load_dataset
    except ImportError:
        raise SystemExit("WikiText needs the `datasets` package: pip install datasets")
    out = DATA / "wikitext-103"
    out.mkdir(parents=True, exist_ok=True)
    for split in splits:
        dst = out / f"{split}.txt"
        if dst.exists() and dst.stat().st_size > 0:
            print(f"[skip] {dst}")
            continue
        print(f"[get ] wikitext-103-raw-v1 / {split}")
        ds = load_dataset("Salesforce/wikitext", "wikitext-103-raw-v1", split=split)
        # Each row is one line of the original file (already ends with '\n' except blanks).
        with open(dst, "w", encoding="utf-8", newline="") as f:
            for row in ds:
                f.write(row["text"])
        print(f"       -> {dst} ({dst.stat().st_size} bytes)")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--full", action="store_true", help="download all WikiText-103 splits")
    ap.add_argument("--gpt2-only", action="store_true", help="skip WikiText")
    args = ap.parse_args()

    download_gpt2()
    if not args.gpt2_only:
        download_wikitext(["test", "validation", "train"] if args.full else ["test"])


if __name__ == "__main__":
    main()
