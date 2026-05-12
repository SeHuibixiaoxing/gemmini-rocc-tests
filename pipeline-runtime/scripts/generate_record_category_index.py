#!/usr/bin/env python3

from __future__ import annotations

import re
from collections import defaultdict
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Iterable


ROOT = Path(__file__).resolve().parents[1]

CATEGORY_ORDER = ["main", "tracev", "gdbserver"]
CATEGORY_TITLES = {
    "main": "Main",
    "tracev": "TraceV",
    "gdbserver": "GDBServer",
}
CATEGORY_DESCRIPTIONS = {
    "main": "主线 `pairdummy/sbus128` / `pipeline-runtime` 语义、workflow、artifact、DMA/Gemmini/RR/SPM-xlate 调试",
    "tracev": "`TraceV` / `TracerV` / `workerpc` / `metasim` / marker / tracefile 调试",
    "gdbserver": "`gdbserver` / NIC 网络联通 / host-guest attach / 最小 Linux smoke 调试",
}

TRACEV_PATTERNS = [
    re.compile(pattern, re.IGNORECASE)
    for pattern in (
        r"tracerv",
        r"tracev",
        r"tracefile",
        r"workerpc",
        r"selector=2",
        r"selector=3",
    )
]

GDBSERVER_PATTERNS = [
    re.compile(pattern, re.IGNORECASE)
    for pattern in (
        r"gdbserver",
        r"target remote",
        r"SSHPort",
        r"networked[_ -]?gdbserver",
        r"guest tcp",
    )
]

IGNORE_FILES = {"README.md", "CATEGORY_INDEX.md"}
EXPLICIT_CATEGORY_RE = re.compile(r"(?:调试类别|category)\s*[:：]\s*(main|tracev|gdbserver)\b", re.IGNORECASE)


@dataclass(frozen=True)
class RecordInfo:
    path: Path
    category: str
    summary: str


def classify_record(text: str, default_category: str | None = None) -> str:
    match = EXPLICIT_CATEGORY_RE.search(text)
    if match:
        return match.group(1).lower()
    if default_category:
        return default_category
    head_text = "\n".join(first_content_lines(text, limit=6))
    tracev_hits = sum(len(pattern.findall(text)) for pattern in TRACEV_PATTERNS)
    gdbserver_hits = sum(len(pattern.findall(text)) for pattern in GDBSERVER_PATTERNS)
    if any(pattern.search(head_text) for pattern in TRACEV_PATTERNS):
        return "tracev"
    if tracev_hits >= 2:
        return "tracev"
    if any(pattern.search(head_text) for pattern in GDBSERVER_PATTERNS):
        return "gdbserver"
    if gdbserver_hits >= 2:
        return "gdbserver"
    return "main"


def first_content_lines(text: str, limit: int) -> list[str]:
    lines: list[str] = []
    for raw_line in text.splitlines():
        line = raw_line.strip()
        if not line:
            continue
        if line.startswith("#"):
            continue
        if line.startswith("```"):
            continue
        if EXPLICIT_CATEGORY_RE.search(line):
            continue
        line = re.sub(r"\s+", " ", line)
        lines.append(line)
        if len(lines) >= limit:
            break
    return lines


def summarize_record(text: str) -> str:
    lines = first_content_lines(text, limit=1)
    if lines:
        return lines[0][:120]
    return "n/a"


def collect_records(
    record_dir: Path,
    default_categories: dict[str, str] | None = None,
) -> list[RecordInfo]:
    records: list[RecordInfo] = []
    for path in sorted(record_dir.glob("*.md")):
        if path.name in IGNORE_FILES:
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        records.append(
            RecordInfo(
                path=path,
                category=classify_record(text, default_categories.get(path.name) if default_categories else None),
                summary=summarize_record(text),
            )
        )
    return records


def render_index(title: str, record_type: str, records: Iterable[RecordInfo]) -> str:
    records = list(records)
    grouped: dict[str, list[RecordInfo]] = defaultdict(list)
    for record in records:
        grouped[record.category].append(record)

    generated_utc = datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M UTC")
    lines: list[str] = [
        f"# {title}",
        "",
        f"更新时间：`{generated_utc}`",
        "",
        "说明：",
        "",
        "- 原始记录文件仍保持扁平的 UTC 时间戳命名；本索引只负责按调试类别聚合。",
        "- 分类规则由 `scripts/generate_record_category_index.py` 生成。",
        "- 当前固定三类：`main`、`tracev` 与 `gdbserver`；新增记录按规则自动归类。",
        "",
        "## 类别说明",
        "",
    ]

    for category in CATEGORY_ORDER:
        count = len(grouped.get(category, []))
        lines.append(
            f"- `{category}`：{CATEGORY_DESCRIPTIONS[category]}（当前 {record_type} 数量：`{count}`）"
        )

    lines.extend(["", "## 按类别索引", ""])

    for category in CATEGORY_ORDER:
        lines.append(f"### `{category}`")
        lines.append("")
        entries = grouped.get(category, [])
        if not entries:
            lines.append("- 暂无")
            lines.append("")
            continue
        for entry in entries:
            lines.append(f"- `{entry.path.name}`：{entry.summary}")
        lines.append("")

    return "\n".join(lines).rstrip() + "\n"


def main() -> int:
    debug_dir = ROOT / "debug_records"
    debug_index = ROOT / "debug_records" / "CATEGORY_INDEX.md"
    change_dir = ROOT / "change_records"
    change_index = ROOT / "change_records" / "CATEGORY_INDEX.md"

    debug_records = collect_records(debug_dir)
    debug_index.write_text(
        render_index(
            "Pipeline Runtime Debug Record Categories",
            "debug record",
            debug_records,
        ),
        encoding="utf-8",
    )
    print(f"wrote {debug_index}")

    sibling_defaults = {record.path.name: record.category for record in debug_records}
    change_records = collect_records(change_dir, default_categories=sibling_defaults)
    change_index.write_text(
        render_index(
            "Pipeline Runtime Change Record Categories",
            "change record",
            change_records,
        ),
        encoding="utf-8",
    )
    print(f"wrote {change_index}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
