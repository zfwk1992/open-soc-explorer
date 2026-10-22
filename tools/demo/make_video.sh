#!/bin/sh
# SPDX-License-Identifier: MIT
# Part of Open SoC Explorer: records the GUI demo from the real application
# (offscreen, via the opt-in test tst_explorergui::tst_demoVideo) and
# encodes it as an H.264 MP4 and a GIF preview with ffmpeg.
#
# Usage: tools/demo/make_video.sh <build directory> <output directory>
# Needs: a build with -DRIPES_BUILD_TESTS=ON, python3 (gem5 trace
# conversion) and ffmpeg with libx264.
set -eu

BUILD=$(cd "$1" && pwd)
OUT=$2
FRAMES="$OUT/frames"
rm -rf "$FRAMES"
mkdir -p "$FRAMES"

QT_QPA_PLATFORM=offscreen OSX_DEMO_DIR="$FRAMES" \
    "$BUILD/test/tst_explorergui" tst_demoVideo

ffmpeg -loglevel error -y -framerate 25 -i "$FRAMES/frame_%05d.png" \
    -c:v libx264 -preset slow -crf 20 -pix_fmt yuv420p -movflags +faststart \
    "$OUT/open-soc-explorer-demo.mp4"

ffmpeg -loglevel error -y -i "$OUT/open-soc-explorer-demo.mp4" \
    -vf "fps=10,scale=960:-1:flags=lanczos,split[a][b];[a]palettegen=max_colors=160:stats_mode=diff[p];[b][p]paletteuse=dither=bayer:bayer_scale=4:diff_mode=rectangle" \
    "$OUT/open-soc-explorer-demo.gif"

ls -l "$OUT/open-soc-explorer-demo.mp4" "$OUT/open-soc-explorer-demo.gif"
