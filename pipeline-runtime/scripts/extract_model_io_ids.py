#!/usr/bin/env python3
"""
Extract model input/output tensor IDs from HybridMapper layers.yaml.

Semantics match HybridMapper Model.get_model_in_out_ids():
  - model inputs: layer input IDs that are never produced by any layer output
  - model outputs: layer output IDs that are never consumed as layer input/weight
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any, Dict, Iterable, List, Sequence, Tuple


def _load_yaml(path: Path) -> Dict[str, Any]:
  data: Dict[str, Any]

  try:
    import yaml  # type: ignore

    with path.open("r", encoding="utf-8") as f:
      loaded = yaml.safe_load(f)
    if not isinstance(loaded, dict):
      raise ValueError("top-level YAML must be a mapping")
    data = loaded
    return data
  except Exception:
    pass

  try:
    from ruamel.yaml import YAML  # type: ignore

    yaml = YAML(typ="safe")
    with path.open("r", encoding="utf-8") as f:
      loaded = yaml.load(f)
    if not isinstance(loaded, dict):
      raise ValueError("top-level YAML must be a mapping")
    data = loaded
    return data
  except Exception as exc:
    raise RuntimeError(
      "failed to parse YAML (tried PyYAML and ruamel.yaml)"
    ) from exc


def _to_int_list(v: Any) -> List[int]:
  if isinstance(v, list):
    out: List[int] = []
    for x in v:
      try:
        out.append(int(x))
      except (TypeError, ValueError):
        continue
    return out
  return []


def _layer_ids(layer: Dict[str, Any]) -> Tuple[List[int], List[int], List[int]]:
  layer_type = str(layer.get("type", ""))
  tensor_ids = _to_int_list(layer.get("tensorIds", []))
  input_ids = _to_int_list(layer.get("input_ids", []))
  output_ids = _to_int_list(layer.get("output_ids", []))
  weight_ids: List[int] = []

  if layer_type == "conv":
    # HybridMapper conv tensorIds: [bias, weight, input, output]
    if len(tensor_ids) >= 2:
      weight_ids.append(tensor_ids[1])
    if not input_ids and len(tensor_ids) >= 3:
      input_ids = [tensor_ids[2]]
    if not output_ids and len(tensor_ids) >= 4:
      output_ids = [tensor_ids[3]]
  elif layer_type == "resadd":
    # HybridMapper resadd tensorIds: [input0, input1, output]
    if not input_ids and len(tensor_ids) >= 2:
      input_ids = [tensor_ids[0], tensor_ids[1]]
    if not output_ids and len(tensor_ids) >= 3:
      output_ids = [tensor_ids[2]]
  else:
    # Fallback for unknown layer types.
    if not input_ids and len(tensor_ids) >= 2:
      input_ids = tensor_ids[:-1]
    if not output_ids and len(tensor_ids) >= 1:
      output_ids = [tensor_ids[-1]]

  return input_ids, weight_ids, output_ids


def extract_model_in_out_ids(layers: Sequence[Dict[str, Any]]) -> Tuple[List[int], List[int]]:
  out_id_set = set()
  for layer in layers:
    _, _, out_ids = _layer_ids(layer)
    for out_id in out_ids:
      out_id_set.add(out_id)

  in_id_set = set()
  for layer in layers:
    in_ids, weight_ids, _ = _layer_ids(layer)
    for in_id in in_ids:
      in_id_set.add(in_id)
    for weight_id in weight_ids:
      in_id_set.add(weight_id)

  model_input_ids = set()
  model_output_ids = set()
  for layer in layers:
    in_ids, _, out_ids = _layer_ids(layer)
    for in_id in in_ids:
      if in_id not in out_id_set:
        model_input_ids.add(in_id)
    for out_id in out_ids:
      if out_id not in in_id_set:
        model_output_ids.add(out_id)

  return sorted(model_input_ids), sorted(model_output_ids)


def _write_json(path: Path, data: Dict[str, Any], pretty: bool) -> None:
  path.parent.mkdir(parents=True, exist_ok=True)
  text = json.dumps(data, indent=2 if pretty else None, ensure_ascii=True)
  if pretty:
    text += "\n"
  path.write_text(text, encoding="utf-8")


def main() -> int:
  parser = argparse.ArgumentParser(
    description="Extract model input/output tensor IDs from HybridMapper layers.yaml"
  )
  parser.add_argument("--layers-yaml", required=True, help="path to layers.yaml")
  parser.add_argument(
    "--output",
    default="-",
    help="output JSON path; use '-' to print to stdout (default: -)",
  )
  parser.add_argument("--pretty", action="store_true", help="pretty-print JSON")
  args = parser.parse_args()

  layers_yaml = Path(args.layers_yaml)
  if not layers_yaml.exists():
    raise SystemExit(f"layers.yaml not found: {layers_yaml}")

  doc = _load_yaml(layers_yaml)
  layers_obj = doc.get("layers", [])
  if not isinstance(layers_obj, list):
    raise SystemExit("invalid layers.yaml: 'layers' must be a list")

  layers: List[Dict[str, Any]] = []
  for item in layers_obj:
    if isinstance(item, dict):
      layers.append(item)

  input_ids, output_ids = extract_model_in_out_ids(layers)
  result = {
    "layers_yaml": str(layers_yaml),
    "input_ids": input_ids,
    "output_ids": output_ids,
  }

  if args.output == "-":
    print(json.dumps(result, indent=2 if args.pretty else None, ensure_ascii=True))
  else:
    _write_json(Path(args.output), result, args.pretty)
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
