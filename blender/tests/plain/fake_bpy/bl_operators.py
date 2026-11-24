"""The bundled Python operators of the stand-in that a fetch must close: preferences.addon_enable, addon_disable,
addon_install and addon_remove (bl_operators in Blender) and extensions.package_* (the extensions add-on of
Blender). Each notes "bundled_ran" first and then does its real work with Python, reaching addon_utils through
its module at call time as the Blender ones do. The stand-in registers them when it starts, before any startup
script.
"""
import os
import shutil

import bpy
from bpy.props import IntProperty, StringProperty


# A path in one of the user folders.
def _user(kind, *parts):
    return os.path.join(bpy.utils.user_resource(kind), *parts)


# Writes a small file, making its folder.
def _write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as handle:
        print(text, file=handle)


class PREFERENCES_OT_addon_enable(bpy.types.Operator):
    bl_idname = "preferences.addon_enable"
    bl_label = "Enable Add-on"

    module: StringProperty(default="")  # type: ignore[valid-type]

    # Enables an add-on.
    def execute(self, context):
        bpy.note("bundled_ran", idname=self.bl_idname)
        import addon_utils
        return {"FINISHED"} if addon_utils.enable(self.module, default_set=True) is not None else {"CANCELLED"}


class PREFERENCES_OT_addon_disable(bpy.types.Operator):
    bl_idname = "preferences.addon_disable"
    bl_label = "Disable Add-on"

    module: StringProperty(default="")  # type: ignore[valid-type]

    # Disables an add-on.
    def execute(self, context):
        bpy.note("bundled_ran", idname=self.bl_idname)
        import addon_utils
        addon_utils.disable(self.module, default_set=True)
        return {"FINISHED"}


class PREFERENCES_OT_addon_install(bpy.types.Operator):
    bl_idname = "preferences.addon_install"
    bl_label = "Install Add-on"

    filepath: StringProperty(default="")  # type: ignore[valid-type]

    # Puts an add-on file into the add-ons folder of the user.
    def execute(self, context):
        bpy.note("bundled_ran", idname=self.bl_idname)
        _write(_user("SCRIPTS", "addons", os.path.basename(self.filepath) or "addon.py"), "# installed")
        return {"FINISHED"}


class PREFERENCES_OT_addon_remove(bpy.types.Operator):
    bl_idname = "preferences.addon_remove"
    bl_label = "Remove Add-on"

    module: StringProperty(default="")  # type: ignore[valid-type]

    # Deletes the folder of an add-on from the add-ons folder of the user.
    def execute(self, context):
        bpy.note("bundled_ran", idname=self.bl_idname)
        folder = _user("SCRIPTS", "addons", self.module)
        if self.module and os.path.isdir(folder):
            shutil.rmtree(folder)
        return {"FINISHED"}


class EXTENSIONS_OT_package_install_files(bpy.types.Operator):
    bl_idname = "extensions.package_install_files"
    bl_label = "Install from Disk"

    filepath: StringProperty(default="")  # type: ignore[valid-type]

    # Unpacks an extension into the extensions folder of the user.
    def execute(self, context):
        bpy.note("bundled_ran", idname=self.bl_idname)
        stem = os.path.splitext(os.path.basename(self.filepath))[0] or "extension"
        _write(_user("EXTENSIONS", "user_default", stem, "__init__.py"), "# installed")
        return {"FINISHED"}


class EXTENSIONS_OT_package_install(bpy.types.Operator):
    bl_idname = "extensions.package_install"
    bl_label = "Install"

    repo_index: IntProperty(default=0)  # type: ignore[valid-type]
    pkg_id: StringProperty(default="")  # type: ignore[valid-type]

    # Installs an extension from a repository.
    def execute(self, context):
        bpy.note("bundled_ran", idname=self.bl_idname)
        _write(_user("EXTENSIONS", "user_default", self.pkg_id or "extension", "__init__.py"), "# installed")
        return {"FINISHED"}


class EXTENSIONS_OT_package_uninstall(bpy.types.Operator):
    bl_idname = "extensions.package_uninstall"
    bl_label = "Uninstall"

    repo_index: IntProperty(default=0)  # type: ignore[valid-type]
    pkg_id: StringProperty(default="")  # type: ignore[valid-type]

    # Deletes an installed extension.
    def execute(self, context):
        bpy.note("bundled_ran", idname=self.bl_idname)
        folder = _user("EXTENSIONS", "user_default", self.pkg_id)
        if self.pkg_id and os.path.isdir(folder):
            shutil.rmtree(folder)
        return {"FINISHED"}


class EXTENSIONS_OT_package_upgrade_all(bpy.types.Operator):
    bl_idname = "extensions.package_upgrade_all"
    bl_label = "Update All"

    # Updates every extension, here a mark in the extensions folder.
    def execute(self, context):
        bpy.note("bundled_ran", idname=self.bl_idname)
        _write(_user("EXTENSIONS", "user_default", ".upgraded"), "upgraded")
        return {"FINISHED"}


CLASSES = (PREFERENCES_OT_addon_enable, PREFERENCES_OT_addon_disable, PREFERENCES_OT_addon_install,
           PREFERENCES_OT_addon_remove, EXTENSIONS_OT_package_install_files, EXTENSIONS_OT_package_install,
           EXTENSIONS_OT_package_uninstall, EXTENSIONS_OT_package_upgrade_all)


# Registers the bundled operators.
def register():
    for cls in CLASSES:
        bpy.utils.register_class(cls)
