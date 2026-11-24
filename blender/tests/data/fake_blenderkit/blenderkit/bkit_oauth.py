"""Sign in of the fake add-on: refresh, logout and token writes, each noted."""
import time

from . import client_lib, global_vars, utils


# Refreshes the sign in when its time has passed.
def ensure_token_refresh():
    prefs = utils.get_prefs()
    if prefs.api_key_refresh and 0 < prefs.api_key_timeout < time.time():
        client_lib.refresh_token(prefs.api_key_refresh, prefs.api_key)
        return True
    return False


# Forgets the sign in and writes that to the preferences file.
def clean_login_data():
    global_vars.note("clean_login_data")
    prefs = utils.get_prefs()
    prefs.api_key = ""
    prefs.api_key_refresh = ""
    utils.save_prefs_to_file()


# Signs out: revokes at the helper and forgets locally.
def logout():
    global_vars.note("logout")
    client_lib.oauth2_logout()
    clean_login_data()


# Stores new tokens and writes them to the preferences file.
def write_tokens(auth_token, refresh_token, oauth_response):
    global_vars.note("write_tokens")
    prefs = utils.get_prefs()
    prefs.api_key = auth_token
    prefs.api_key_refresh = refresh_token
    utils.save_prefs_to_file()
