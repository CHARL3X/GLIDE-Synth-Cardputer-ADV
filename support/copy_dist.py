# PIO post-action: copy app firmware.bin to dist/ with a stable name,
# ready to drop onto the Cardputer SD card for bmorcelli Launcher.
Import("env")
import os, shutil


def copy_dist(source, target, env):
    src = str(target[0])
    project_dir = env["PROJECT_DIR"]
    dist_dir = os.path.join(project_dir, "dist")
    os.makedirs(dist_dir, exist_ok=True)
    dst = os.path.join(dist_dir, "GLIDE.bin")
    shutil.copy2(src, dst)
    size = os.path.getsize(dst)
    print(f"[dist] {dst} ({size} bytes)")


env.AddPostAction("$BUILD_DIR/firmware.bin", copy_dist)
