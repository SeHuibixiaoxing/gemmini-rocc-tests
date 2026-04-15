#!/usr/bin/env python3

from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path
from typing import Any, Dict, List, Tuple


def _maybe_reexec_with_repo_python():
    chipyard_root = Path(__file__).resolve().parents[6]
    preferred = chipyard_root / ".conda-env" / "bin" / "python"
    if os.environ.get("PIPELINE_RUNTIME_AUDIT_REEXEC") == "1":
        return
    if not preferred.is_file():
        return
    if Path(sys.executable).resolve() == preferred.resolve():
        return
    env = dict(os.environ)
    env["PIPELINE_RUNTIME_AUDIT_REEXEC"] = "1"
    os.execve(str(preferred), [str(preferred), str(Path(__file__).resolve()), *sys.argv[1:]], env)


_maybe_reexec_with_repo_python()


def _add_local_site_packages():
    chipyard_root = Path(__file__).resolve().parents[6]
    version_tag = f"python{sys.version_info.major}.{sys.version_info.minor}"
    for env_name in (".conda-env", ".conda-lock-env"):
        site_dir = chipyard_root / env_name / "lib" / version_tag / "site-packages"
        site_path = str(site_dir)
        if site_dir.is_dir() and site_path not in sys.path:
            sys.path.append(site_path)


_add_local_site_packages()

try:
    import yaml as pyyaml
except ModuleNotFoundError:  # pragma: no cover
    pyyaml = None

try:
    from ruamel.yaml import YAML as RuamelYAML
except ModuleNotFoundError:  # pragma: no cover
    RuamelYAML = None


PAGE_SIZE_BYTES = 1024
RR_MAX_CFGS = 16
RR_SPM_XLATE_CFG_ID = RR_MAX_CFGS - 1
RR_STAGE_SCOPE_BUDGET = (RR_MAX_CFGS - 2) // 2
SUPPORTED_OP_TYPES = {"conv", "resadd"}
SUPPORTED_SPLIT_KINDS = {"single", "oc", "spatial", "resadd_spatial"}


def load_yaml(path: Path) -> Any:
    with path.open("r", encoding="utf-8") as f:
        if pyyaml is not None:
            return pyyaml.safe_load(f)
        if RuamelYAML is not None:
            return RuamelYAML(typ="safe").load(f)
    raise RuntimeError("no yaml loader available")


def unwrap_singleton_row(values: Any) -> Any:
    if isinstance(values, list) and len(values) == 1 and isinstance(values[0], list):
        return values[0]
    return values


def to_int_list(values: Any, *, flatten_singleton_row: bool = False) -> List[int]:
    if flatten_singleton_row:
        values = unwrap_singleton_row(values)
    if not isinstance(values, list):
        return []
    return [int(v) for v in values]


def to_str_list(values: Any, *, flatten_singleton_row: bool = False) -> List[str]:
    if flatten_singleton_row:
        values = unwrap_singleton_row(values)
    if not isinstance(values, list):
        return []
    return [str(v) for v in values]


def to_int_map(values: Any) -> Dict[int, int]:
    if not isinstance(values, dict):
        return {}
    return {int(k): int(v) for k, v in values.items()}


def build_model_layer_index(model_doc: Dict[str, Any] | None) -> Dict[int, Dict[str, Any]]:
    if not isinstance(model_doc, dict):
        return {}
    layers = model_doc.get("layers", []) or []
    index: Dict[int, Dict[str, Any]] = {}
    for layer in layers:
        if not isinstance(layer, dict):
            continue
        if "index" not in layer:
            continue
        index[int(layer["index"])] = layer
    return index


def stage_local_bytes(stage: Dict[str, Any], tensor_id: int) -> int:
    tensor_ids = to_int_list(stage.get("tensorIdList", []))
    if tensor_id not in tensor_ids:
        raise RuntimeError(f"tensor {tensor_id} missing from tensorIdList")
    slot = tensor_ids.index(tensor_id)
    local_bytes = to_int_list(stage.get("localSpmTensorBytesList", []))
    local_pages = to_int_list(stage.get("localSpmPageCountList", []))
    if slot < len(local_bytes) and int(local_bytes[slot]) > 0:
        return int(local_bytes[slot])
    if slot < len(local_pages):
        return int(local_pages[slot]) * PAGE_SIZE_BYTES
    raise RuntimeError(f"tensor {tensor_id} missing local bytes/page count")


def validate_stage_contract(
    seg_idx: int,
    stage_idx: int,
    stage: Dict[str, Any],
    target_doc: Dict[str, Any] | None,
    model_layers: Dict[int, Dict[str, Any]],
) -> Tuple[str, str, int]:
    target_num_gemmini = 0
    if isinstance(target_doc, dict):
        target_num_gemmini = int(target_doc.get("num_gemmini", 0) or 0)

    layer_ids = to_int_list(stage.get("layerIdList", []))
    if len(layer_ids) != 1:
        raise RuntimeError(f"segment {seg_idx} stage {stage_idx} layerIdList must contain exactly one layer id")
    layer_id = layer_ids[0]

    split_kind = str(stage.get("splitKind", "") or "")
    if split_kind not in SUPPORTED_SPLIT_KINDS:
        raise RuntimeError(f"segment {seg_idx} stage {stage_idx} unsupported splitKind={split_kind!r}")

    acc_util = int(stage.get("accUtil", 0) or 0)
    if acc_util <= 0:
        raise RuntimeError(f"segment {seg_idx} stage {stage_idx} invalid accUtil={acc_util}")
    if target_num_gemmini and acc_util > target_num_gemmini:
        raise RuntimeError(
            f"segment {seg_idx} stage {stage_idx} accUtil={acc_util} exceeds target num_gemmini={target_num_gemmini}"
        )

    v_acc = to_int_list(stage.get("vAccIdxList", []), flatten_singleton_row=True)
    if len(v_acc) != acc_util:
        raise RuntimeError(
            f"segment {seg_idx} stage {stage_idx} vAccIdxList length={len(v_acc)} expected={acc_util}"
        )
    if sorted(v_acc) != list(range(acc_util)):
        raise RuntimeError(
            f"segment {seg_idx} stage {stage_idx} vAccIdxList={v_acc} is not a permutation of 0..{acc_util - 1}"
        )

    p_acc = to_int_list(stage.get("pAccIdxList", []), flatten_singleton_row=True)
    if p_acc:
        if len(p_acc) != acc_util:
            raise RuntimeError(
                f"segment {seg_idx} stage {stage_idx} pAccIdxList length={len(p_acc)} expected={acc_util}"
            )
        if len(set(p_acc)) != len(p_acc):
            raise RuntimeError(f"segment {seg_idx} stage {stage_idx} pAccIdxList has duplicates: {p_acc}")
        if target_num_gemmini and any(v < 0 or v >= target_num_gemmini for v in p_acc):
            raise RuntimeError(
                f"segment {seg_idx} stage {stage_idx} pAccIdxList={p_acc} exceeds target num_gemmini={target_num_gemmini}"
            )

    tensor_ids = to_int_list(stage.get("tensorIdList", []))
    if not tensor_ids:
        raise RuntimeError(f"segment {seg_idx} stage {stage_idx} missing tensorIdList")

    local_addr = to_int_list(stage.get("localSpmTensorAddrList", []))
    local_vpage = to_int_list(stage.get("localSpmFirstVPageList", []))
    local_pages = to_int_list(stage.get("localSpmPageCountList", []))
    local_bytes = to_int_list(stage.get("localSpmTensorBytesList", []))
    for field_name, values in (
        ("localSpmTensorAddrList", local_addr),
        ("localSpmFirstVPageList", local_vpage),
        ("localSpmPageCountList", local_pages),
        ("localSpmTensorBytesList", local_bytes),
    ):
        if len(values) != len(tensor_ids):
            raise RuntimeError(
                f"segment {seg_idx} stage {stage_idx} {field_name} length={len(values)} tensor_count={len(tensor_ids)}"
            )

    page_span = int(stage.get("localSpmPageSpan", 0) or 0)
    required_span = 0
    for first_vpage, page_count in zip(local_vpage, local_pages):
        required_span = max(required_span, first_vpage + page_count)
    if page_span < required_span:
        raise RuntimeError(
            f"segment {seg_idx} stage {stage_idx} localSpmPageSpan={page_span} required>={required_span}"
        )

    entry_ids = to_int_list(stage.get("entryTensorIdList", []))
    export_ids = to_int_list(stage.get("exportTensorIdList", []))
    entry_types = to_str_list(stage.get("entryTensorTypeList", []))
    export_types = to_str_list(stage.get("exportTensorTypeList", []))
    if len(entry_ids) != len(entry_types):
        raise RuntimeError(f"segment {seg_idx} stage {stage_idx} entry tensor/type length mismatch")
    if len(export_ids) != len(export_types):
        raise RuntimeError(f"segment {seg_idx} stage {stage_idx} export tensor/type length mismatch")
    tensor_id_set = set(tensor_ids)
    for tensor_id in entry_ids + export_ids:
        if tensor_id not in tensor_id_set:
            raise RuntimeError(
                f"segment {seg_idx} stage {stage_idx} boundary tensor {tensor_id} missing from tensorIdList"
            )

    layer_doc = model_layers.get(layer_id)
    if not layer_doc:
        raise RuntimeError(f"segment {seg_idx} stage {stage_idx} references missing model layer {layer_id}")
    op_type = str(layer_doc.get("type", "") or "")
    if op_type not in SUPPORTED_OP_TYPES:
        raise RuntimeError(f"segment {seg_idx} stage {stage_idx} model layer {layer_id} unsupported type={op_type!r}")
    if op_type == "conv" and split_kind not in {"single", "oc", "spatial"}:
        raise RuntimeError(
            f"segment {seg_idx} stage {stage_idx} conv layer {layer_id} incompatible splitKind={split_kind!r}"
        )
    if op_type == "resadd" and split_kind not in {"single", "spatial", "resadd_spatial"}:
        raise RuntimeError(
            f"segment {seg_idx} stage {stage_idx} resadd layer {layer_id} incompatible splitKind={split_kind!r}"
        )

    model_tensor_ids = set(to_int_list(layer_doc.get("tensorIds", [])))
    stage_boundary_ids = set(entry_ids + export_ids)
    if not stage_boundary_ids.issubset(model_tensor_ids):
        raise RuntimeError(
            f"segment {seg_idx} stage {stage_idx} boundary tensors {sorted(stage_boundary_ids)} not subset of model layer tensors {sorted(model_tensor_ids)}"
        )

    return split_kind, op_type, acc_util


def audit_segment(
    seg_idx: int,
    seg: Dict[str, Any],
    target_doc: Dict[str, Any] | None,
    model_layers: Dict[int, Dict[str, Any]],
) -> Tuple[Dict[str, int], Dict[str, int], Dict[str, int], Dict[str, int]]:
    tensor_stats: Dict[int, Dict[str, Any]] = {}
    entry_type_coverage: Dict[str, int] = {}
    binding_kind_coverage: Dict[str, int] = {}
    split_kind_coverage: Dict[str, int] = {}
    op_type_coverage: Dict[str, int] = {}
    stages = seg.get("stages", []) or []
    target_num_gemmini = 0
    if isinstance(target_doc, dict):
        target_num_gemmini = int(target_doc.get("num_gemmini", 0) or 0)

    if len(stages) > RR_STAGE_SCOPE_BUDGET:
        raise RuntimeError(
            f"segment {seg_idx} has {len(stages)} stages, exceeds RR cfg budget {RR_STAGE_SCOPE_BUDGET} "
            f"(cfg {RR_SPM_XLATE_CFG_ID} reserved for spm_xlate)"
        )

    if stages:
        first_stage = stages[0][0]
        last_stage = stages[-1][0]
        boundary_all_ring = []
        for tensor_id, tensor_type in zip(
            to_int_list(first_stage.get("entryTensorIdList", [])),
            [str(v) for v in (first_stage.get("entryTensorTypeList", []) or [])],
        ):
            if tensor_type == "ALL_RINGBUFFER":
                boundary_all_ring.append(int(tensor_id))
        for tensor_id, tensor_type in zip(
            to_int_list(last_stage.get("exportTensorIdList", [])),
            [str(v) for v in (last_stage.get("exportTensorTypeList", []) or [])],
        ):
            if tensor_type == "ALL_RINGBUFFER":
                boundary_all_ring.append(int(tensor_id))
        if boundary_all_ring:
            raise RuntimeError(f"segment {seg_idx} boundary tensors use ALL_RINGBUFFER: {sorted(set(boundary_all_ring))}")

    for stage_group in stages:
        if not isinstance(stage_group, list) or len(stage_group) != 1 or not isinstance(stage_group[0], dict):
            raise RuntimeError(f"segment {seg_idx} invalid stage group structure")
        stage = stage_group[0]
        for field_name, id_name, is_entry in (
            ("entryTensorTypeList", "entryTensorIdList", True),
            ("exportTensorTypeList", "exportTensorIdList", False),
        ):
            tensor_types = [str(v) for v in (stage.get(field_name, []) or [])]
            tensor_ids = to_int_list(stage.get(id_name, []))
            if len(tensor_types) != len(tensor_ids):
                raise RuntimeError(f"segment {seg_idx} stage {stage.get('globalStageId', '?')} mismatched {field_name}")
            for tensor_id, tensor_type in zip(tensor_ids, tensor_types):
                entry_type_coverage[tensor_type] = entry_type_coverage.get(tensor_type, 0) + 1
                local_bytes = stage_local_bytes(stage, tensor_id)
                info = tensor_stats.setdefault(
                    int(tensor_id),
                    {
                        "types": set(),
                        "min_bytes": local_bytes,
                        "max_bytes": local_bytes,
                    },
                )
                info["types"].add(tensor_type)
                info["min_bytes"] = min(int(info["min_bytes"]), local_bytes)
                info["max_bytes"] = max(int(info["max_bytes"]), local_bytes)
                if tensor_type == "ALL_RINGBUFFER":
                    if is_entry and stage is first_stage:
                        raise RuntimeError(f"segment {seg_idx} tensor {tensor_id} first-stage entry cannot be ALL_RINGBUFFER")
                    if (not is_entry) and stage is last_stage:
                        raise RuntimeError(f"segment {seg_idx} tensor {tensor_id} last-stage export cannot be ALL_RINGBUFFER")

    transport_effective = to_int_map(seg.get("transport_effective_bytes", {}))
    ring_slot_effective = to_int_map(seg.get("ring_slot_effective_bytes", {}))
    ring_count = to_int_map(seg.get("ring_buffer_count", {}))
    ring_size_per = to_int_map(seg.get("ring_buffer_size_per", {}))

    total_acc_util = 0
    for stage_idx, stage_group in enumerate(stages):
        if not isinstance(stage_group, list) or len(stage_group) != 1 or not isinstance(stage_group[0], dict):
            raise RuntimeError(f"segment {seg_idx} invalid stage group structure")
        stage = stage_group[0]
        split_kind, op_type, acc_util = validate_stage_contract(seg_idx, stage_idx, stage, target_doc, model_layers)
        total_acc_util += acc_util
        split_kind_coverage[split_kind] = split_kind_coverage.get(split_kind, 0) + 1
        op_type_coverage[op_type] = op_type_coverage.get(op_type, 0) + 1
    if target_num_gemmini and total_acc_util > target_num_gemmini:
        raise RuntimeError(
            f"segment {seg_idx} total accUtil={total_acc_util} exceeds target num_gemmini={target_num_gemmini}"
        )

    binding_ids = to_int_list(seg.get("bufferBindingIdList", []))
    binding_tensor_ids = to_int_list(seg.get("bufferBindingTensorIdList", []))
    binding_kinds = [str(v) for v in (seg.get("bufferBindingKindList", []) or [])]
    binding_slot_counts = to_int_list(seg.get("bufferBindingSlotCountList", []))
    binding_pages = to_int_list(seg.get("bufferBindingPagesPerSlotList", []))
    if not (len(binding_ids) == len(binding_tensor_ids) == len(binding_kinds) == len(binding_slot_counts) == len(binding_pages)):
        raise RuntimeError(f"segment {seg_idx} buffer binding list length mismatch")
    ring_bindings: Dict[int, Tuple[int, int]] = {}
    for tensor_id, kind, slot_count, pages_per_slot in zip(binding_tensor_ids, binding_kinds, binding_slot_counts, binding_pages):
        binding_kind_coverage[kind] = binding_kind_coverage.get(kind, 0) + 1
        if kind == "RING":
            ring_bindings[int(tensor_id)] = (int(slot_count), int(pages_per_slot))

    for tensor_id, info in sorted(tensor_stats.items()):
        tensor_types = set(info["types"])
        min_bytes = int(info["min_bytes"])
        max_bytes = int(info["max_bytes"])
        if "ALL_RINGBUFFER" in tensor_types:
            if tensor_types != {"ALL_RINGBUFFER"}:
                raise RuntimeError(f"segment {seg_idx} tensor {tensor_id} mixes ALL_RINGBUFFER with {sorted(tensor_types)}")
            if ring_slot_effective.get(tensor_id) != max_bytes:
                raise RuntimeError(
                    f"segment {seg_idx} tensor {tensor_id} ring_slot_effective_bytes={ring_slot_effective.get(tensor_id)} expected={max_bytes}"
                )
            if tensor_id in transport_effective:
                raise RuntimeError(f"segment {seg_idx} tensor {tensor_id} unexpectedly has transport_effective_bytes")
        elif "ISOLATE_SPM" in tensor_types or "DRAM_DEPEN" in tensor_types:
            if transport_effective.get(tensor_id) != min_bytes:
                raise RuntimeError(
                    f"segment {seg_idx} tensor {tensor_id} transport_effective_bytes={transport_effective.get(tensor_id)} expected={min_bytes}"
                )
            if tensor_id in ring_slot_effective:
                raise RuntimeError(f"segment {seg_idx} tensor {tensor_id} unexpectedly has ring_slot_effective_bytes")
        else:
            if tensor_id in transport_effective or tensor_id in ring_slot_effective:
                raise RuntimeError(f"segment {seg_idx} tensor {tensor_id} has unexpected explicit byte contract")

    for tensor_id, count in sorted(ring_count.items()):
        if tensor_id not in tensor_stats:
            raise RuntimeError(f"segment {seg_idx} tensor {tensor_id} ring_buffer_count without tensor stats")
        info = tensor_stats[tensor_id]
        tensor_types = set(info["types"])
        if "ALL_RINGBUFFER" in tensor_types:
            expected_pages = (int(info["max_bytes"]) + PAGE_SIZE_BYTES - 1) // PAGE_SIZE_BYTES
        elif "ISOLATE_SPM" in tensor_types or "DRAM_DEPEN" in tensor_types:
            expected_pages = (int(info["min_bytes"]) + PAGE_SIZE_BYTES - 1) // PAGE_SIZE_BYTES
        else:
            raise RuntimeError(f"segment {seg_idx} tensor {tensor_id} has unexpected ring tensor types {sorted(tensor_types)}")
        if ring_size_per.get(tensor_id) != expected_pages:
            raise RuntimeError(
                f"segment {seg_idx} tensor {tensor_id} ring_buffer_size_per={ring_size_per.get(tensor_id)} expected={expected_pages}"
            )
        if tensor_id not in ring_bindings:
            raise RuntimeError(f"segment {seg_idx} tensor {tensor_id} missing RING binding")
        slot_count, pages_per_slot = ring_bindings[tensor_id]
        if slot_count != count or pages_per_slot != expected_pages:
            raise RuntimeError(
                f"segment {seg_idx} tensor {tensor_id} ring binding slot/pages={slot_count}/{pages_per_slot} expected={count}/{expected_pages}"
            )

    return entry_type_coverage, binding_kind_coverage, split_kind_coverage, op_type_coverage


def audit_pipeline(
    pipeline_doc: Dict[str, Any],
    hardware_doc: Dict[str, Any] | None,
    model_doc: Dict[str, Any] | None,
    expect_target_key: str | None,
) -> None:
    pipeline_target_key = str(pipeline_doc.get("target_key", "") or "")
    if expect_target_key and pipeline_target_key != expect_target_key:
        raise RuntimeError(f"pipeline target_key={pipeline_target_key} expected={expect_target_key}")
    if hardware_doc is not None:
        hardware_target_key = str(hardware_doc.get("target_key", "") or "")
        if hardware_target_key and pipeline_target_key != hardware_target_key:
            raise RuntimeError(f"pipeline target_key={pipeline_target_key} hardware target_key={hardware_target_key}")
        if isinstance(pipeline_doc.get("target"), dict) and isinstance(hardware_doc.get("target"), dict):
            if pipeline_doc["target"] != hardware_doc["target"]:
                raise RuntimeError("pipeline target body does not match hardware target body")

    entry_coverage_total: Dict[str, int] = {}
    binding_coverage_total: Dict[str, int] = {}
    split_coverage_total: Dict[str, int] = {}
    op_coverage_total: Dict[str, int] = {}
    model_layers = build_model_layer_index(model_doc)
    target_doc = pipeline_doc.get("target", {}) if isinstance(pipeline_doc.get("target"), dict) else {}
    segments = pipeline_doc.get("segments", []) or []
    for seg_idx, seg in enumerate(segments):
        entry_cov, binding_cov, split_cov, op_cov = audit_segment(seg_idx, seg, target_doc, model_layers)
        for key, value in entry_cov.items():
            entry_coverage_total[key] = entry_coverage_total.get(key, 0) + value
        for key, value in binding_cov.items():
            binding_coverage_total[key] = binding_coverage_total.get(key, 0) + value
        for key, value in split_cov.items():
            split_coverage_total[key] = split_coverage_total.get(key, 0) + value
        for key, value in op_cov.items():
            op_coverage_total[key] = op_coverage_total.get(key, 0) + value

    print(f"[artifact-audit] PASS target={pipeline_target_key} segments={len(segments)}")
    print(f"[artifact-audit] tensor_type_coverage={dict(sorted(entry_coverage_total.items()))}")
    print(f"[artifact-audit] buffer_binding_coverage={dict(sorted(binding_coverage_total.items()))}")
    print(f"[artifact-audit] split_kind_coverage={dict(sorted(split_coverage_total.items()))}")
    print(f"[artifact-audit] op_type_coverage={dict(sorted(op_coverage_total.items()))}")
    print(
        f"[artifact-audit] rr_stage_scope_budget={RR_STAGE_SCOPE_BUDGET} "
        f"reserved_cfg={RR_SPM_XLATE_CFG_ID}"
    )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Static audit for pipeline-runtime artifact semantics.")
    parser.add_argument("--pipeline-yaml", required=True, help="pipeline mapping yaml to audit")
    parser.add_argument("--hardware-yaml", default="", help="optional hardware target yaml for target consistency")
    parser.add_argument("--model-yaml", default="", help="optional model.layers.yaml for op/tensor cross-checks")
    parser.add_argument("--expect-target-key", default="", help="optional expected target key")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    pipeline_yaml = Path(args.pipeline_yaml)
    hardware_yaml = Path(args.hardware_yaml) if args.hardware_yaml else None
    model_yaml = Path(args.model_yaml) if args.model_yaml else None
    pipeline_doc = load_yaml(pipeline_yaml) or {}
    hardware_doc = load_yaml(hardware_yaml) if hardware_yaml else None
    model_doc = load_yaml(model_yaml) if model_yaml else None
    audit_pipeline(pipeline_doc, hardware_doc, model_doc, args.expect_target_key or None)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"[artifact-audit] FAIL {exc}", file=sys.stderr)
        raise SystemExit(1)
