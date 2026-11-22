from . import download


# Stands in for the add-on's client timer: each run moves the running fake downloads one step on.
def client_communication_timer():
    download.step_all()
    return 0.1
