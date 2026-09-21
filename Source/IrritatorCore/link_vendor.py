# Source/IrritatorCore/link_vendor.py — à lancer une fois après le clone/pull du submodule
import os, subprocess

SRC = os.path.abspath("Source/ThirdParty/irritator/lib")
DST = os.path.abspath("Source/IrritatorCore/Private/vendor/irritator-lib")

os.makedirs(os.path.dirname(DST), exist_ok=True)
if not os.path.exists(DST):
    if os.name == "nt":
        subprocess.run(["cmd", "/c", "mklink", "/J", DST, SRC], check=True)
    else:
        os.symlink(SRC, DST, target_is_directory=True)