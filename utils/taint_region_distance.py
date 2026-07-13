#!/usr/bin/env python3
import argparse
import csv
import re
import statistics
import sys
from dataclasses import dataclass


BB_RE = re.compile(r"^\s*bb\.[^:]+:")
NAME_RE = re.compile(r"^name:\s*(.+?)\s*$")


@dataclass
class Region:
    index: int
    function: str
    block: str
    start_line: int
    end_line: int = 0
    instr_count: int = 0
    gap_before: int | None = None
    gap_after: int | None = None


def is_barrier(text: str, name: str) -> bool:
    return re.match(rf"^\s*{name}\b", text) is not None


def is_counted_instruction(text: str, include_debug: bool) -> bool:
    stripped = text.strip()
    if not stripped:
        return False
    if stripped.startswith("#"):
        return False
    if stripped.startswith(("successors:", "liveins:", "implicit-defs:", "tracksRegLiveness:")):
        return False
    if stripped.startswith(("bb.", "body:", "---", "...")):
        return False
    if stripped.startswith(("name:", "alignment:", "exposesReturnsTwice:", "legalized:")):
        return False
    if stripped.startswith(("regBankSelected:", "selected:", "failedISel:", "registers:")):
        return False
    if stripped.startswith(("liveins:", "frameInfo:", "machineFunctionInfo:")):
        return False
    if stripped.startswith(("DBG_", "EH_LABEL", "CFI_INSTRUCTION")) and not include_debug:
        return False
    if is_barrier(text, "ISB") or is_barrier(text, "DSB"):
        return False
    return text.startswith("    ")


def parse_mir(path: str, include_debug: bool) -> list[Region]:
    regions: list[Region] = []
    function = "<unknown>"
    block = "<unknown>"
    open_region: Region | None = None
    pending_gap: dict[str, int] = {}
    previous_region: dict[str, Region] = {}

    with open(path, encoding="utf-8") as f:
        for line_no, line in enumerate(f, 1):
            line = line.rstrip("\n")

            name_match = NAME_RE.match(line)
            if name_match:
                function = name_match.group(1)
                block = "<unknown>"
                open_region = None
                pending_gap.setdefault(function, 0)
                continue

            if BB_RE.match(line):
                block = line.strip().rstrip(":")
                continue

            if is_barrier(line, "ISB"):
                if open_region is not None:
                    print(
                        f"warning: nested ISB at {path}:{line_no}; closing previous open region",
                        file=sys.stderr,
                    )
                    open_region.end_line = line_no

                prev = previous_region.get(function)
                region = Region(
                    index=len(regions) + 1,
                    function=function,
                    block=block,
                    start_line=line_no,
                    gap_before=pending_gap.get(function) if prev is not None else None,
                )
                if prev is not None:
                    prev.gap_after = region.gap_before
                open_region = region
                regions.append(region)
                pending_gap[function] = 0
                continue

            if is_barrier(line, "DSB"):
                if open_region is None:
                    print(f"warning: unmatched DSB at {path}:{line_no}", file=sys.stderr)
                else:
                    open_region.end_line = line_no
                    previous_region[function] = open_region
                    open_region = None
                    pending_gap[function] = 0
                continue

            if is_counted_instruction(line, include_debug):
                if open_region is not None:
                    open_region.instr_count += 1
                elif function in previous_region:
                    pending_gap[function] = pending_gap.get(function, 0) + 1

    if open_region is not None:
        print(
            f"warning: region opened at {path}:{open_region.start_line} has no closing DSB",
            file=sys.stderr,
        )

    return regions


def mean(values: list[int]) -> float:
    return statistics.fmean(values) if values else 0.0


def print_summary(regions: list[Region], function_filter: str | None) -> None:
    functions = []
    seen = set()
    for region in regions:
        if function_filter and region.function != function_filter:
            continue
        if region.function not in seen:
            seen.add(region.function)
            functions.append(region.function)

    for function in functions:
        fn_regions = [r for r in regions if r.function == function]
        lengths = [r.instr_count for r in fn_regions]
        gaps = [r.gap_before for r in fn_regions if r.gap_before is not None]

        print(f"\nFunction: {function}")
        print(f"  protected regions: {len(fn_regions)}")
        print(f"  region length: avg={mean(lengths):.2f} min={min(lengths)} max={max(lengths)}")
        if gaps:
            print(f"  gap between regions: avg={mean(gaps):.2f} min={min(gaps)} max={max(gaps)}")
            print(f"  adjacent regions with gap <= 2: {sum(1 for g in gaps if g <= 2)}")
        else:
            print("  gap between regions: n/a")


def print_table(regions: list[Region], function_filter: str | None, limit: int | None) -> None:
    rows = [r for r in regions if not function_filter or r.function == function_filter]
    if limit is not None:
        rows = rows[:limit]

    print()
    print(
        f"{'Region':>6}  {'Function':<28} {'Block':<34} "
        f"{'MIR lines':<13} {'Len':>5} {'GapPrev':>7} {'GapNext':>7}"
    )
    print("-" * 112)
    for r in rows:
        gap_before = "-" if r.gap_before is None else str(r.gap_before)
        gap_after = "-" if r.gap_after is None else str(r.gap_after)
        print(
            f"{r.index:6d}  {r.function:<28.28} {r.block:<34.34} "
            f"{r.start_line}-{r.end_line:<7} {r.instr_count:5d} "
            f"{gap_before:>7} {gap_after:>7}"
        )


def write_csv(regions: list[Region], function_filter: str | None, output: str) -> None:
    rows = [r for r in regions if not function_filter or r.function == function_filter]
    with open(output, "w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        writer.writerow(
            [
                "region",
                "function",
                "block",
                "start_line",
                "end_line",
                "instruction_count",
                "gap_before",
                "gap_after",
            ]
        )
        for r in rows:
            writer.writerow(
                [
                    r.index,
                    r.function,
                    r.block,
                    r.start_line,
                    r.end_line,
                    r.instr_count,
                    "" if r.gap_before is None else r.gap_before,
                    "" if r.gap_after is None else r.gap_after,
                ]
            )


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Report protected-region lengths and distances from a hardened MIR file."
    )
    parser.add_argument(
        "mir",
        nargs="?",
        default="playground/firefox_convolve_int.hardened.mir",
        help="hardened MIR file",
    )
    parser.add_argument("--function", help="only show one MachineFunction")
    parser.add_argument("--limit", type=int, help="limit table rows")
    parser.add_argument("--csv", metavar="FILE", help="write the per-region table as CSV")
    parser.add_argument(
        "--include-debug",
        action="store_true",
        help="count DBG_/CFI pseudo-instructions in lengths and gaps",
    )
    args = parser.parse_args()

    regions = parse_mir(args.mir, args.include_debug)
    if not regions:
        print(f"no ISB/DSB protected regions found in {args.mir}", file=sys.stderr)
        return 1

    selected = [r for r in regions if not args.function or r.function == args.function]
    if not selected:
        print(f"no regions found for function {args.function!r}", file=sys.stderr)
        return 1

    print_summary(selected, args.function)
    print_table(selected, args.function, args.limit)

    if args.csv:
        write_csv(selected, args.function, args.csv)
        print(f"\nCSV: {args.csv}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
