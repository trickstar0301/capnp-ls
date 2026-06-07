#!/usr/bin/env sh
set -eu

REPO="${CAPNP_LS_REPO:-trickstar0301/capnp-ls}"
VERSION="${CAPNP_LS_VERSION:-latest}"
INSTALL_DIR="${CAPNP_LS_INSTALL_DIR:-$HOME/.local/bin}"
CAPNP_VERSION="${CAPNP_LS_CAPNP_VERSION:-v1}"
DOWNLOAD_BASE_URL="${CAPNP_LS_DOWNLOAD_BASE_URL:-https://github.com/$REPO/releases/download}"
GITHUB_API_URL="${CAPNP_LS_GITHUB_API_URL:-https://api.github.com/repos/$REPO/releases/latest}"

need_command() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "Missing required command: $1" >&2
    exit 1
  fi
}

download() {
  url="$1"
  output="$2"

  if command -v curl >/dev/null 2>&1; then
    curl -fsSL "$url" -o "$output"
  elif command -v wget >/dev/null 2>&1; then
    wget -qO "$output" "$url"
  else
    echo "Missing required command: curl or wget" >&2
    exit 1
  fi
}

detect_os() {
  case "${CAPNP_LS_OS:-$(uname -s)}" in
    Darwin|darwin|macOS|macos) echo "macos" ;;
    Linux|linux) echo "linux" ;;
    *) echo "Unsupported OS: ${CAPNP_LS_OS:-$(uname -s)}" >&2; exit 1 ;;
  esac
}

detect_arch() {
  case "${CAPNP_LS_ARCH:-$(uname -m)}" in
    x86_64|amd64) echo "x86_64" ;;
    arm64|aarch64) echo "arm64" ;;
    *) echo "Unsupported architecture: ${CAPNP_LS_ARCH:-$(uname -m)}" >&2; exit 1 ;;
  esac
}

normalize_capnp_version() {
  case "$1" in
    v1|1|1.x|1.1.0|1.2.0|1.3.0|1.4.0) echo "v1" ;;
    v2|2|2.x|2.0-dev) echo "v2" ;;
    *) echo "Unsupported Cap'n Proto version channel: $1" >&2; exit 1 ;;
  esac
}

verify_checksum() {
  file="$1"
  checksum_file="$2"

  if command -v sha256sum >/dev/null 2>&1; then
    (cd "$(dirname "$file")" && sha256sum -c "$(basename "$checksum_file")")
    return
  fi

  need_command shasum
  expected="$(awk '{print $1}' "$checksum_file")"
  actual="$(shasum -a 256 "$file" | awk '{print $1}')"
  if [ "$expected" != "$actual" ]; then
    echo "Checksum mismatch" >&2
    echo "Expected: $expected" >&2
    echo "Actual:   $actual" >&2
    exit 1
  fi
}

validate_platform() {
  os_name="$1"
  arch_name="$2"

  case "${os_name}-${arch_name}" in
    linux-x86_64|macos-arm64) ;;
    *)
      echo "Unsupported release platform: ${os_name}-${arch_name}" >&2
      echo "Published binaries are currently available for linux-x86_64 and macos-arm64." >&2
      exit 1
      ;;
  esac
}

if [ "$VERSION" = "latest" ]; then
  tmp_latest="$(mktemp)"
  download "$GITHUB_API_URL" "$tmp_latest"
  VERSION="$(sed -n 's/.*"tag_name": *"\([^"]*\)".*/\1/p' "$tmp_latest" | head -n 1)"
  rm -f "$tmp_latest"
  if [ -z "$VERSION" ]; then
    echo "Could not resolve latest release version." >&2
    exit 1
  fi
fi

OS_NAME="$(detect_os)"
ARCH_NAME="$(detect_arch)"
validate_platform "$OS_NAME" "$ARCH_NAME"
CAPNP_CHANNEL="$(normalize_capnp_version "$CAPNP_VERSION")"
ASSET="capnp-ls-${OS_NAME}-${ARCH_NAME}-capnp-${CAPNP_CHANNEL}"
URL="${DOWNLOAD_BASE_URL}/${VERSION}/${ASSET}"

TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

echo "Downloading ${ASSET} from ${VERSION}..."
download "$URL" "$TMP_DIR/$ASSET"
download "$URL.sha256" "$TMP_DIR/$ASSET.sha256"
verify_checksum "$TMP_DIR/$ASSET" "$TMP_DIR/$ASSET.sha256"

chmod +x "$TMP_DIR/$ASSET"
mkdir -p "$INSTALL_DIR"
mv "$TMP_DIR/$ASSET" "$INSTALL_DIR/capnp-ls"

echo "Installed capnp-ls to $INSTALL_DIR/capnp-ls"

case ":$PATH:" in
  *":$INSTALL_DIR:"*) ;;
  *)
    echo ""
    echo "Warning: $INSTALL_DIR is not in your PATH."
    echo "Add this to your shell profile:"
    echo "  export PATH=\"$INSTALL_DIR:\$PATH\""
    ;;
esac

"$INSTALL_DIR/capnp-ls" --version || true
