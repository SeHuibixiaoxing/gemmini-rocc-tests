#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cy_dir="$(cd "${script_dir}/../../../../../.." && pwd)"

generated_src_root="${PAIRDUMMY_NETWORK_AUDIT_GENERATED_SRC_ROOT:-${cy_dir}/sims/firesim-staging/generated-src}"
target_globs_raw="${PAIRDUMMY_NETWORK_AUDIT_TARGET_GLOB:-*GemminiLearningConfigSpadReRoCCGlobalNoC4C2x2P12x4x3CoupledDMAPairManagerDummy16x16Sbus128}"
nic_regex="${PAIRDUMMY_NETWORK_AUDIT_NIC_REGEX:-ucb-bar,ice-nic|ice-nic|nic@10016000|ethernet}"
blkdev_regex="${PAIRDUMMY_NETWORK_AUDIT_BLKDEV_REGEX:-blkdev-controller@10015000|ucb-bar,blkdev}"

if [[ ! -d "${generated_src_root}" ]]; then
  echo "network_target_audit_status=missing-generated-src-root"
  echo "network_target_audit_generated_src_root=${generated_src_root}"
  exit 2
fi

IFS=':' read -r -a target_globs <<< "${target_globs_raw}"

target_dirs=()
declare -A seen_target_dirs=()
for target_glob in "${target_globs[@]}"; do
  [[ -n "${target_glob}" ]] || continue
  while IFS= read -r target_dir; do
    [[ -n "${target_dir}" ]] || continue
    if [[ -z "${seen_target_dirs[${target_dir}]:-}" ]]; then
      target_dirs+=("${target_dir}")
      seen_target_dirs["${target_dir}"]=1
    fi
  done < <(find "${generated_src_root}" -maxdepth 1 -mindepth 1 -type d -name "${target_glob}" | sort)
done

if [[ "${#target_dirs[@]}" -eq 0 ]]; then
  echo "network_target_audit_status=missing-target-dir"
  echo "network_target_audit_generated_src_root=${generated_src_root}"
  echo "network_target_audit_target_globs=${target_globs_raw}"
  exit 2
fi

found_nic=0
inspected_dts=0

echo "network_target_audit_generated_src_root=${generated_src_root}"
echo "network_target_audit_target_globs=${target_globs_raw}"

for target_dir in "${target_dirs[@]}"; do
  echo "network_target_audit_target_dir=${target_dir}"

  mapfile -t dts_files < <(
    find "${target_dir}" -maxdepth 1 -type f -name '*.dts' | sort
  )

  if [[ "${#dts_files[@]}" -eq 0 ]]; then
    echo "network_target_audit_dts=missing"
    continue
  fi

  for dts_file in "${dts_files[@]}"; do
    inspected_dts=$((inspected_dts + 1))
    echo "network_target_audit_dts=${dts_file}"

    if grep -Eq "${blkdev_regex}" "${dts_file}"; then
      echo "network_target_audit_has_blkdev=yes"
    else
      echo "network_target_audit_has_blkdev=no"
    fi

    nic_hits="$(grep -nE "${nic_regex}" "${dts_file}" || true)"
    if [[ -n "${nic_hits}" ]]; then
      found_nic=1
      echo "network_target_audit_has_nic=yes"
      while IFS= read -r line; do
        [[ -n "${line}" ]] || continue
        echo "network_target_audit_nic_hit=${line}"
      done <<< "${nic_hits}"
    else
      echo "network_target_audit_has_nic=no"
    fi
  done
done

if [[ "${inspected_dts}" -eq 0 ]]; then
  echo "network_target_audit_status=missing-dts"
  exit 2
fi

if [[ "${found_nic}" -ne 0 ]]; then
  echo "network_target_audit_status=pass"
  echo "network_target_audit_summary=generated-target-exposes-guest-nic"
  exit 0
fi

echo "network_target_audit_status=fail"
echo "network_target_audit_summary=generated-target-has-no-guest-nic"
echo "network_target_audit_reason=current-generated-target-exposes-no-ice-nic-device-tree-node-so-guest-linux-cannot-create-a-netdev"
exit 1
