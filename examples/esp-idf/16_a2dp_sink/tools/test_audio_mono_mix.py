#!/usr/bin/env python3
import os
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "main" / "audio_mono_mix.c"
INCLUDE = ROOT / "main"


def main() -> int:
    gcc = shutil.which("gcc")
    if not gcc:
        print("gcc not found", file=sys.stderr)
        return 2

    with tempfile.TemporaryDirectory() as tmp:
        tmpdir = Path(tmp)
        test_c = tmpdir / "test_audio_mono_mix.c"
        exe = tmpdir / ("test_audio_mono_mix.exe" if os.name == "nt" else "test_audio_mono_mix")
        test_c.write_text(
            r'''
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "audio_mono_mix.h"

static int expect_frame(const uint8_t *buf, int frame, int16_t expected)
{
    int16_t left = 0;
    int16_t right = 0;
    memcpy(&left, buf + frame * 4, sizeof(left));
    memcpy(&right, buf + frame * 4 + 2, sizeof(right));
    if (left != expected || right != expected) {
        printf("frame %d expected %d/%d got %d/%d\n", frame, expected, expected, left, right);
        return 1;
    }
    return 0;
}

int main(void)
{
    audio_mono_mix_reset();

    uint8_t pcm[] = {
        0xe8, 0x03, 0x18, 0xfc, /* 1000, -1000 -> left duplicate */
        0x00, 0x40, 0x00, 0x00, /* 16384, 0 -> left duplicate */
        0xff, 0x7f, 0x00, 0x80, /* 32767, -32768 -> left duplicate */
        0x00                    /* trailing byte should be preserved */
    };

    audio_mono_mix_stereo16le_in_place(pcm, sizeof(pcm));

    if (expect_frame(pcm, 0, 1000)) {
        return 1;
    }
    if (expect_frame(pcm, 1, 16384)) {
        return 1;
    }
    if (expect_frame(pcm, 2, 32767)) {
        return 1;
    }
    if (pcm[12] != 0x00) {
        printf("trailing byte changed to 0x%02x\n", pcm[12]);
        return 1;
    }

    audio_mono_mix_set_mode(AUDIO_STEREO_MIX_RIGHT_DUPLICATE);
    uint8_t clip_pcm[] = {
        0xff, 0x7f, 0x00, 0x80,
    };
    audio_mono_mix_stereo16le_in_place(clip_pcm, sizeof(clip_pcm));
    if (expect_frame(clip_pcm, 0, -32768)) {
        return 1;
    }

    audio_mono_mix_set_mode(AUDIO_STEREO_MIX_MONO_AVERAGE);
    uint8_t mono_pcm[] = {
        0xe8, 0x03, 0xe8, 0x03, /* 1000, 1000 -> 1500 after low shelf */
        0x00, 0x40, 0x00, 0x00, /* 16384, 0 -> conditioned mono */
    };
    audio_mono_mix_stereo16le_in_place(mono_pcm, sizeof(mono_pcm));
    if (expect_frame(mono_pcm, 0, 1500)) {
        return 1;
    }
    if (expect_frame(mono_pcm, 1, 7041)) {
        return 1;
    }

    audio_mono_mix_set_mode(AUDIO_STEREO_MIX_STEREO_PASSTHROUGH);
    uint8_t stereo_pcm[] = {
        0xe8, 0x03, 0x18, 0xfc,
    };
    audio_mono_mix_stereo16le_in_place(stereo_pcm, sizeof(stereo_pcm));
    int16_t left = 0;
    int16_t right = 0;
    memcpy(&left, stereo_pcm, sizeof(left));
    memcpy(&right, stereo_pcm + 2, sizeof(right));
    if (left != 1000 || right != -1000) {
        printf("stereo passthrough expected 1000/-1000 got %d/%d\n", left, right);
        return 1;
    }

    return 0;
}
''',
            encoding="utf-8",
        )

        cmd = [
            gcc,
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I",
            str(INCLUDE),
            str(test_c),
            str(SRC),
            "-o",
            str(exe),
        ]
        subprocess.run(cmd, check=True)
        subprocess.run([str(exe)], check=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
