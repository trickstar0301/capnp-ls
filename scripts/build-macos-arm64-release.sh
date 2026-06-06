#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
  echo "Usage: $0 <capnp-version> <capnp-cxx-source-dir>" >&2
  echo "Example: $0 1.2.0 ../capnproto/c++" >&2
  exit 2
fi

capnp_version="$1"
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
build_dir="${repo_root}/build-release-macos-arm64-capnp-${capnp_version}"
dist_dir="${repo_root}/dist"
asset_name="capnp-ls-macos-arm64-capnp-${capnp_version}"

cmake -B "${build_dir}" \
  -DCAPNP_SOURCE_DIR="${capnp_source_dir}" \
  -DCAPNP_LS_BUILD_TESTS=ON \
  "${repo_root}"

cmake --build "${build_dir}"
ctest --test-dir "${build_dir}" --output-on-failure

mkdir -p "${dist_dir}"
cp "${build_dir}/capnp-ls" "${dist_dir}/${asset_name}"
chmod +x "${dist_dir}/${asset_name}"

echo "Built ${dist_dir}/${asset_name}"
echo "Upload manually with:"
echo "  gh release upload <tag> ${dist_dir}/${asset_name} --clobber"
