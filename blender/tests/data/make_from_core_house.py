"""Makes buildjob/from_core_house.json from a job.json that `plynth build` left in a project's build folder.

The project is the two floor house of samples/plans (house-01-ground.png and house-01-upper.png) with a stair on the
ground floor, a gable roof on the upper floor and the default flat roofs on the exposed part of the ground floor.
The project and library folders become the placeholders @PROJECT@ and @LIBRARY@ that the tests fill in.
Run with: python make_from_core_house.py <job.json> <project folder> <library folder>
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
OUTPUT = os.path.join(HERE, "buildjob", "from_core_house.json")


# Returns the text with the folder replaced by the placeholder, whatever the slashes.
def replace_folder(text, folder, placeholder):
    folder = folder.replace("\\", "/").rstrip("/")
    return text.replace(folder, placeholder)


def main(job_file, project, library):
    with open(job_file, "r", encoding="utf-8") as handle:
        text = handle.read()
    text = replace_folder(text, library, "@LIBRARY@")
    text = replace_folder(text, project, "@PROJECT@")
    job = json.loads(text)
    job["job_id"] = "build-house"
    with open(OUTPUT, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(json.dumps(job, indent=2) + "\n")


if __name__ == "__main__":
    main(*sys.argv[1:4])
