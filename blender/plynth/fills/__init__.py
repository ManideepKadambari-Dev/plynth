from collections import namedtuple

# What each fill module registers: the opening types it serves and a function build(ctx, floor, opening) that
# makes the objects of one opening and returns them.
FillEntry = namedtuple("FillEntry", "types build")
