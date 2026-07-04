#!/bin/bash
# usage: verify-synth.sh <image> <voice-label> [text]
#
# Runs `synth` inside the given image and asserts the output is a valid,
# non-silent 16-bit PCM WAV file. A nonzero exit from `docker run` itself
# (e.g. vtwav.exe failing loudly on a watermark-strip mismatch, see
# CLAUDE.md "Watermark removal") also fails this script automatically via
# `set -e`, so that failure mode is caught without any STT tooling.
set -e

IMAGE="$1"
VOICE="$2"
TEXT="${3:-This is an automated end to end synthesis verification test.}"
SILENCE_THRESHOLD_DB="-40"
MIN_BYTES=1000

if [ -z "$IMAGE" ] || [ -z "$VOICE" ]; then
  echo "usage: verify-synth.sh <image> <voice-label> [text]" >&2
  exit 1
fi

WORKDIR=$(mktemp -d "${TMPDIR:-/tmp}/verify-synth-${VOICE}.XXXXXX")
cleanup() { rm -rf "$WORKDIR"; }
trap cleanup EXIT

WAV="$WORKDIR/out.wav"

echo "[$VOICE] running synth in $IMAGE ..."
docker run --rm --platform linux/amd64 -v "$WORKDIR:/output" "$IMAGE" synth "$TEXT" /output/out.wav

if [ ! -s "$WAV" ]; then
  echo "FAIL [$VOICE]: $IMAGE produced no output file (or it's empty)" >&2
  exit 1
fi

BYTES=$(wc -c < "$WAV" | tr -d ' ')
if [ "$BYTES" -lt "$MIN_BYTES" ]; then
  echo "FAIL [$VOICE]: $IMAGE output is only $BYTES bytes (< $MIN_BYTES), looks truncated/empty" >&2
  exit 1
fi

FILE_TYPE=$(file --brief "$WAV")
echo "[$VOICE] file: $FILE_TYPE"
case "$FILE_TYPE" in
  *"WAVE audio"*) ;;
  *) echo "FAIL [$VOICE]: $IMAGE output is not a valid WAVE file ($FILE_TYPE)" >&2; exit 1 ;;
esac
case "$FILE_TYPE" in
  *"16 bit"*) ;;
  *) echo "FAIL [$VOICE]: $IMAGE output is not 16-bit PCM ($FILE_TYPE)" >&2; exit 1 ;;
esac

VOLUME_LINE=$(ffmpeg -nostdin -i "$WAV" -af volumedetect -f null - 2>&1 | grep 'mean_volume:') || {
  echo "FAIL [$VOICE]: could not read mean_volume from ffmpeg output" >&2
  exit 1
}
MEAN_VOLUME=$(echo "$VOLUME_LINE" | sed -E 's/.*mean_volume: *(-?[0-9.]+|-?inf) dB.*/\1/')
echo "[$VOICE] mean_volume: ${MEAN_VOLUME} dB"

if [ "$MEAN_VOLUME" = "-inf" ] || [ "$MEAN_VOLUME" = "inf" ]; then
  echo "FAIL [$VOICE]: $IMAGE produced silent audio (mean_volume -inf dB)" >&2
  exit 1
fi

if awk -v m="$MEAN_VOLUME" -v t="$SILENCE_THRESHOLD_DB" 'BEGIN{exit !(m < t)}'; then
  echo "FAIL [$VOICE]: $IMAGE audio too quiet (mean_volume ${MEAN_VOLUME} dB < ${SILENCE_THRESHOLD_DB} dB)" >&2
  exit 1
fi

echo "PASS [$VOICE]: $IMAGE — valid, non-silent WAV (${BYTES} bytes, ${MEAN_VOLUME} dB)"
