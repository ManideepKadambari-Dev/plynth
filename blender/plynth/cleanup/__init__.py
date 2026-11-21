from collections import namedtuple

# What each cleanup module registers: where it runs in the order, the sentence shown as progress, and
# a function run(ctx).
Step = namedtuple("Step", "order message run")
