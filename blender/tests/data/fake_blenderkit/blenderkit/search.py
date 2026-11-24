"""Search of the fake add-on, under the real name: what the server says about an id, through the helper. It can
answer another item than the one asked for, as the real server can for a version id. The operator looks it up
through this module at call time, as the real one does.
"""
import json

from . import client_lib


# The results of a search, a list of item records. Writes them to filepath when one is given. Raises when the
# helper does not answer in time.
def get_search_simple(parameters, filepath=None, page_size=100, max_results=100000000, api_key=""):
    answer = client_lib.blocking_request("https://www.blendkit.com/api/v1/search/", "GET", {},
                                         {"asset_base_id": parameters.get("asset_base_id", "")}, 1)
    results = list(answer.get("results", []))[:max(0, min(page_size, max_results))]
    if filepath:
        with open(filepath, "w", encoding="utf-8") as handle:
            json.dump(results, handle)
    return results
