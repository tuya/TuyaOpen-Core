#!/bin/sh
# Generate the pre-encoded, rate-switchable samples used by demo_vcam.
#
# The vcam switches to the highest variant at or below the P2P rate-control
# request.  All variants must share one resolution/profile/fps/GOP and be
# I/P-only (no B-frames) with SPS/PPS repeated at every IDR; otherwise the
# App sees a jump when the buffer is swapped.
#
# Usage:
#   tools/gen_demo_variants.sh [source] [out_dir] [width] [height]
#
# Defaults: assets/demo_video.264, ".", 960, 540.  Run it from the directory
# the demo uses as its working directory (or pass that directory as out_dir)
# so the files sit next to demo_video.264.
set -eu

SRC=${1:-assets/demo_video.264}
OUT=${2:-.}
WIDTH=${3:-960}
HEIGHT=${4:-540}
FFMPEG=${FFMPEG:-ffmpeg}

if [ ! -r "$SRC" ]; then
    echo "source not readable: $SRC" >&2
    exit 1
fi
if ! command -v "$FFMPEG" >/dev/null 2>&1; then
    echo "ffmpeg not found: $FFMPEG" >&2
    exit 1
fi

mkdir -p "$OUT"

for rate in 550 500 450 350 250; do
    maxrate=$((rate * 13 / 10))
    bufsize=$((rate * 2))
    if [ "$rate" = 550 ]; then
        name=demo_video.264
    else
        name="demo_video_${rate}.264"
    fi

    echo "generating $OUT/$name (${rate} kbps, I/P only)"
    "$FFMPEG" -y -hide_banner -loglevel error -i "$SRC" \
        -vf "scale=${WIDTH}:${HEIGHT}" \
        -c:v libx264 -preset veryfast \
        -b:v "${rate}k" -maxrate "${maxrate}k" -bufsize "${bufsize}k" \
        -g 60 -keyint_min 60 -sc_threshold 0 -bf 0 \
        -x264-params "aud=1:repeat-headers=1" \
        -pix_fmt yuv420p -an "$OUT/$name"
done
