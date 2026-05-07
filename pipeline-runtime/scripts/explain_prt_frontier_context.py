#!/usr/bin/env python3
from __future__ import annotations

import argparse
from pathlib import Path
from typing import Any, Optional

import yaml

import decode_prt_breadcrumb as breadcrumb


PAGE_BYTES = 1024


def any_to_none(value: int) -> Optional[int]:
    return None if value == breadcrumb.PRT_BREADCRUMB_ANY_U32 else value


def fmt_opt(value: Optional[int]) -> str:
    return "any" if value is None else str(value)


def load_yaml(path: Path) -> Any:
    with path.open("r", encoding="utf-8") as handle:
        return yaml.safe_load(handle)


def load_frontier(args: argparse.Namespace) -> dict[str, Any]:
    if args.breadcrumb:
        data = breadcrumb.decode(str(args.breadcrumb))
        if data.magic != breadcrumb.PRT_BREADCRUMB_MAGIC:
            raise RuntimeError(f"unexpected breadcrumb magic 0x{data.magic:x}")
        if data.last_slot_idx >= data.slot_count:
            raise RuntimeError(f"last slot out of range: {data.last_slot_idx}")
        slot = data.slots[data.last_slot_idx]
        if not breadcrumb.slot_is_populated(slot):
            raise RuntimeError(f"last slot {data.last_slot_idx} is empty")
        return {
            "kind": breadcrumb.KIND_NAMES.get(slot.kind, f"kind_{slot.kind}"),
            "phase": breadcrumb.PHASE_NAMES.get(slot.phase, f"phase_{slot.phase}"),
            "segment": any_to_none(slot.segment_idx),
            "global_stage": any_to_none(slot.global_stage_id),
            "local_stage": any_to_none(slot.local_stage_id),
            "subbatch": any_to_none(slot.subbatch_id),
            "tensor": any_to_none(slot.tensor_id),
            "token": any_to_none(slot.token_id),
            "manager": any_to_none(slot.manager_id),
            "page": any_to_none(slot.page_idx),
            "rc": slot.rc,
            "flags": slot.flags,
            "src": slot.src_addr,
            "dst": slot.dst_addr,
            "aux0": slot.aux_u64_0,
            "aux1": slot.aux_u64_1,
            "line": slot.line,
        }

    return {
        "kind": args.kind,
        "phase": args.phase,
        "segment": args.segment,
        "global_stage": args.global_stage,
        "local_stage": args.local_stage,
        "subbatch": args.subbatch,
        "tensor": args.tensor,
        "token": args.token,
        "manager": args.manager,
        "page": args.page,
        "rc": None,
        "flags": None,
        "src": args.src,
        "dst": args.dst,
        "aux0": args.aux0,
        "aux1": args.aux1,
        "line": None,
    }


def stage_candidates(pipeline: dict[str, Any], frontier: dict[str, Any]) -> list[tuple[int, int, dict[str, Any], dict[str, Any]]]:
    result: list[tuple[int, int, dict[str, Any], dict[str, Any]]] = []
    want_seg = frontier.get("segment")
    want_gstage = frontier.get("global_stage")
    want_lstage = frontier.get("local_stage")
    for seg_idx, seg in enumerate(pipeline.get("segments", []) or []):
        if want_seg is not None and int(seg.get("segment_idx", seg_idx)) != want_seg:
            continue
        stage_groups = seg.get("stages", []) or []
        for local_idx, group in enumerate(stage_groups):
            if want_lstage is not None and local_idx != want_lstage:
                continue
            for stage in group or []:
                if want_gstage is not None and int(stage.get("globalStageId", -1)) != want_gstage:
                    continue
                result.append((int(seg.get("segment_idx", seg_idx)), local_idx, seg, stage))
    return result


def tensor_index(stage: dict[str, Any], tensor_id: Optional[int]) -> Optional[int]:
    if tensor_id is None:
        return None
    for idx, value in enumerate(stage.get("tensorIdList", []) or []):
        if int(value) == tensor_id:
            return idx
    return None


def tensor_roles(stage: dict[str, Any], tensor_id: Optional[int]) -> list[str]:
    if tensor_id is None:
        return []
    roles: list[str] = []
    for key, role in (
        ("entryTensorIdList", "entry"),
        ("exportTensorIdList", "export"),
        ("fixTensorDramBypassIdList", "fixed"),
        ("innerIsolateTensorId", "inner_isolate"),
        ("innerSharedTensorId", "inner_shared"),
    ):
        if tensor_id in [int(v) for v in stage.get(key, []) or []]:
            roles.append(role)
    return roles


def model_layers_for_stage(model: dict[str, Any], stage: dict[str, Any]) -> list[dict[str, Any]]:
    ids = {int(v) for v in stage.get("layerIdList", []) or []}
    return [layer for layer in model.get("layers", []) or [] if int(layer.get("index", -1)) in ids]


def dict_get_int_key(mapping: Any, key: int) -> Any:
    if not isinstance(mapping, dict):
        return None
    if key in mapping:
        return mapping[key]
    text_key = str(key)
    return mapping.get(text_key)


def emit_stage_context(
    *,
    model: dict[str, Any],
    pipeline: dict[str, Any],
    layer_mapping: Optional[dict[str, Any]],
    frontier: dict[str, Any],
) -> None:
    candidates = stage_candidates(pipeline, frontier)
    print(f"matched_stage_count={len(candidates)}")
    if not candidates:
        return

    tensor_id = frontier.get("tensor")
    page = frontier.get("page")
    for match_idx, (seg_idx, local_idx, seg, stage) in enumerate(candidates):
        print(f"stage_match[{match_idx}].segment={seg_idx}")
        print(f"stage_match[{match_idx}].local_stage={local_idx}")
        print(f"stage_match[{match_idx}].global_stage={stage.get('globalStageId', 'n/a')}")
        print(f"stage_match[{match_idx}].segment_layers={seg.get('start_layer_idx', 'n/a')}..{seg.get('end_layer_idx', 'n/a')}")
        print(f"stage_match[{match_idx}].stage_layers={stage.get('layerIdList', [])}")
        print(f"stage_match[{match_idx}].split_kind={stage.get('splitKind', 'n/a')}")
        print(f"stage_match[{match_idx}].acc_util={stage.get('accUtil', 'n/a')}")
        print(f"stage_match[{match_idx}].v_acc={stage.get('vAccIdxList', [])}")
        print(f"stage_match[{match_idx}].tensor_ids={stage.get('tensorIdList', [])}")
        print(f"stage_match[{match_idx}].entry_tensors={stage.get('entryTensorIdList', [])}")
        print(f"stage_match[{match_idx}].export_tensors={stage.get('exportTensorIdList', [])}")
        print(f"stage_match[{match_idx}].fixed_tensors={stage.get('fixTensorDramBypassIdList', [])}")

        idx = tensor_index(stage, tensor_id)
        roles = tensor_roles(stage, tensor_id)
        print(f"stage_match[{match_idx}].frontier_tensor_roles={roles or ['unknown']}")
        if idx is not None:
            addrs = stage.get("localSpmTensorAddrList", []) or []
            first_pages = stage.get("localSpmFirstVPageList", []) or []
            page_counts = stage.get("localSpmPageCountList", []) or []
            byte_counts = stage.get("localSpmTensorBytesList", []) or []
            base = int(addrs[idx]) if idx < len(addrs) else None
            first_vpage = int(first_pages[idx]) if idx < len(first_pages) else None
            page_count = int(page_counts[idx]) if idx < len(page_counts) else None
            byte_count = int(byte_counts[idx]) if idx < len(byte_counts) else None
            print(f"stage_match[{match_idx}].frontier_tensor_index={idx}")
            print(f"stage_match[{match_idx}].frontier_tensor_local_spm_base={base}")
            print(f"stage_match[{match_idx}].frontier_tensor_first_vpage={first_vpage}")
            print(f"stage_match[{match_idx}].frontier_tensor_page_count={page_count}")
            print(f"stage_match[{match_idx}].frontier_tensor_bytes={byte_count}")
            if page is not None and base is not None:
                print(f"stage_match[{match_idx}].frontier_page_byte_offset={page * PAGE_BYTES}")
                print(f"stage_match[{match_idx}].frontier_page_local_spm_addr={base + page * PAGE_BYTES}")
                if first_vpage is not None:
                    print(f"stage_match[{match_idx}].frontier_page_local_vpage={first_vpage + page}")
                if byte_count is not None:
                    remaining = max(byte_count - page * PAGE_BYTES, 0)
                    print(f"stage_match[{match_idx}].frontier_page_remaining_bytes={remaining}")
                    print(f"stage_match[{match_idx}].frontier_page_transfer_bytes={min(PAGE_BYTES, remaining)}")

        for layer in model_layers_for_stage(model, stage):
            layer_id = int(layer.get("index", -1))
            print(f"stage_match[{match_idx}].layer[{layer_id}].type={layer.get('type', 'n/a')}")
            print(f"stage_match[{match_idx}].layer[{layer_id}].input_ids={layer.get('input_ids', [])}")
            print(f"stage_match[{match_idx}].layer[{layer_id}].output_ids={layer.get('output_ids', [])}")
            print(f"stage_match[{match_idx}].layer[{layer_id}].tensor_ids={layer.get('tensorIds', [])}")
            print(f"stage_match[{match_idx}].layer[{layer_id}].param={layer.get('param', [])}")
            if tensor_id is not None:
                print(
                    f"stage_match[{match_idx}].layer[{layer_id}].frontier_tensor_size="
                    f"{dict_get_int_key(layer.get('data_size'), tensor_id)}"
                )

        if layer_mapping:
            entries = [
                entry
                for entry in layer_mapping.get("entries", []) or []
                if int(entry.get("layer_id", -1)) in {int(v) for v in stage.get("layerIdList", []) or []}
            ]
            if entries:
                entry = entries[0]
                print(f"stage_match[{match_idx}].layer_mapping_first.source_candidate_index={entry.get('source_candidate_index', 'n/a')}")
                print(f"stage_match[{match_idx}].layer_mapping_first.mapping_tile={entry.get('mapping_tile', [])}")
                print(f"stage_match[{match_idx}].layer_mapping_first.mapping_dram_bypass={entry.get('mapping_dram_bypass', [])}")
                print(f"stage_match[{match_idx}].layer_mapping_first.mapping_spm_bypass={entry.get('mapping_spm_bypass', [])}")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Explain the model/mapping context around a pipeline-runtime breadcrumb frontier."
    )
    parser.add_argument("--breadcrumb", type=Path, default=None)
    parser.add_argument("--model-yaml", type=Path, required=True)
    parser.add_argument("--pipeline-yaml", type=Path, required=True)
    parser.add_argument("--layer-mapping-yaml", type=Path, default=None)
    parser.add_argument("--kind", default="manual")
    parser.add_argument("--phase", default="manual")
    parser.add_argument("--segment", type=int, default=None)
    parser.add_argument("--global-stage", type=int, default=None)
    parser.add_argument("--local-stage", type=int, default=None)
    parser.add_argument("--subbatch", type=int, default=None)
    parser.add_argument("--tensor", type=int, default=None)
    parser.add_argument("--token", type=int, default=None)
    parser.add_argument("--manager", type=int, default=None)
    parser.add_argument("--page", type=int, default=None)
    parser.add_argument("--src", type=lambda v: int(v, 0), default=0)
    parser.add_argument("--dst", type=lambda v: int(v, 0), default=0)
    parser.add_argument("--aux0", type=lambda v: int(v, 0), default=0)
    parser.add_argument("--aux1", type=lambda v: int(v, 0), default=0)
    args = parser.parse_args()

    frontier = load_frontier(args)
    model = load_yaml(args.model_yaml)
    pipeline = load_yaml(args.pipeline_yaml)
    layer_mapping = load_yaml(args.layer_mapping_yaml) if args.layer_mapping_yaml else None

    print(f"frontier.kind={frontier['kind']}")
    print(f"frontier.phase={frontier['phase']}")
    print(f"frontier.segment={fmt_opt(frontier.get('segment'))}")
    print(f"frontier.global_stage={fmt_opt(frontier.get('global_stage'))}")
    print(f"frontier.local_stage={fmt_opt(frontier.get('local_stage'))}")
    print(f"frontier.subbatch={fmt_opt(frontier.get('subbatch'))}")
    print(f"frontier.tensor={fmt_opt(frontier.get('tensor'))}")
    print(f"frontier.token={fmt_opt(frontier.get('token'))}")
    print(f"frontier.manager={fmt_opt(frontier.get('manager'))}")
    print(f"frontier.page={fmt_opt(frontier.get('page'))}")
    print(f"frontier.rc={frontier.get('rc')}")
    print(f"frontier.flags={frontier.get('flags')}")
    print(f"frontier.src=0x{int(frontier.get('src') or 0):x}")
    print(f"frontier.dst=0x{int(frontier.get('dst') or 0):x}")
    print(f"frontier.aux0=0x{int(frontier.get('aux0') or 0):x}")
    print(f"frontier.aux1=0x{int(frontier.get('aux1') or 0):x}")
    print(f"frontier.line={frontier.get('line')}")
    print(f"pipeline.target_key={pipeline.get('target_key', 'n/a')}")
    print(f"pipeline.target={pipeline.get('target', {})}")
    emit_stage_context(
        model=model,
        pipeline=pipeline,
        layer_mapping=layer_mapping,
        frontier=frontier,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
