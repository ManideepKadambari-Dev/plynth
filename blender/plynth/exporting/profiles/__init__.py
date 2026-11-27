from collections import namedtuple

# What each export profile module registers (core/include/plynth/core/ExportJob.h, "Profiles"). name is shown
# in messages. collision says whether collision meshes go into the files, kit whether the modular kit is made.
# texture_prefix starts every texture file name. mesh_prefix is the start every exported mesh name must have
# and collision_prefix the one of every collision mesh (None when the profile has none); refused_prefixes are
# starts no mesh name may have. name_objects(ctx) names every mesh and material before the scene checks, and
# finish_objects(ctx) makes the objects ready for the files after them.
Profile = namedtuple("Profile", "name collision kit texture_prefix mesh_prefix collision_prefix refused_prefixes "
                                "name_objects finish_objects")
