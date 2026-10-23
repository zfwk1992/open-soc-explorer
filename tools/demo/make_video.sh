#!/bin/sh
# SPDX-License-Identifier: MIT
# Part of Open SoC Explorer: records a GUI demo from the real application
# (offscreen, via an opt-in test of tst_explorergui) and encodes it as an
# H.264 MP4 and a GIF preview with ffmpeg.
#
# Usage: tools/demo/make_video.sh <build directory> <output directory> [demo]
#   demo: "tour" (default): tst_demoVideo -> open-soc-explorer-demo.*
#         "language": tst_languageDemoVideo, switching English <-> 简体中文
#         at run time -> open-soc-explorer-language.*
# Needs: a build with -DRIPES_BUILD_TESTS=ON, python3 (gem5 trace
# conversion) and ffmpeg with libx264.
set -eu

BUILD=$(cd "$1" && pwd)
OUT=$2
case "${3:-tour}" in
    tour) TEST=tst_demoVideo; NAME=open-soc-explorer-demo ;;
    language) TEST=tst_languageDemoVideo; NAME=open-soc-explorer-language ;;
    *) echo "unknown demo: $3" >&2; exit 1 ;;
esac
FRAMES="$OUT/frames"
rm -rf "$FRAMES"
mkdir -p "$FRAMES"

QT_QPA_PLATFORM=offscreen OSX_DEMO_DIR="$FRAMES" \
    OSX_LANGUAGE_DEMO_DIR="$FRAMES" "$BUILD/test/tst_explorergui" "$TEST"

ffmpeg -loglevel error -y -framerate 25 -i "$FRAMES/frame_%05d.png" \
    -c:v libx264 -preset slow -crf 20 -pix_fmt yuv420p -movflags +faststart \
    "$OUT/$NAME.mp4"

ffmpeg -loglevel error -y -i "$OUT/$NAME.mp4" \
    -vf "fps=10,scale=960:-1:flags=lanczos,split[a][b];[a]palettegen=max_colors=160:stats_mode=diff[p];[b][p]paletteuse=dither=bayer:bayer_scale=4:diff_mode=rectangle" \
    "$OUT/$NAME.gif"

ls -l "$OUT/$NAME.mp4" "$OUT/$NAME.gif"
