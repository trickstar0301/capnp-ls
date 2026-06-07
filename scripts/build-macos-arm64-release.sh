#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
  echo "Usage: $0 <capnp-channel> <capnp-cxx-source-dir>" >&2
  echo "Example: $0 v1 ../capnproto-v1.4.0/c++" >&2
  exit 2
fi

capnp_channel="$1"
capnp_source_dir="$2"

if [[ "$(uname -s)" != "Darwin" || "$(uname -m)" != "arm64" ]]; then
  echo "This release script only supports macOS arm64." >&2
  exit 1
fi

if [[ ! -f "${capnp_source_dir}/src/capnp/compiler/compiler.h" ]]; then
  echo "CAPNP_SOURCE_DIR must point to a Cap'n Proto C++ source directory." >&2
  echo "Missing: ${capnp_source_dir}/src/capnp/compiler/compiler.h" >&2
  exit 1
fi

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
build_dir="${repo_root}/build-release-macos-arm64-capnp-${capnp_channel}"
dist_dir="${repo_root}/dist"
asset_name="capnp-ls-macos-arm64-capnp-${capnp_channel}"
cxx_standard="17"

case "${capnp_channel}" in
  v1)
    cxx_standard="17"
    ;;
  v2)
    cxx_standard="23"
    ;;
  *)
    echo "Unsupported Cap'n Proto channel: ${capnp_channel}. Expected v1 or v2." >&2
    exit 1
    ;;
esac

cmake -B "${build_dir}" \
  -DCMAKE_CXX_STANDARD="${cxx_standard}" \
  -DCAPNP_SOURCE_DIR="${capnp_source_dir}" \
  -DCAPNP_LS_BUILD_TESTS=ON \
  "${repo_root}"

cmake --build "${build_dir}"
ctest --test-dir "${build_dir}" --output-on-failure

mkdir -p "${dist_dir}"
cp "${build_dir}/capnp-ls" "${dist_dir}/${asset_name}"
chmod +x "${dist_dir}/${asset_name}"
(
  cd "${dist_dir}"
  shasum -a 256 "${asset_name}" > "${asset_name}.sha256"
)

echo "Built ${dist_dir}/${asset_name}"
echo "Built ${dist_dir}/${asset_name}.sha256"
echo "Upload manually with:"
echo "  gh release upload <tag> ${dist_dir}/${asset_name} ${dist_dir}/${asset_name}.sha256 --clobber"
