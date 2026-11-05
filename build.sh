#!/usr/bin/env bash
# Builds Silinium without make / SDK samples by calling the PS3 toolchain directly.
# Usage: ./build.sh [debug|release|clean|format] [hen]
#   hen: also sign bin/<config>/Silinium_hen.sprx for PS3HEN with scetool
#
# Environment:
#   SCE_PS3_ROOT      PS3 SDK root (required)
#   PS3_HOST_DIR      toolchain dir, default: $SCE_PS3_ROOT/host-win32 (Windows) or host-linux
#   PS3_EXE_SUFFIX    tool suffix, default: .exe on Windows / when using host-win32
#   PS3_TOOL_WRAPPER  prefix for every tool call, e.g. "wine" to run host-win32 tools on Linux
#   SCETOOL           scetool binary for the hen build, default: scetool (keys in $PS3, or ./data, which is gitignored)

set -euo pipefail

CONFIG="${1:-debug}"
TARGET="${2:-}"
NAME="Silinium"
SRC_DIR="src"
OBJ_DIR="objs/$CONFIG"
OUT_DIR="bin/$CONFIG"

cd "$(dirname "$0")"

usage() {
    echo "usage: $0 [debug|release|clean|format] [hen]" >&2
    exit 1
}

case "$CONFIG" in
    debug|release) [ -z "$TARGET" ] || [ "$TARGET" = "hen" ] || usage ;;
    clean|format)  [ -z "$TARGET" ] || usage ;;
    *)             usage ;;
esac

if [ "$CONFIG" = "clean" ]; then
    rm -rf bin objs
    echo "# cleaned bin/ and objs/"
    exit 0
fi

if [ "$CONFIG" = "format" ]; then
    clang-format -i "$SRC_DIR"/*.c "$SRC_DIR"/*.h
    echo "# formatted $SRC_DIR/"
    exit 0
fi

SCETOOL="${SCETOOL:-scetool}"
if [ "$TARGET" = "hen" ] && ! command -v "$SCETOOL" >/dev/null; then
    echo "error: scetool not found ($SCETOOL), set SCETOOL" >&2
    exit 1
fi

if [ -z "${SCE_PS3_ROOT:-}" ]; then
    echo "error: SCE_PS3_ROOT is not set (path to the PS3 SDK)" >&2
    exit 1
fi

case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) DEFAULT_HOST="host-win32" ;;
    *)                    DEFAULT_HOST="host-linux" ;;
esac

HOST_DIR="${PS3_HOST_DIR:-$SCE_PS3_ROOT/$DEFAULT_HOST}"
case "$HOST_DIR" in
    *host-win32*) EXE="${PS3_EXE_SUFFIX-.exe}" ;;
    *)            EXE="${PS3_EXE_SUFFIX-}" ;;
esac
WRAP="${PS3_TOOL_WRAPPER:-}"

if [ ! -d "$HOST_DIR" ]; then
    echo "error: toolchain not found at $HOST_DIR (set PS3_HOST_DIR)" >&2
    exit 1
fi

CC="$HOST_DIR/ppu/bin/ppu-lv2-gcc$EXE"
PRX_STRIP="$HOST_DIR/bin/ppu-lv2-prx-strip$EXE"
MAKE_FSELF="$HOST_DIR/bin/make_fself$EXE"

CFLAGS=(-std=c99 -ffunction-sections -fdata-sections -fno-builtin-printf -nodefaultlibs
        -Wno-shadow -Wno-unused-parameter)
if [ "$CONFIG" = "release" ]; then
    CFLAGS+=(-O2)
else
    CFLAGS+=(-O0 -g)
fi
LDFLAGS=(-mprx -zgenprx)
LDLIBS=(-lc -lfs_stub -llv2_stub)

mkdir -p "$OBJ_DIR" "$OUT_DIR"

OBJS=()
for src in "$SRC_DIR"/*.c; do
    obj="$OBJ_DIR/$(basename "${src%.c}").o"
    echo "# compiling $src"
    $WRAP "$CC" "${CFLAGS[@]}" -c "$src" -o "$obj"
    OBJS+=("$obj")
done

PRX="$OUT_DIR/$NAME.prx"
SPRX="$OUT_DIR/$NAME.sprx"

echo "# linking $PRX"
$WRAP "$CC" "${LDFLAGS[@]}" "${OBJS[@]}" "${LDLIBS[@]}" -o "$PRX"

if [ "$CONFIG" = "release" ]; then
    echo "# stripping $PRX"
    $WRAP "$PRX_STRIP" "$PRX" -o "$PRX"
fi

echo "# signing $SPRX"
$WRAP "$MAKE_FSELF" "$PRX" "$SPRX"
echo "# done: $SPRX"

if [ "$TARGET" = "hen" ]; then
    HEN_SPRX="$OUT_DIR/${NAME}_hen.sprx"
    echo "# signing $HEN_SPRX"
    rm -f "$HEN_SPRX"
    # Same container as the HEN build of InfinityOps.sprx: retail APP SELF, key revision 4.
    "$SCETOOL" --sce-type=SELF --compress-data=TRUE --skip-sections=FALSE --self-add-shdrs=TRUE \
        --key-revision=04 --self-auth-id=1010000001000003 --self-vendor-id=01000002 --self-type=APP \
        --self-app-version=0001000000000000 --encrypt "$PRX" "$HEN_SPRX"
    # scetool can exit 0 without writing anything, e.g. when its keys are missing.
    if [ ! -f "$HEN_SPRX" ]; then
        echo "error: scetool did not write $HEN_SPRX (keys missing?)" >&2
        exit 1
    fi
    echo "# done: $HEN_SPRX"
fi
