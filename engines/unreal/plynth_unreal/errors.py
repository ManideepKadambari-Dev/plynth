ERROR_CODES = ("not_editor", "bad_job", "folder_taken", "import_failed", "material_failed", "level_failed")


class LinkError(Exception):
    # Makes an error with one of the result's error codes and one plain sentence for the user.
    def __init__(self, code, message):
        if code not in ERROR_CODES:
            code = "import_failed"
        super().__init__(message)
        self.code = code
        self.message = message or "Something went wrong in Unreal Engine."
