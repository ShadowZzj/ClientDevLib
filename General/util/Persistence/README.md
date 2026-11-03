# VersionedJsonStore

`VersionedJsonStore` is a per-file SQLite document store, using existing
`General/ThirdParty/sqlite3pp` / `sqlite3` and `General/ThirdParty/json.hpp`.
Include `cmake/ClientControllerDependencies.cmake` and link
`ClientDevLib::VersionedJsonStore`. Windows packages must call
`clientdev_deploy_sqlite(target)` to retain the existing architecture-specific DLL.
Other platforms use the installed `SQLite::SQLite3` target.

The generic store knows only document keys, monotonic generations, JSON objects
and caller-provided timestamps. Product validation and fields remain with the
caller. `compareExchange(key, expectedGeneration, callback, updatedAt)` obtains a
SQLite `BEGIN IMMEDIATE` write reservation, checks the current generation, calls
the validator/document callback with `nextGeneration`, and commits both the
current document and immutable history in one transaction. An absent document
has generation 0; first publication is generation 1. `VersionConflict` exposes
the current generation so an API can return a conflict and request a reload.

Each instance serializes access to its connection; separate connections and
processes are serialized by SQLite. The busy timeout is 5 seconds. WAL with
`synchronous=FULL` is used; failed validation or SQL operations roll back without
changing the current document or creating a history record. Stored JSON objects
are limited to 1 MiB and generations to 9007199254740991. The SQLite schema has
no credentials, tenant, policy or agent concepts.

For a simple file backup, stop every process using the database and then copy the
database. While writers are active, use SQLite's backup facility rather than
copying only the main file and discarding the WAL.
