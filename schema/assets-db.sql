-- Library/assets/assets.db, version 1 (PRAGMA user_version).
-- The index of the assets catalog. The files in items/ are the truth: rescan rebuilds the asset rows,
-- tags, categories of assets and the search table from them. room_types, categories and their links are
-- the user's lists and live only here; a lost database starts again from the built-in lists.
-- Each connection sets: PRAGMA foreign_keys = ON; PRAGMA journal_mode = WAL; PRAGMA busy_timeout = 5000.
-- Times are UTC text, for example 2026-11-21T10:00:00Z. Lengths are metres.

CREATE TABLE room_types (
    id       TEXT PRIMARY KEY NOT NULL,
    name     TEXT NOT NULL,
    sort     INTEGER NOT NULL,
    built_in INTEGER NOT NULL DEFAULT 0 CHECK (built_in IN (0, 1))
);

CREATE TABLE categories (
    id       TEXT PRIMARY KEY NOT NULL,
    name     TEXT NOT NULL,
    sort     INTEGER NOT NULL,
    built_in INTEGER NOT NULL DEFAULT 0 CHECK (built_in IN (0, 1))
);

CREATE TABLE room_type_categories (
    room_type TEXT NOT NULL REFERENCES room_types (id) ON DELETE CASCADE,
    category  TEXT NOT NULL REFERENCES categories (id) ON DELETE CASCADE,
    PRIMARY KEY (room_type, category)
) WITHOUT ROWID;

CREATE TABLE licenses (
    id              TEXT PRIMARY KEY NOT NULL,
    name            TEXT NOT NULL,
    url             TEXT NOT NULL DEFAULT '',
    redistributable INTEGER NOT NULL DEFAULT 0 CHECK (redistributable IN (0, 1)),
    sort            INTEGER NOT NULL
);

CREATE TABLE assets (
    id               TEXT PRIMARY KEY NOT NULL,
    name             TEXT NOT NULL,
    license          TEXT NOT NULL DEFAULT 'unknown' REFERENCES licenses (id),
    source_kind      TEXT NOT NULL DEFAULT '',
    source_ref       TEXT NOT NULL DEFAULT '',
    imported_at      TEXT NOT NULL DEFAULT '',
    width            REAL NOT NULL DEFAULT 0,
    depth            REAL NOT NULL DEFAULT 0,
    height           REAL NOT NULL DEFAULT 0,
    triangles        INTEGER NOT NULL DEFAULT 0,
    state            TEXT NOT NULL CHECK (state IN ('ready', 'needs_refresh', 'broken')),
    problem          TEXT NOT NULL DEFAULT '',
    thumbnail        TEXT NOT NULL CHECK (thumbnail IN ('ok', 'failed', 'missing')),
    thumbnail_error  TEXT NOT NULL DEFAULT '',
    blend_bytes      INTEGER NOT NULL DEFAULT 0,
    blend_modified   TEXT NOT NULL DEFAULT '',
    info_modified    TEXT NOT NULL DEFAULT ''
);

CREATE TABLE asset_categories (
    asset    TEXT NOT NULL REFERENCES assets (id) ON DELETE CASCADE,
    category TEXT NOT NULL REFERENCES categories (id) ON DELETE CASCADE,
    PRIMARY KEY (asset, category)
) WITHOUT ROWID;

CREATE INDEX asset_categories_by_category ON asset_categories (category);

CREATE TABLE asset_tags (
    asset TEXT NOT NULL REFERENCES assets (id) ON DELETE CASCADE,
    tag   TEXT NOT NULL,
    PRIMARY KEY (asset, tag)
) WITHOUT ROWID;

CREATE INDEX asset_tags_by_tag ON asset_tags (tag);

-- Kept in step with assets by the catalog code, one row per asset. tags and categories hold the tags and
-- the category names joined with spaces.
CREATE VIRTUAL TABLE asset_search USING fts5 (
    id UNINDEXED,
    name,
    tags,
    categories,
    source,
    tokenize = 'unicode61 remove_diacritics 2',
    prefix = '2 3'
);

PRAGMA user_version = 1;
