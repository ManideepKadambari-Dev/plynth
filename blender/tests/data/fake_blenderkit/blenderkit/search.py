"""Search of the fake add-on: what the server says about an id, through the helper. It can answer another
item than the one asked for, as the real server can for a version id.
"""
from . import client_lib


# The item record for an id, or None. Raises when the helper does not answer in time.
def get_asset_by_base_id(asset_base_id):
    answer = client_lib.blocking_request("https://www.blendkit.com/api/v1/search/", "GET", {},
                                         {"asset_base_id": asset_base_id}, 1)
    results = answer.get("results", [])
    return results[0] if results else None
