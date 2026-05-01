Import("env", "projenv")
import os
import shutil
import UnifiedConfiguration

target_name = env['PIOENV'].upper()
print("BUILD ENV: '%s'" % target_name)

def copy_bootfile(source, target, env):
    framework_dir = env.PioPlatform().get_package_dir("framework-arduinoespressif32")
    shutil.copyfile(framework_dir + "/tools/partitions/boot_app0.bin",
                    env.subst("$BUILD_DIR") + "/boot_app0.bin")

env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", copy_bootfile)

# Force a clean re-link so the appended OPTIONS_JSON blob is always fresh.
try:
    os.remove(env['PROJECT_BUILD_DIR'] + '/' + env['PIOENV'] + '/' + env['PROGNAME'] + '.bin')
except FileNotFoundError:
    pass

env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", UnifiedConfiguration.appendConfiguration)
