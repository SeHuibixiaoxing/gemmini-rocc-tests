#!/usr/bin/env python3

from __future__ import annotations

import argparse
import re
import sys
from collections import deque
from pathlib import Path
from typing import Deque, Dict, List, Optional

import decode_prt_breadcrumb as breadcrumb


WORKER_RE = re.compile(r"worker stage=(?P<stage>\d+) subbatch=(?P<subbatch>\d+) (?P<state>begin|done)")
POINTWISE_DISPATCH_RE = re.compile(
    r"conv-sync-strided stage=(?P<stage>\d+) mgr=(?P<mgr>\d+) dispatch=pointwise(?:-return rc=(?P<rc>-?\d+))?"
)
POINTWISE_MATMUL_RE = re.compile(
    r"pointwise-matmul-fallback stage=(?P<stage>\d+) mgr=(?P<mgr>\d+) (?P<state>begin|end)\b"
)
OC_SPLIT_POINTWISE_RE = re.compile(
    r"oc-split-pointwise stage=(?P<stage>\d+) tile=(?P<tile>\d+)/(?P<tiles>\d+) mgr=(?P<mgr>\d+) .* (?P<state>begin|end)$"
)
EXPORT_TARGET_RE = re.compile(
    r"export-target-dispatch stage=(?P<stage>\d+) tensor=(?P<tensor>\d+) target_seq=(?P<seq>\d+) .* phase=(?P<state>begin|end)"
)
DMA_FIXED_HOST_RE = re.compile(
    r"dma-fixed-load-host stage=(?P<stage>\d+) tensor=(?P<tensor>\d+) phase=(?P<phase>[a-z0-9-]+) "
    r"page=(?P<page>\d+)/(?P<page_count>\d+)"
)
DMA_EXPORT_HOST_RE = re.compile(
    r"dma-export-host stage=(?P<stage>\d+) tensor=(?P<tensor>\d+) phase=(?P<phase>[a-z0-9-]+) "
    r"page=(?P<page>\d+)/(?P<page_count>\d+)"
)
DMA_FIXED_SUBMIT_RE = re.compile(
    r"dma-fixed-load-submit stage=(?P<stage>\d+) tensor=(?P<tensor>\d+) phase=(?P<phase>[a-z0-9-]+) tok=(?P<tok>\d+)"
)
DMA_FIXED_WAIT_RE = re.compile(
    r"dma-fixed-load-wait phase=(?P<phase>[a-z0-9-]+) token=(?P<tok>\d+) stage=(?P<stage>\d+) tensor=(?P<tensor>\d+)"
)
DMA_EXPORT_WAIT_RE = re.compile(
    r"dma-export-wait phase=(?P<phase>[a-z0-9-]+) token=(?P<tok>\d+) stage=(?P<stage>\d+) tensor=(?P<tensor>\d+)"
)
SPM_XLATE_RELEASE_RE = re.compile(
    r"spm-xlate-release mgr=(?P<mgr>\d+) cfg=(?P<cfg>\d+) phase=(?P<phase>[a-z0-9-]+)"
)
TRIGGER_LINE_RE = re.compile(
    r"seq=(?P<seq>\d+) "
    r"fam=(?P<fam>\S+) "
    r"ph=(?P<phase>\S+) "
    r"seg=(?P<seg>\S+) "
    r"gs=(?P<gs>\S+) "
    r"ls=(?P<ls>\S+) "
    r"sb=(?P<sb>\S+) "
    r"mgr=(?P<mgr>\S+) "
    r"tn=(?P<tn>\S+) "
    r"pg=(?P<pg>\S+) "
    r"tok=(?P<tok>\S+) "
    r"rc=(?P<rc>-?\d+)"
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Quickly triage a pipeline-runtime host-watchdog capture before adding new logs."
    )
    parser.add_argument(
        "path",
        nargs="?",
        default="",
        help="capture directory, watchdog capture prefix, breadcrumb binary, or sparse log path",
    )
    parser.add_argument("--breadcrumb", default="", help="explicit breadcrumb binary path")
    parser.add_argument("--sparse-log", default="", help="explicit sparse log path")
    parser.add_argument("--trigger-log", default="", help="explicit trigger-log path")
    parser.add_argument("--tail-lines", type=int, default=4000, help="max sparse-log lines to inspect")
    parser.add_argument(
        "--trigger-post-budget",
        type=int,
        default=32,
        help="expected trigger post-budget; used to detect capture-window saturation",
    )
    parser.add_argument(
        "--trigger-match-once",
        type=int,
        default=1,
        help="expected trigger match-once setting; saturation detection assumes a single trigger window when this is non-zero",
    )
    parser.add_argument("--focus-stage", type=int, default=None, help="optional stage filter for DMA fixed-load triage")
    parser.add_argument("--focus-tensor", type=int, default=None, help="optional tensor filter for DMA fixed-load triage")
    parser.add_argument("--focus-page", type=int, default=None, help="optional page filter for DMA fixed-load triage")
    parser.add_argument("--previous-frontier", default="", help="optional previous frontier summary for claim classification")
    parser.add_argument(
        "--change-kind",
        default="none",
        choices=["none", "observability", "semantic_software", "hardware"],
        help="classify the repo change since the previous frontier",
    )
    parser.add_argument(
        "--emit-trigger-env",
        action="store_true",
        help="emit a recommended trigger-gate env block for the next rerun",
    )
    return parser.parse_args()


def resolve_inputs(args: argparse.Namespace) -> tuple[Optional[Path], Optional[Path], Optional[Path]]:
    breadcrumb_path = Path(args.breadcrumb) if args.breadcrumb else None
    sparse_log_path = Path(args.sparse_log) if args.sparse_log else None
    trigger_log_path = Path(args.trigger_log) if args.trigger_log else None

    if args.path:
        path = Path(args.path)
        if path.is_dir():
            if breadcrumb_path is None:
                matches = sorted(path.glob("*guest-breadcrumb.bin"))
                breadcrumb_path = matches[0] if matches else None
            if sparse_log_path is None:
                matches = sorted(path.glob("*guest-sparse-log.txt"))
                sparse_log_path = matches[0] if matches else None
            if trigger_log_path is None:
                matches = sorted(path.glob("*guest-trigger-log.txt"))
                trigger_log_path = matches[0] if matches else None
        else:
            resolved_from_prefix = False
            if path.exists():
                if path.name.endswith(".bin") and breadcrumb_path is None:
                    breadcrumb_path = path
                elif path.name.endswith("guest-sparse-log.txt") and sparse_log_path is None:
                    sparse_log_path = path
                elif path.name.endswith("guest-trigger-log.txt") and trigger_log_path is None:
                    trigger_log_path = path
                elif sparse_log_path is None:
                    sparse_log_path = path
            else:
                prefix = str(path)
                if breadcrumb_path is None:
                    candidate = Path(prefix + ".guest-breadcrumb.bin")
                    if candidate.exists():
                        breadcrumb_path = candidate
                        resolved_from_prefix = True
                if sparse_log_path is None:
                    candidate = Path(prefix + ".guest-sparse-log.txt")
                    if candidate.exists():
                        sparse_log_path = candidate
                        resolved_from_prefix = True
                if trigger_log_path is None:
                    candidate = Path(prefix + ".guest-trigger-log.txt")
                    if candidate.exists():
                        trigger_log_path = candidate
                        resolved_from_prefix = True
            if resolved_from_prefix and sparse_log_path is None and path.exists():
                sparse_log_path = path

    return breadcrumb_path, sparse_log_path, trigger_log_path


def tail_lines(path: Path, max_lines: int) -> List[str]:
    lines: Deque[str] = deque(maxlen=max_lines)
    with path.open("r", encoding="utf-8", errors="ignore") as handle:
        for line in handle:
            lines.append(line.rstrip("\n"))
    return list(lines)


def parse_sparse_summary(
    lines: List[str],
    *,
    focus_stage: Optional[int],
    focus_tensor: Optional[int],
    focus_page: Optional[int],
) -> Dict[str, object]:
    summary: Dict[str, Dict[str, str]] = {}
    focus_page_phases: List[str] = []
    focus_page_lines: List[str] = []
    last_doneflag_end_tok: Optional[int] = None
    last_wait_after_release_tok: Optional[int] = None
    for idx, line in enumerate(lines):
        for key, regex in (
            ("worker", WORKER_RE),
            ("pointwise_dispatch", POINTWISE_DISPATCH_RE),
            ("pointwise_matmul", POINTWISE_MATMUL_RE),
            ("oc_split_pointwise", OC_SPLIT_POINTWISE_RE),
            ("export_target", EXPORT_TARGET_RE),
        ):
            match = regex.search(line)
            if not match:
                continue
            data = {k: v for k, v in match.groupdict().items() if v is not None}
            data["line"] = line
            data["line_idx"] = str(idx)
            summary[key] = data

        match = DMA_FIXED_HOST_RE.search(line)
        if match:
            data = {k: v for k, v in match.groupdict().items() if v is not None}
            data["line"] = line
            data["line_idx"] = str(idx)
            summary["dma_fixed_host"] = data
            stage = int(data["stage"])
            tensor = int(data["tensor"])
            page = int(data["page"])
            if (
                (focus_stage is None or stage == focus_stage)
                and (focus_tensor is None or tensor == focus_tensor)
                and (focus_page is None or page == focus_page)
            ):
                focus_page_phases.append(data["phase"])
                focus_page_lines.append(line)
            continue

        match = DMA_EXPORT_HOST_RE.search(line)
        if match:
            data = {k: v for k, v in match.groupdict().items() if v is not None}
            data["line"] = line
            data["line_idx"] = str(idx)
            summary["dma_export_host"] = data
            continue

        match = DMA_FIXED_SUBMIT_RE.search(line)
        if match:
            data = {k: v for k, v in match.groupdict().items() if v is not None}
            data["line"] = line
            data["line_idx"] = str(idx)
            summary["dma_fixed_submit"] = data
            if data.get("phase") == "doneflag-end":
                last_doneflag_end_tok = int(data["tok"])
            continue

        match = DMA_FIXED_WAIT_RE.search(line)
        if match:
            data = {k: v for k, v in match.groupdict().items() if v is not None}
            data["line"] = line
            data["line_idx"] = str(idx)
            summary["dma_fixed_wait"] = data
            if data.get("phase") == "after-release":
                last_wait_after_release_tok = int(data["tok"])
            continue

        match = DMA_EXPORT_WAIT_RE.search(line)
        if match:
            data = {k: v for k, v in match.groupdict().items() if v is not None}
            data["line"] = line
            data["line_idx"] = str(idx)
            summary["dma_export_wait"] = data
            continue

        match = SPM_XLATE_RELEASE_RE.search(line)
        if match:
            data = {k: v for k, v in match.groupdict().items() if v is not None}
            data["line"] = line
            data["line_idx"] = str(idx)
            summary["spm_xlate_release"] = data
            continue

    summary["dma_fixed_focus_page_phases"] = focus_page_phases
    summary["dma_fixed_focus_page_lines"] = focus_page_lines
    if last_doneflag_end_tok is not None:
        summary["dma_fixed_last_doneflag_end_tok"] = str(last_doneflag_end_tok)
    if last_wait_after_release_tok is not None:
        summary["dma_fixed_last_wait_after_release_tok"] = str(last_wait_after_release_tok)
    return summary


def parse_trigger_summary(
    lines: List[str],
    *,
    post_budget: int,
    match_once: bool,
) -> Dict[str, object]:
    events: List[Dict[str, str]] = []
    seq_contiguous = True
    last_seq: Optional[int] = None
    for idx, line in enumerate(lines):
        match = TRIGGER_LINE_RE.search(line)
        if not match:
            continue
        data = {k: v for k, v in match.groupdict().items() if v is not None}
        data["line"] = line
        data["line_idx"] = str(idx)
        events.append(data)
        try:
            seq = int(data["seq"])
        except ValueError:
            seq_contiguous = False
            continue
        if last_seq is not None and seq != last_seq + 1:
            seq_contiguous = False
        last_seq = seq

    if not events:
        return {}

    line_count = len(events)
    budget_exhausted = (
        match_once and post_budget >= 0 and line_count == (post_budget + 1)
    )
    return {
        "line_count": str(line_count),
        "seq_contiguous": "yes" if seq_contiguous else "no",
        "budget_exhausted": "yes" if budget_exhausted else "no",
        "first_event": events[0],
        "last_event": events[-1],
    }


def format_sparse_item(label: str, data: Dict[str, str] | None) -> str:
    if not data:
        return f"{label}: n/a"
    filtered = {k: v for k, v in data.items() if k not in ("line", "line_idx")}
    details = " ".join(f"{k}={v}" for k, v in filtered.items())
    return f"{label}: {details}"


def format_sparse_list(label: str, values: List[str]) -> str:
    if not values:
        return f"{label}: n/a"
    return f"{label}: {' -> '.join(values)}"


def format_trigger_item(label: str, data: Dict[str, str] | None) -> str:
    if not data:
        return f"{label}: n/a"
    return (
        f"{label}: seq={data.get('seq', 'n/a')} fam={data.get('fam', 'n/a')} "
        f"ph={data.get('phase', 'n/a')} seg={data.get('seg', 'n/a')} "
        f"gs={data.get('gs', 'n/a')} ls={data.get('ls', 'n/a')} "
        f"sb={data.get('sb', 'n/a')} mgr={data.get('mgr', 'n/a')} "
        f"tn={data.get('tn', 'n/a')} pg={data.get('pg', 'n/a')} "
        f"tok={data.get('tok', 'n/a')} rc={data.get('rc', 'n/a')}"
    )


def load_last_breadcrumb(path: Path) -> tuple[breadcrumb.BreadcrumbFile, Optional[breadcrumb.BreadcrumbSlot]]:
    data = breadcrumb.decode(str(path))
    if data.magic != breadcrumb.PRT_BREADCRUMB_MAGIC:
        raise RuntimeError(f"unexpected breadcrumb magic 0x{data.magic:x}")
    if data.last_slot_idx >= data.slot_count:
        return data, None
    slot = data.slots[data.last_slot_idx]
    if not breadcrumb.slot_is_populated(slot):
        return data, None
    return data, slot


def classify_frontier(
    slot: Optional[breadcrumb.BreadcrumbSlot],
    sparse_summary: Dict[str, object],
) -> tuple[str, str]:
    focus_page_phases = list(sparse_summary.get("dma_fixed_focus_page_phases", []))
    last_doneflag_end_tok = sparse_summary.get("dma_fixed_last_doneflag_end_tok")
    last_wait_after_release_tok = sparse_summary.get("dma_fixed_last_wait_after_release_tok")

    if focus_page_phases:
        phases = set(focus_page_phases)
        if "v2p-begin" in phases and "v2p-end" not in phases:
            return (
                "inside fixed-load host-v2p",
                "Inspect `prt_host_virt_to_phys()` and, if needed, enable only page-scoped `host-v2p` deep logs.",
            )
        if "submitwait-begin" in phases and "submitwait-end" not in phases:
            return (
                "inside fixed-load submit/wait",
                "Read `dma_submit_wait_annotated_scoped()` and `dma_completion_flag_acquire()` before widening logs.",
            )
        if "submitwait-end" in phases:
            return (
                "after fixed-load page submit/wait return",
                "The focused page already retired; move the frontier forward instead of adding more page-local logs.",
            )

    if last_wait_after_release_tok and (
        last_doneflag_end_tok is None or int(last_doneflag_end_tok) < int(last_wait_after_release_tok)
    ):
        return (
            "between fixed-load wait-after-release and the next doneflag-end",
            "Prioritize the path after `dma-fixed-load-wait phase=after-release` and before the following `doneflag-end`.",
        )

    spm_xlate_release = sparse_summary.get("spm_xlate_release")
    if isinstance(spm_xlate_release, dict):
        phase = spm_xlate_release.get("phase", "n/a")
        if phase == "restore-begin":
            return (
                "inside spm-xlate restore path",
                "Read `prt_spm_xlate_release_scope()` first, then use the existing `spm-xlate` trigger family to isolate `restore-begin -> restore-end`.",
            )
        if phase == "release-end":
            return (
                "between spm-xlate release-end and restore-end",
                "Inspect the `rr_restore_opcode_binding(3U, prev_binding)` boundary before adding wider logs.",
            )
        return (
            f"inside spm-xlate release path ({phase})",
            "Read `prt_spm_xlate_release_scope()` before widening logs; keep the next rerun scoped to the `spm-xlate` trigger family.",
        )

    if slot is None:
        if sparse_summary.get("pointwise_matmul"):
            return (
                "late pointwise/Gemmini candidate",
                "Read the pointwise/Gemmini code path first; add breadcrumb or deep log only for the missing inner boundary.",
            )
        return (
            "insufficient breadcrumb evidence",
            "Confirm capture freshness and use the sparse log to choose the exact code path before adding logs.",
        )

    if slot.kind == 5:
        if slot.phase == 400:
            return (
                "between pointwise caller entry and inner matmul entry",
                "Inspect the path between `conv_call_for_manager_sync_strided()` and `prt_run_pointwise_matmul_fallback_*()` before widening logs.",
            )
        if slot.phase == 402:
            return (
                "inside or below tiled_matmul_nn_stride_auto()",
                "Prioritize Gemmini inner-call static analysis, especially the OS bias/mvin3 path and post-call fence ordering.",
            )
        if slot.phase == 403:
            return (
                "after inner matmul return but before pointwise caller return",
                "Inspect the pointwise post-call path and the subsequent fence/drain/release boundary.",
            )
        if slot.phase == 401:
            return (
                "after pointwise caller return",
                "Inspect the caller-side fence/drain/release path rather than the pointwise inner call.",
            )
        if slot.phase == 404:
            return (
                "before caller-side rr_fence",
                "Inspect the boundary between `issue-done` and `prt_rr_fence_scope()` before adding broader logs.",
            )
        if slot.phase == 405:
            return (
                "after caller-side rr_fence",
                "Inspect the path between `prt_rr_fence_scope()` and `gemmini_fence()`.",
            )
        if slot.phase == 406:
            return (
                "after caller-side gemmini_fence",
                "Inspect `flush_scope_after_drain()` next; the inner pointwise matmul and outer rr_fence already returned.",
            )
        if slot.phase == 407:
            return (
                "after caller-side drain return",
                "Inspect the final scope release/consumer handoff path instead of the pointwise drain itself.",
            )
        if slot.phase == 408:
            return (
                "after caller-side release return",
                "The pointwise caller-side fence/drain/release path completed; inspect the next outer caller boundary.",
            )

    if slot.kind == 4 and slot.phase == 306:
        if sparse_summary.get("pointwise_dispatch") or sparse_summary.get("pointwise_matmul"):
            return (
                "rr breadcrumb alone is not enough; frontier is already in the late pointwise/Gemmini window",
                "Do not treat `rr_acquire_after_call` as proof of an RR stall. Read the pointwise code path first, then add only the narrowest missing breadcrumb.",
            )
        return (
            "after rr acquire wrapper",
            "Read the acquire caller and its immediate post-acquire path before adding any logs.",
        )

    if slot.kind == 2 and slot.phase == 112:
        return (
            "dma page boundary",
            "Inspect the exact page/target window before adding more export probes.",
        )

    if slot.kind == 2 and slot.phase == 124:
        return (
            "before DMA done-flag polling loop",
            "If the next capture stays here, verify the fail-fast env made it into the image and whether the poll loop itself is being reached.",
        )

    if slot.kind == 2 and slot.phase == 125:
        return (
            "after DMA done-flag poll success",
            "The DMA completion flag became visible; inspect the following shared fence/release path if the runtime still hangs.",
        )

    if slot.kind == 2 and slot.phase == 126:
        return (
            "DMA completion flag poll timeout",
            "This localizes the stall before DMA completion visibility; prioritize the programmed src/dst/done PA and DMA engine acceptance/completion state.",
        )

    kind_name = breadcrumb.KIND_NAMES.get(slot.kind, f"kind_{slot.kind}")
    phase_name = breadcrumb.PHASE_NAMES.get(slot.phase, f"phase_{slot.phase}")
    return (
        f"frontier at {kind_name}/{phase_name}",
        "Use this boundary to choose the exact code path; add logs only if breadcrumb still cannot separate adjacent steps.",
    )


def frontier_summary(
    slot: Optional[breadcrumb.BreadcrumbSlot],
    sparse_summary: Dict[str, object],
    verdict: str,
) -> str:
    if sparse_summary.get("spm_xlate_release"):
        data = sparse_summary["spm_xlate_release"]
        return "spm-xlate-release mgr={mgr} cfg={cfg} phase={phase}".format(**data)
    if sparse_summary.get("dma_fixed_host"):
        data = sparse_summary["dma_fixed_host"]
        return "dma-fixed-load stage={stage} tensor={tensor} page={page} phase={phase}".format(**data)
    if sparse_summary.get("dma_fixed_wait"):
        data = sparse_summary["dma_fixed_wait"]
        return "dma-fixed-load stage={stage} tensor={tensor} token={tok} phase={phase}".format(**data)
    if sparse_summary.get("dma_export_wait"):
        data = sparse_summary["dma_export_wait"]
        return "dma-export stage={stage} tensor={tensor} token={tok} phase={phase}".format(**data)
    if slot is not None and slot.kind in (2, 4, 5):
        kind_name = breadcrumb.KIND_NAMES.get(slot.kind, f"kind_{slot.kind}")
        phase_name = breadcrumb.PHASE_NAMES.get(slot.phase, f"phase_{slot.phase}")
        return (
            f"{kind_name}/{phase_name}"
            f" seg={breadcrumb.fmt_u32(slot.segment_idx)}"
            f" gstage={breadcrumb.fmt_u32(slot.global_stage_id)}"
            f" lstage={breadcrumb.fmt_u32(slot.local_stage_id)}"
            f" sb={breadcrumb.fmt_u32(slot.subbatch_id)}"
            f" mgr={breadcrumb.fmt_u32(slot.manager_id)}"
            f" tensor={breadcrumb.fmt_u32(slot.tensor_id)}"
            f" page={breadcrumb.fmt_u32(slot.page_idx)}"
            f" tok={breadcrumb.fmt_u32(slot.token_id)}"
        )
    sparse_key, sparse_data = latest_sparse_item(
        sparse_summary,
        "worker",
        "pointwise_dispatch",
        "pointwise_matmul",
        "oc_split_pointwise",
        "export_target",
        "dma_export_host",
        "dma_export_wait",
        "dma_fixed_submit",
    )
    if sparse_key == "worker" and sparse_data:
        return "worker stage={stage} subbatch={subbatch} state={state}".format(**sparse_data)
    if sparse_key == "pointwise_dispatch" and sparse_data:
        return "pointwise-dispatch stage={stage} mgr={mgr} rc={rc}".format(
            rc=sparse_data.get("rc", "n/a"),
            **sparse_data,
        )
    if sparse_key == "pointwise_matmul" and sparse_data:
        return "pointwise-matmul stage={stage} mgr={mgr} state={state}".format(**sparse_data)
    if sparse_key == "oc_split_pointwise" and sparse_data:
        return "oc-split-pointwise stage={stage} mgr={mgr} tile={tile}/{tiles} state={state}".format(**sparse_data)
    if sparse_key == "export_target" and sparse_data:
        return "export-target stage={stage} tensor={tensor} seq={seq} state={state}".format(**sparse_data)
    if sparse_key == "dma_export_host" and sparse_data:
        return "dma-export stage={stage} tensor={tensor} page={page} phase={phase}".format(**sparse_data)
    if slot is not None:
        kind_name = breadcrumb.KIND_NAMES.get(slot.kind, f"kind_{slot.kind}")
        phase_name = breadcrumb.PHASE_NAMES.get(slot.phase, f"phase_{slot.phase}")
        return (
            f"{kind_name}/{phase_name}"
            f" seg={breadcrumb.fmt_u32(slot.segment_idx)}"
            f" gstage={breadcrumb.fmt_u32(slot.global_stage_id)}"
            f" lstage={breadcrumb.fmt_u32(slot.local_stage_id)}"
            f" sb={breadcrumb.fmt_u32(slot.subbatch_id)}"
            f" mgr={breadcrumb.fmt_u32(slot.manager_id)}"
            f" tensor={breadcrumb.fmt_u32(slot.tensor_id)}"
            f" page={breadcrumb.fmt_u32(slot.page_idx)}"
            f" tok={breadcrumb.fmt_u32(slot.token_id)}"
        )
    return verdict


def classify_claim(
    frontier: str,
    previous_frontier: str,
    change_kind: str,
) -> tuple[str, str, str]:
    if change_kind == "observability":
        return ("observability_only", "high", "yes")
    if change_kind == "hardware":
        return ("hardware_candidate", "high", "yes")
    if change_kind == "semantic_software":
        if previous_frontier and previous_frontier != frontier:
            return ("semantic_candidate_frontier_moved", "medium", "no")
        return ("semantic_candidate", "medium", "yes")
    if previous_frontier and previous_frontier and previous_frontier != frontier:
        return ("frontier_moved_without_repo_change", "medium", "yes")
    return ("stable_observation", "low", "no")


def emit_trigger_env(
    slot: Optional[breadcrumb.BreadcrumbSlot],
    sparse_summary: Dict[str, object],
) -> List[str]:
    env = {
        "PIPELINE_RUNTIME_DEBUG_TRIGGER_ENABLE": "1",
        "PIPELINE_RUNTIME_DEBUG_TRIGGER_PRE_RING": "8",
        "PIPELINE_RUNTIME_DEBUG_TRIGGER_POST_BUDGET": "32",
        "PIPELINE_RUNTIME_DEBUG_TRIGGER_MATCH_ONCE": "1",
        "PIPELINE_RUNTIME_DEBUG_TRIGGER_LOG_PATH": "/root/pipeline-runtime-debug/bertmini-batch8.trigger.log",
    }

    if sparse_summary.get("dma_fixed_host"):
        data = sparse_summary["dma_fixed_host"]
        env["PIPELINE_RUNTIME_DEBUG_TRIGGER_KIND"] = "dma-fixed-load"
        env["PIPELINE_RUNTIME_DEBUG_TRIGGER_GLOBAL_STAGE"] = data["stage"]
        env["PIPELINE_RUNTIME_DEBUG_TRIGGER_LOCAL_STAGE"] = data["stage"]
        env["PIPELINE_RUNTIME_DEBUG_TRIGGER_TENSOR_ID"] = data["tensor"]
        env["PIPELINE_RUNTIME_DEBUG_TRIGGER_PAGE"] = data["page"]
    elif sparse_summary.get("dma_fixed_wait"):
        data = sparse_summary["dma_fixed_wait"]
        env["PIPELINE_RUNTIME_DEBUG_TRIGGER_KIND"] = "dma-fixed-load"
        env["PIPELINE_RUNTIME_DEBUG_TRIGGER_GLOBAL_STAGE"] = data["stage"]
        env["PIPELINE_RUNTIME_DEBUG_TRIGGER_LOCAL_STAGE"] = data["stage"]
        env["PIPELINE_RUNTIME_DEBUG_TRIGGER_TENSOR_ID"] = data["tensor"]
        env["PIPELINE_RUNTIME_DEBUG_TRIGGER_TOKEN"] = data["tok"]
    elif sparse_summary.get("spm_xlate_release"):
        data = sparse_summary["spm_xlate_release"]
        env["PIPELINE_RUNTIME_DEBUG_TRIGGER_KIND"] = "spm-xlate"
        env["PIPELINE_RUNTIME_DEBUG_TRIGGER_MANAGER"] = data["mgr"]
        env["PIPELINE_RUNTIME_DEBUG_TRIGGER_TOKEN"] = data["cfg"]
        if slot is not None and slot.segment_idx != breadcrumb.PRT_BREADCRUMB_ANY_U32:
            env["PIPELINE_RUNTIME_DEBUG_TRIGGER_SEGMENT"] = str(slot.segment_idx)
        if slot is not None and slot.global_stage_id != breadcrumb.PRT_BREADCRUMB_ANY_U32:
            env["PIPELINE_RUNTIME_DEBUG_TRIGGER_GLOBAL_STAGE"] = str(slot.global_stage_id)
        if slot is not None and slot.local_stage_id != breadcrumb.PRT_BREADCRUMB_ANY_U32:
            env["PIPELINE_RUNTIME_DEBUG_TRIGGER_LOCAL_STAGE"] = str(slot.local_stage_id)
        if slot is not None and slot.subbatch_id != breadcrumb.PRT_BREADCRUMB_ANY_U32:
            env["PIPELINE_RUNTIME_DEBUG_TRIGGER_SUBBATCH"] = str(slot.subbatch_id)
    elif slot is not None and slot.kind == 5:
        sparse_key, sparse_data = latest_sparse_item(
            sparse_summary,
            "oc_split_pointwise",
            "pointwise_dispatch",
            "pointwise_matmul",
        )
        env["PIPELINE_RUNTIME_DEBUG_TRIGGER_KIND"] = "gemmini-pointwise"
        if slot.segment_idx != breadcrumb.PRT_BREADCRUMB_ANY_U32:
            env["PIPELINE_RUNTIME_DEBUG_TRIGGER_SEGMENT"] = str(slot.segment_idx)
        if slot.global_stage_id != breadcrumb.PRT_BREADCRUMB_ANY_U32:
            env["PIPELINE_RUNTIME_DEBUG_TRIGGER_GLOBAL_STAGE"] = str(slot.global_stage_id)
        if slot.local_stage_id != breadcrumb.PRT_BREADCRUMB_ANY_U32:
            env["PIPELINE_RUNTIME_DEBUG_TRIGGER_LOCAL_STAGE"] = str(slot.local_stage_id)
        if slot.subbatch_id != breadcrumb.PRT_BREADCRUMB_ANY_U32:
            env["PIPELINE_RUNTIME_DEBUG_TRIGGER_SUBBATCH"] = str(slot.subbatch_id)
        if sparse_key and sparse_data and "mgr" in sparse_data:
            env["PIPELINE_RUNTIME_DEBUG_TRIGGER_MANAGER"] = sparse_data["mgr"]
        elif slot.manager_id != breadcrumb.PRT_BREADCRUMB_ANY_U32:
            env["PIPELINE_RUNTIME_DEBUG_TRIGGER_MANAGER"] = str(slot.manager_id)
    elif slot is not None and slot.kind == 4:
        env["PIPELINE_RUNTIME_DEBUG_TRIGGER_KIND"] = "rr"
        if slot.global_stage_id != breadcrumb.PRT_BREADCRUMB_ANY_U32:
            env["PIPELINE_RUNTIME_DEBUG_TRIGGER_GLOBAL_STAGE"] = str(slot.global_stage_id)
        if slot.local_stage_id != breadcrumb.PRT_BREADCRUMB_ANY_U32:
            env["PIPELINE_RUNTIME_DEBUG_TRIGGER_LOCAL_STAGE"] = str(slot.local_stage_id)
        if slot.subbatch_id != breadcrumb.PRT_BREADCRUMB_ANY_U32:
            env["PIPELINE_RUNTIME_DEBUG_TRIGGER_SUBBATCH"] = str(slot.subbatch_id)
        if slot.manager_id != breadcrumb.PRT_BREADCRUMB_ANY_U32:
            env["PIPELINE_RUNTIME_DEBUG_TRIGGER_MANAGER"] = str(slot.manager_id)
        if slot.token_id != breadcrumb.PRT_BREADCRUMB_ANY_U32:
            env["PIPELINE_RUNTIME_DEBUG_TRIGGER_TOKEN"] = str(slot.token_id)
    elif slot is not None and slot.kind == 2:
        env["PIPELINE_RUNTIME_DEBUG_TRIGGER_KIND"] = (
            "dma-export"
            if slot.tensor_id == 2
            else "dma-fixed-load"
        )
        if slot.segment_idx != breadcrumb.PRT_BREADCRUMB_ANY_U32:
            env["PIPELINE_RUNTIME_DEBUG_TRIGGER_SEGMENT"] = str(slot.segment_idx)
        if slot.global_stage_id != breadcrumb.PRT_BREADCRUMB_ANY_U32:
            env["PIPELINE_RUNTIME_DEBUG_TRIGGER_GLOBAL_STAGE"] = str(slot.global_stage_id)
        if slot.local_stage_id != breadcrumb.PRT_BREADCRUMB_ANY_U32:
            env["PIPELINE_RUNTIME_DEBUG_TRIGGER_LOCAL_STAGE"] = str(slot.local_stage_id)
        if slot.subbatch_id != breadcrumb.PRT_BREADCRUMB_ANY_U32:
            env["PIPELINE_RUNTIME_DEBUG_TRIGGER_SUBBATCH"] = str(slot.subbatch_id)
        if slot.manager_id != breadcrumb.PRT_BREADCRUMB_ANY_U32:
            env["PIPELINE_RUNTIME_DEBUG_TRIGGER_MANAGER"] = str(slot.manager_id)
        if slot.tensor_id != breadcrumb.PRT_BREADCRUMB_ANY_U32:
            env["PIPELINE_RUNTIME_DEBUG_TRIGGER_TENSOR_ID"] = str(slot.tensor_id)
        if slot.page_idx != breadcrumb.PRT_BREADCRUMB_ANY_U32:
            env["PIPELINE_RUNTIME_DEBUG_TRIGGER_PAGE"] = str(slot.page_idx)
    elif sparse_summary.get("pointwise_dispatch") or sparse_summary.get("pointwise_matmul") or sparse_summary.get("oc_split_pointwise"):
        sparse_key, data = latest_sparse_item(
            sparse_summary,
            "pointwise_dispatch",
            "pointwise_matmul",
            "oc_split_pointwise",
        )
        if sparse_key and data:
            env["PIPELINE_RUNTIME_DEBUG_TRIGGER_KIND"] = "gemmini-pointwise"
            env["PIPELINE_RUNTIME_DEBUG_TRIGGER_GLOBAL_STAGE"] = data["stage"]
            env["PIPELINE_RUNTIME_DEBUG_TRIGGER_LOCAL_STAGE"] = data["stage"]
            env["PIPELINE_RUNTIME_DEBUG_TRIGGER_MANAGER"] = data["mgr"]
    elif sparse_summary.get("worker"):
        data = sparse_summary["worker"]
        env["PIPELINE_RUNTIME_DEBUG_TRIGGER_KIND"] = "runtime"
        env["PIPELINE_RUNTIME_DEBUG_TRIGGER_GLOBAL_STAGE"] = data["stage"]
        env["PIPELINE_RUNTIME_DEBUG_TRIGGER_LOCAL_STAGE"] = data["stage"]
        env["PIPELINE_RUNTIME_DEBUG_TRIGGER_SUBBATCH"] = data["subbatch"]
    elif sparse_summary.get("dma_export_host"):
        data = sparse_summary["dma_export_host"]
        env["PIPELINE_RUNTIME_DEBUG_TRIGGER_KIND"] = "dma-export"
        env["PIPELINE_RUNTIME_DEBUG_TRIGGER_GLOBAL_STAGE"] = data["stage"]
        env["PIPELINE_RUNTIME_DEBUG_TRIGGER_LOCAL_STAGE"] = data["stage"]
        env["PIPELINE_RUNTIME_DEBUG_TRIGGER_TENSOR_ID"] = data["tensor"]
        env["PIPELINE_RUNTIME_DEBUG_TRIGGER_PAGE"] = data["page"]
    elif sparse_summary.get("dma_export_wait"):
        data = sparse_summary["dma_export_wait"]
        env["PIPELINE_RUNTIME_DEBUG_TRIGGER_KIND"] = "dma-export"
        env["PIPELINE_RUNTIME_DEBUG_TRIGGER_GLOBAL_STAGE"] = data["stage"]
        env["PIPELINE_RUNTIME_DEBUG_TRIGGER_LOCAL_STAGE"] = data["stage"]
        env["PIPELINE_RUNTIME_DEBUG_TRIGGER_TENSOR_ID"] = data["tensor"]
        env["PIPELINE_RUNTIME_DEBUG_TRIGGER_TOKEN"] = data["tok"]
    else:
        env["PIPELINE_RUNTIME_DEBUG_TRIGGER_KIND"] = "runtime"

    return [f"export {key}={value}" for key, value in env.items()]


def sparse_line_idx(data: Dict[str, str] | None) -> int:
    if not data:
        return -1
    try:
        return int(data.get("line_idx", "-1"))
    except ValueError:
        return -1


def latest_sparse_item(
    sparse_summary: Dict[str, object],
    *keys: str,
) -> tuple[Optional[str], Optional[Dict[str, str]]]:
    best_key: Optional[str] = None
    best_data: Optional[Dict[str, str]] = None
    best_idx = -1
    for key in keys:
        data = sparse_summary.get(key)
        if not isinstance(data, dict):
            continue
        idx = sparse_line_idx(data)
        if idx > best_idx:
            best_idx = idx
            best_key = key
            best_data = data
    return best_key, best_data


def recommend_next_probe(
    *,
    verdict: str,
    claim_class: str,
    sparse_summary: Dict[str, object],
    slot: Optional[breadcrumb.BreadcrumbSlot],
    trigger_summary: Dict[str, object],
) -> str:
    if trigger_summary.get("budget_exhausted") == "yes":
        return "retarget-trigger-or-raise-budget-before-frontier-claim"
    if claim_class in ("observability_only", "hardware_candidate", "frontier_moved_without_repo_change"):
        return "control-rerun-first"
    if sparse_summary.get("spm_xlate_release"):
        return "static-read-spm-xlate-restore-then-sx-trigger"
    if slot is not None and slot.kind == 2:
        return "static-read-dma-page-boundary-then-page-trigger"
    if slot is not None and slot.kind == 5:
        return "static-read-pointwise-gemmini-then-gpw-trigger"
    if slot is not None and slot.kind == 4:
        return "static-read-rr-caller-boundary-then-rr-trigger"
    if sparse_summary.get("dma_fixed_host"):
        return "static-read-dma-fixed-host-then-page-trigger"
    if sparse_summary.get("dma_fixed_wait"):
        return "static-read-dma-wait-path-then-token-trigger"
    if sparse_summary.get("dma_export_host"):
        return "static-read-dma-export-host-then-page-trigger"
    if sparse_summary.get("dma_export_wait"):
        return "static-read-dma-export-wait-then-token-trigger"
    if sparse_summary.get("worker"):
        return "worker-boundary-trigger-only"
    if "insufficient breadcrumb evidence" in verdict:
        return "artifact-audit-then-breadcrumb-rerun"
    return "static-read-nearest-boundary-before-new-logs"


def main() -> int:
    args = parse_args()
    breadcrumb_path, sparse_log_path, trigger_log_path = resolve_inputs(args)

    sparse_lines: List[str] = []
    sparse_summary: Dict[str, object] = {}
    if sparse_log_path:
        sparse_lines = tail_lines(sparse_log_path, args.tail_lines)
        sparse_summary = parse_sparse_summary(
            sparse_lines,
            focus_stage=args.focus_stage,
            focus_tensor=args.focus_tensor,
            focus_page=args.focus_page,
        )

    trigger_lines: List[str] = []
    trigger_summary: Dict[str, object] = {}
    if trigger_log_path:
        trigger_lines = tail_lines(trigger_log_path, args.tail_lines)
        trigger_summary = parse_trigger_summary(
            trigger_lines,
            post_budget=args.trigger_post_budget,
            match_once=args.trigger_match_once != 0,
        )

    breadcrumb_data = None
    last_slot = None
    if breadcrumb_path:
        breadcrumb_data, last_slot = load_last_breadcrumb(breadcrumb_path)

    verdict, next_action = classify_frontier(last_slot, sparse_summary)

    if breadcrumb_path:
        print(f"breadcrumb={breadcrumb_path.resolve()}")
    else:
        print("breadcrumb=n/a")
    if sparse_log_path:
        print(f"sparse_log={sparse_log_path.resolve()}")
    else:
        print("sparse_log=n/a")
    if trigger_log_path:
        print(f"trigger_log={trigger_log_path.resolve()}")
    else:
        print("trigger_log=n/a")

    if breadcrumb_data is not None:
        print(
            "breadcrumb_last update_count={updates} last_kind={kind} last_phase={phase}".format(
                updates=breadcrumb_data.update_count,
                kind=breadcrumb.KIND_NAMES.get(breadcrumb_data.last_kind, f"kind_{breadcrumb_data.last_kind}"),
                phase=breadcrumb.PHASE_NAMES.get(breadcrumb_data.last_phase, f"phase_{breadcrumb_data.last_phase}"),
            )
        )
    if last_slot is not None:
        line = (
            "last_slot seg={seg} gstage={gstage} lstage={lstage} sb={sb} tensor={tensor} "
            "mgr={mgr} page={page} rc={rc} line={line}".format(
                seg=breadcrumb.fmt_u32(last_slot.segment_idx),
                gstage=breadcrumb.fmt_u32(last_slot.global_stage_id),
                lstage=breadcrumb.fmt_u32(last_slot.local_stage_id),
                sb=breadcrumb.fmt_u32(last_slot.subbatch_id),
                tensor=breadcrumb.fmt_u32(last_slot.tensor_id),
                mgr=breadcrumb.fmt_u32(last_slot.manager_id),
                page=breadcrumb.fmt_u32(last_slot.page_idx),
                rc=last_slot.rc,
                line=last_slot.line,
            )
        )
        if last_slot.kind == 5 and last_slot.phase in breadcrumb.GEMMINI_PHASES:
            dim_j = (last_slot.aux_u64_1 >> 32) & 0xFFFFFFFF
            dim_k = last_slot.aux_u64_1 & 0xFFFFFFFF
            fallback = breadcrumb.FALLBACK_TYPE_NAMES.get(last_slot.token_id, str(last_slot.token_id))
            line += f" dim_J={dim_j} dim_K={dim_k} fallback={fallback}"
        print(line)
    else:
        print("last_slot=n/a")

    print(format_sparse_item("sparse_worker", sparse_summary.get("worker")))
    print(format_sparse_item("sparse_pointwise_dispatch", sparse_summary.get("pointwise_dispatch")))
    print(format_sparse_item("sparse_pointwise_matmul", sparse_summary.get("pointwise_matmul")))
    print(format_sparse_item("sparse_oc_split_pointwise", sparse_summary.get("oc_split_pointwise")))
    print(format_sparse_item("sparse_export_target", sparse_summary.get("export_target")))
    print(format_sparse_item("sparse_dma_fixed_host", sparse_summary.get("dma_fixed_host")))
    print(format_sparse_item("sparse_dma_export_host", sparse_summary.get("dma_export_host")))
    print(format_sparse_item("sparse_dma_fixed_submit", sparse_summary.get("dma_fixed_submit")))
    print(format_sparse_item("sparse_dma_fixed_wait", sparse_summary.get("dma_fixed_wait")))
    print(format_sparse_item("sparse_dma_export_wait", sparse_summary.get("dma_export_wait")))
    print(format_sparse_item("sparse_spm_xlate_release", sparse_summary.get("spm_xlate_release")))
    print(
        format_sparse_list(
            "sparse_dma_fixed_focus_page_phases",
            list(sparse_summary.get("dma_fixed_focus_page_phases", [])),
        )
    )
    print(
        "sparse_dma_fixed_last_doneflag_end_tok={tok}".format(
            tok=sparse_summary.get("dma_fixed_last_doneflag_end_tok", "n/a")
        )
    )
    print(
        "sparse_dma_fixed_last_wait_after_release_tok={tok}".format(
            tok=sparse_summary.get("dma_fixed_last_wait_after_release_tok", "n/a")
        )
    )
    print(
        "trigger_line_count={count}".format(
            count=trigger_summary.get("line_count", "n/a")
        )
    )
    print(
        "trigger_seq_contiguous={value}".format(
            value=trigger_summary.get("seq_contiguous", "n/a")
        )
    )
    print(
        "trigger_budget_exhausted={value}".format(
            value=trigger_summary.get("budget_exhausted", "n/a")
        )
    )
    print(format_trigger_item("trigger_first_event", trigger_summary.get("first_event")))
    print(format_trigger_item("trigger_last_event", trigger_summary.get("last_event")))
    frontier = frontier_summary(last_slot, sparse_summary, verdict)
    claim_class, disturbance_risk, requires_control_rerun = classify_claim(
        frontier,
        args.previous_frontier,
        args.change_kind,
    )
    print(f"frontier={frontier}")
    print(f"previous_frontier={args.previous_frontier or 'n/a'}")
    print(f"change_kind={args.change_kind}")
    print(f"claim_class={claim_class}")
    print(f"disturbance_risk={disturbance_risk}")
    print(f"requires_control_rerun={requires_control_rerun}")
    print(
        "recommended_next_probe={probe}".format(
            probe=recommend_next_probe(
                verdict=verdict,
                claim_class=claim_class,
                sparse_summary=sparse_summary,
                slot=last_slot,
                trigger_summary=trigger_summary,
            )
        )
    )
    print(f"verdict={verdict}")
    print(f"next_action={next_action}")
    if trigger_summary.get("budget_exhausted") == "yes":
        print(
            "trigger_warning=trigger log exactly filled match-line + post-budget window; "
            "treat the last trigger line as a capture cutoff, not as an authoritative frontier"
        )

    if args.emit_trigger_env:
        print("trigger_env:")
        for line in emit_trigger_env(last_slot, sparse_summary):
            print(line)

    if sparse_lines:
        print("sparse_tail:")
        for line in sparse_lines[-8:]:
            print(line)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
