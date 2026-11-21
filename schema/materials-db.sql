-- Library/materials/materials.db, version 1 (PRAGMA user_version).
-- The index of the materials catalog. Created empty by initLibrary in milestone 6 so the layout is final;
-- milestone 9 seeds the groups and licenses and fills it. Shares nothing with assets.db.
-- Each connection sets: PRAGMA foreign_keys = ON; PRAGMA journal_mode = WAL; PRAGMA busy_timeout = 5000.
-- Times are UTC text. Lengths are metres.

CREATE TABLE material_groups (
    id       TEXT PRIMARY KEY NOT NULL,
    name     TEXT NOT NULL,
    sort     INTEGER NOT NULL,
    built_in INTEGER NOT NULL DEFAULT 0 CHECK (built_in IN (0, 1))
);

CREATE TABLE licenses (
    id              TEXT PRIMARY KEY NOT NULL,
    name            TEXT NOT NULL,
    url             TEXT NOT NULL DEFAULT '',
    redistributable INTEGER NOT NULL DEFAULT 0 CHECK (redistributable IN (0, 1)),
    sort            INTEGER NOT NULL
);

CREATE TABLE materials (
    id              TEXT PRIMARY KEY NOT NULL,
    name            TEXT NOT NULL,
    license         TEXT NOT NULL DEFAULT 'unknown',
    source_kind     TEXT NOT NULL DEFAULT '',
    source_ref      TEXT NOT NULL DEFAULT '',
    imported_at     TEXT NOT NULL DEFAULT '',
    tile_width      REAL NOT NULL DEFAULT 1,
    tile_height     REAL NOT NULL DEFAULT 1,
    state           TEXT NOT NULL CHECK (state IN ('ready', 'needs_refresh', 'broken')),
    problem         TEXT NOT NULL DEFAULT '',
    thumbnail       TEXT NOT NULL CHECK (thumbnail IN ('ok', 'failed', 'missing')),
    thumbnail_error TEXT NOT NULL DEFAULT '',
    info_modified   TEXT NOT NULL DEFAULT ''
);

CREATE TABLE material_group_members (
    material TEXT NOT NULL REFERENCES materials (id) ON DELETE CASCADE,
    grp      TEXT NOT NULL REFERENCES material_groups (id) ON DELETE CASCADE,
    PRIMARY KEY (material, grp)
) WITHOUT ROWID;

CREATE TABLE material_tags (
    material TEXT NOT NULL REFERENCES materials (id) ON DELETE CASCADE,
    tag      TEXT NOT NULL,
    PRIMARY KEY (material, tag)
) WITHOUT ROWID;

CREATE VIRTUAL TABLE material_search USING fts5 (
    id UNINDEXED,
    name,
    tags,
    groups,
    source,
    tokenize = 'unicode61 remove_diacritics 2',
    prefix = '2 3'
);

PRAGMA user_version = 1;
