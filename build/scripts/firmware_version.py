# Injects FIRMWARE_VERSION, derived from git, so a running device can say which
# build it is. Without it the only way to tell one firmware from another is to
# notice which settings keys it happens to expose - which is how a display can
# quietly get flashed with an older build than it was running.
Import("env")

import subprocess


def describe():
    try:
        return (
            subprocess.check_output(
                ["git", "describe", "--always", "--dirty", "--tags"],
                stderr=subprocess.DEVNULL,
            )
            .decode()
            .strip()
        )
    except Exception:
        return "unknown"


version = describe()
print("Firmware version: %s" % version)
env.Append(CPPDEFINES=[("FIRMWARE_VERSION", env.StringifyMacro(version))])
