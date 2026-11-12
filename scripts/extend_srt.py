#!/usr/bin/env python3
"""Extend each SRT subtitle cue's end timestamp toward the next cue's start time without overlapping."""

from __future__ import annotations

import argparse
import os
import re
import sys
from pathlib import Path

TIMESTAMP_RE = re.compile(
    r"^(\d{2}):(\d{2}):(\d{2})[,.](\d{3})\s*-->\s*(\d{2}):(\d{2}):(\d{2})[,.](\d{3})(.*)$"
)


def to_ms(h: str, m: str, s: str, ms: str) -> int:
    return ((int(h) * 60 + int(m)) * 60 + int(s)) * 1000 + int(ms)


def format_ms(total_ms: int) -> str:
    if total_ms < 0:
        total_ms = 0
    ms = total_ms % 1000
    total_s = total_ms // 1000
    s = total_s % 60
    total_m = total_s // 60
    m = total_m % 60
    h = total_m // 60
    return f"{h:02d}:{m:02d}:{s:02d},{ms:03d}"


def default_output_path(input_path: Path) -> Path:
    if input_path.suffix.lower() == ".srt":
        return input_path.with_name(f"{input_path.stem}.extended.srt")
    return input_path.with_name(f"{input_path.name}.extended.srt")


def extend_srt(
    input_path: Path,
    output_path: Path,
    gap_ms: int = 150,
    max_extend_ms: int | None = None,
) -> tuple[int, int, int]:
    raw_text = input_path.read_text(encoding="utf-8-sig")
    newline = "\r\n" if "\r\n" in raw_text else "\n"
    blocks = re.split(r"(?:\r?\n){2,}", raw_text.strip())

    parsed_cues: list[dict] = []
    for block in blocks:
        lines = block.splitlines()
        if len(lines) < 2:
            continue
        ts_idx = 1 if TIMESTAMP_RE.match(lines[1].strip()) else (0 if TIMESTAMP_RE.match(lines[0].strip()) else -1)
        if ts_idx == -1:
            continue
        m = TIMESTAMP_RE.match(lines[ts_idx].strip())
        assert m is not None
        start_ms = to_ms(m.group(1), m.group(2), m.group(3), m.group(4))
        end_ms = to_ms(m.group(5), m.group(6), m.group(7), m.group(8))
        trailing = m.group(9)
        header = lines[:ts_idx]
        body = lines[ts_idx + 1 :]
        parsed_cues.append(
            {
                "header": header,
                "start_ms": start_ms,
                "end_ms": end_ms,
                "trailing": trailing,
                "body": body,
            }
        )

    if not parsed_cues:
        raise ValueError(f"No valid SRT cues found in {input_path}")

    extended_count = 0
    for i in range(len(parsed_cues) - 1):
        cur = parsed_cues[i]
        nxt = parsed_cues[i + 1]
        old_end = cur["end_ms"]
        target_end = nxt["start_ms"] - gap_ms
        if max_extend_ms is not None:
            target_end = min(target_end, old_end + max_extend_ms)
        if target_end > old_end and target_end > cur["start_ms"]:
            cur["end_ms"] = target_end
            extended_count += 1

    min_gap = (
        min(
            parsed_cues[i + 1]["start_ms"] - parsed_cues[i]["end_ms"]
            for i in range(len(parsed_cues) - 1)
        )
        if len(parsed_cues) > 1
        else 0
    )

    out_blocks: list[str] = []
    for idx, cue in enumerate(parsed_cues, start=1):
        header_lines = cue["header"] if cue["header"] else [str(idx)]
        ts_line = f"{format_ms(cue['start_ms'])} --> {format_ms(cue['end_ms'])}{cue['trailing']}"
        out_blocks.append(newline.join([*header_lines, ts_line, *cue["body"]]))

    sep = newline * 2
    with output_path.open("w", encoding="utf-8", newline="") as f:
        f.write(sep.join(out_blocks) + newline)
    return len(parsed_cues), extended_count, min_gap


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Extend each SRT cue end time close to the next cue's start time without overlapping."
    )
    parser.add_argument("input", type=Path, help="Path to input .srt file (never overwritten)")
    parser.add_argument(
        "-o",
        "--output",
        type=Path,
        default=None,
        help="Path to output .srt file (default: <name>.extended.srt)",
    )
    parser.add_argument(
        "--gap",
        type=int,
        default=150,
        help="Minimum gap in ms to leave before the next cue starts (default: 150)",
    )
    parser.add_argument(
        "--max-extend",
        type=int,
        default=None,
        help="Optional maximum extension per cue in ms (default: unlimited)",
    )
    args = parser.parse_args(argv)

    if args.gap < 0:
        parser.error("--gap must be >= 0")
    if args.max_extend is not None and args.max_extend < 0:
        parser.error("--max-extend must be >= 0")

    input_path = args.input.resolve()
    if not input_path.is_file():
        print(f"[ERROR] File not found: {input_path}", file=sys.stderr)
        return 1

    output_path = (args.output if args.output is not None else default_output_path(input_path)).resolve()
    if os.path.normcase(str(output_path)) == os.path.normcase(str(input_path)):
        print("[ERROR] Output path cannot be the same as input path (original must not be overwritten).", file=sys.stderr)
        return 1

    total, extended, min_gap = extend_srt(input_path, output_path, args.gap, args.max_extend)
    print(f"Input   : {input_path}")
    print(f"Output  : {output_path}")
    print(f"Cues    : {total} total, {extended} extended, {total - extended} unchanged")
    print(f"Min gap : {min_gap} ms (target gap: {args.gap} ms)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
