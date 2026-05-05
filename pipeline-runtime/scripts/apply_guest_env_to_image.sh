#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: apply_guest_env_to_image.sh <image-path> <guest-env-path>

Replace /firemarshal.env inside an ext4 FireMarshal image using debugfs and
verify the replacement matches byte-for-byte.
EOF
}

if [[ $# -ne 2 ]]; then
  usage >&2
  exit 2
fi

image_path="$1"
guest_env_path="$2"

if [[ ! -f "${image_path}" ]]; then
  echo "[guest-env-patch] missing image: ${image_path}" >&2
  exit 1
fi

if [[ ! -f "${guest_env_path}" ]]; then
  echo "[guest-env-patch] missing guest env: ${guest_env_path}" >&2
  exit 1
fi

tmpdir="$(mktemp -d)"
trap 'rm -rf "${tmpdir}"' EXIT
verify_path="${tmpdir}/firemarshal.env"

if ! debugfs -w -R "rm /firemarshal.env" "${image_path}" >/dev/null 2>&1; then
  echo "[guest-env-patch] failed to remove existing /firemarshal.env from ${image_path}" >&2
  exit 1
fi

if ! debugfs -w -R "write ${guest_env_path} /firemarshal.env" "${image_path}" >/dev/null 2>&1; then
  echo "[guest-env-patch] failed to write /firemarshal.env into ${image_path}" >&2
  exit 1
fi

if ! debugfs -R "cat /firemarshal.env" "${image_path}" > "${verify_path}" 2>/dev/null; then
  echo "[guest-env-patch] failed to re-read /firemarshal.env from ${image_path}" >&2
  exit 1
fi

if ! cmp -s "${guest_env_path}" "${verify_path}"; then
  echo "[guest-env-patch] verification mismatch for ${image_path}" >&2
  exit 1
fi

sha="$(sha256sum "${guest_env_path}" | awk '{print $1}')"
echo "[guest-env-patch] patched ${image_path} /firemarshal.env sha256=${sha}"
