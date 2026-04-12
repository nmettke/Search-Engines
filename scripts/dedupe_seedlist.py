#!/usr/bin/env python3

import argparse
from pathlib import Path


def dedupe_seedlist(input_path: Path, output_path: Path) -> tuple[int, int]:
    seen = set()
    unique_links = []

    with input_path.open("r", encoding="utf-8") as f:
        for line in f:
            link = line.strip()
            if not link:
                continue
            if link in seen:
                continue
            seen.add(link)
            unique_links.append(link)

    with output_path.open("w", encoding="utf-8") as f:
        for link in unique_links:
            f.write(link + "\n")

    return len(unique_links), len(seen)


def main() -> None:
    parser = argparse.ArgumentParser(description="Remove duplicate links from a seed list.")
    parser.add_argument(
        "--input",
        default="src/crawler/seedList.txt",
        help="Input seed list path (default: src/crawler/seedList.txt)",
    )
    parser.add_argument(
        "--output",
        default=None,
        help="Output path. Defaults to in-place overwrite of --input.",
    )

    args = parser.parse_args()
    input_path = Path(args.input)
    output_path = Path(args.output) if args.output else input_path

    if not input_path.exists():
        raise FileNotFoundError(f"Input file not found: {input_path}")

    total_lines = 0
    with input_path.open("r", encoding="utf-8") as f:
        for line in f:
            if line.strip():
                total_lines += 1

    unique_count, _ = dedupe_seedlist(input_path, output_path)
    removed_count = total_lines - unique_count

    print(f"Processed: {total_lines} non-empty links")
    print(f"Unique: {unique_count}")
    print(f"Removed duplicates: {removed_count}")
    print(f"Wrote deduplicated list to: {output_path}")


if __name__ == "__main__":
    main()