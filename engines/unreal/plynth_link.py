import os
import sys


# Returns the folder this script is in, so the package next to it can be imported.
def script_folder():
    path = globals().get("__file__") or (sys.argv[0] if sys.argv else "")
    return os.path.dirname(os.path.abspath(path))


# Drops copies of the package an earlier send loaded, so a send after an update runs the new code.
def forget_loaded_package():
    for name in list(sys.modules):
        if name == "plynth_unreal" or name.startswith("plynth_unreal."):
            del sys.modules[name]


# Runs one Unreal job: the job file is the first argument after the script.
def main():
    folder = script_folder()
    if folder not in sys.path:
        sys.path.insert(0, folder)
    forget_loaded_package()
    args = [arg for arg in sys.argv[1:] if arg]
    if not args:
        print("plynth_link: error bad_job")
        print("No job file was given to the Plynth script.")
        return 1
    try:
        from plynth_unreal import link
    except Exception as error:
        print("plynth_link: error not_editor")
        print("The Plynth script could not start: %s" % error)
        return 1
    return link.run(args[0])


main()
