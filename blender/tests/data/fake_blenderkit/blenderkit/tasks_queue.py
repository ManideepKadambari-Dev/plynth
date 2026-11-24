"""The fake add-on's queue of work for the main thread. register puts a preferences save in it."""
from . import global_vars

task_queue = []


# Adds a task, a (function, arguments) pair.
def add_task(task):
    task_queue.append(task)


# Runs every queued task.
def queue_worker():
    while task_queue:
        function, arguments = task_queue.pop(0)
        global_vars.note("queued_task_ran", name=function.__name__)
        function(*arguments)
    return 0.5
