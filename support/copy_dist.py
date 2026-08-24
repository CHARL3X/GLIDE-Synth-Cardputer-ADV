# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# Copyright (C) 2026 Charles Tobin (CHARL3X)
# PIO post-action: copy app firmware.bin to dist/ with a stable name,
# ready to drop onto the Cardputer SD card for bmorcelli Launcher — and refresh
# dist/NOTICES.txt beside it, because the binary carries MIT/BSD/Apache/LGPL
# code whose licences travel with every copy of it.
Import("env")
import os, shutil, subprocess, sys

DIST_NAMES = {
    "cardputer-adv": "GLIDE.bin",
    "phase0-probe": "GLIDE-probe.bin",
    "nvsfill": "GLIDE-nvsfill.bin",
}


def copy_dist(source, target, env):
    src = str(target[0])
    project_dir = env["PROJECT_DIR"]
    dist_dir = os.path.join(project_dir, "dist")
    os.makedirs(dist_dir, exist_ok=True)
    name = DIST_NAMES.get(env["PIOENV"], env["PIOENV"] + ".bin")
    dst = os.path.join(dist_dir, name)
    shutil.copy2(src, dst)
    size = os.path.getsize(dst)
    print(f"[dist] {dst} ({size} bytes)")

    # Attribution ships with the binary, always. Generated here rather than
    # maintained by hand so it reads the licence text out of the libraries this
    # build actually linked, and picks the version straight out of config.h.
    gen = os.path.join(project_dir, "support", "gen_notices.py")
    try:
        subprocess.check_call([sys.executable, gen])
    except (OSError, subprocess.CalledProcessError) as exc:
        # Never fail a build over this — but never let it fail quietly either.
        print(f"[dist] WARNING: could not refresh dist/NOTICES.txt ({exc})")
        print("[dist] Do not ship a binary without it — run: python support/gen_notices.py")


env.AddPostAction("$BUILD_DIR/firmware.bin", copy_dist)
