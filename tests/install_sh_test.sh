#!/usr/bin/env sh
set -eu

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
tmp_root="$(mktemp -d)"
trap 'rm -rf "$tmp_root"' EXIT

fail() {
  echo "install_sh_test: $*" >&2
  exit 1
}

write_asset() {
  asset_dir="$1"
  asset_name="$2"
  mkdir -p "$asset_dir"
  printf '#!/usr/bin/env sh\nprintf "capnp-ls test binary\\n"\n' > "$asset_dir/$asset_name"
  chmod +x "$asset_dir/$asset_name"
  if command -v sha256sum >/dev/null 2>&1; then
    (cd "$asset_dir" && sha256sum "$asset_name" > "$asset_name.sha256")
  else
    (cd "$asset_dir" && shasum -a 256 "$asset_name" > "$asset_name.sha256")
  fi
}

asset_name="capnp-ls-linux-x86_64-capnp-v1"
v2_asset_name="capnp-ls-linux-x86_64-capnp-v2"
release_dir="$tmp_root/releases/v9.9.9"
install_dir="$tmp_root/bin"
write_asset "$release_dir" "$asset_name"
write_asset "$release_dir" "$v2_asset_name"

CAPNP_LS_VERSION="v9.9.9" \
CAPNP_LS_INSTALL_DIR="$install_dir" \
CAPNP_LS_DOWNLOAD_BASE_URL="file://$tmp_root/releases" \
CAPNP_LS_OS="linux" \
CAPNP_LS_ARCH="x86_64" \
  sh "$repo_root/install.sh"

[ -x "$install_dir/capnp-ls" ] || fail "expected installed capnp-ls to be executable"
"$install_dir/capnp-ls" | grep -q "capnp-ls test binary" || fail "installed binary did not run"

bad_release_dir="$tmp_root/releases/v9.9.10"
write_asset "$bad_release_dir" "$asset_name"
printf '0000000000000000000000000000000000000000000000000000000000000000  %s\n' "$asset_name" > "$bad_release_dir/$asset_name.sha256"

if CAPNP_LS_VERSION="v9.9.10" \
  CAPNP_LS_INSTALL_DIR="$tmp_root/bad-bin" \
  CAPNP_LS_DOWNLOAD_BASE_URL="file://$tmp_root/releases" \
  CAPNP_LS_OS="linux" \
  CAPNP_LS_ARCH="x86_64" \
    sh "$repo_root/install.sh" >/dev/null 2>&1; then
  fail "expected checksum mismatch to fail"
fi

if CAPNP_LS_VERSION="v9.9.9" \
  CAPNP_LS_INSTALL_DIR="$tmp_root/unsupported-bin" \
  CAPNP_LS_DOWNLOAD_BASE_URL="file://$tmp_root/releases" \
  CAPNP_LS_OS="linux" \
  CAPNP_LS_ARCH="arm64" \
    sh "$repo_root/install.sh" >/dev/null 2>&1; then
  fail "expected unsupported release platform to fail"
fi

CAPNP_LS_VERSION="v9.9.9" \
CAPNP_LS_INSTALL_DIR="$tmp_root/compat-bin" \
CAPNP_LS_DOWNLOAD_BASE_URL="file://$tmp_root/releases" \
CAPNP_LS_OS="linux" \
CAPNP_LS_ARCH="x86_64" \
CAPNP_LS_CAPNP_VERSION="1.4.0" \
  sh "$repo_root/install.sh" >/dev/null

[ -x "$tmp_root/compat-bin/capnp-ls" ] || fail "expected 1.4.0 alias to install capnp-v1 asset"

CAPNP_LS_VERSION="v9.9.9" \
CAPNP_LS_INSTALL_DIR="$tmp_root/v2-bin" \
CAPNP_LS_DOWNLOAD_BASE_URL="file://$tmp_root/releases" \
CAPNP_LS_OS="linux" \
CAPNP_LS_ARCH="x86_64" \
CAPNP_LS_CAPNP_VERSION="2.0-dev" \
  sh "$repo_root/install.sh" >/dev/null

[ -x "$tmp_root/v2-bin/capnp-ls" ] || fail "expected 2.0-dev alias to install capnp-v2 asset"

echo "install_sh_test: ok"
