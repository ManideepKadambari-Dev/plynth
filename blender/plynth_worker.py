import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))


# Finds the job file after the "--" in the command line.
def job_path_from(argv):
    args = argv[argv.index("--") + 1:] if "--" in argv else []
    if "--job" in args and args.index("--job") + 1 < len(args):
        return args[args.index("--job") + 1]
    return None


# Runs the job named on the command line and exits 3 when no result could be written.
def main():
    path = job_path_from(sys.argv)
    if path is None:
        print("PLYNTH worker: no --job file was given.")
        sys.exit(3)
    from plynth.runner import run_job_file
    result = run_job_file(path)
    if result is None:
        sys.exit(3)


if __name__ == "__main__":
    main()
