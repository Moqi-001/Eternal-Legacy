#ifndef ETERNAL_RESOURCE_INDEX_H
#define ETERNAL_RESOURCE_INDEX_H
#include <stddef.h>
/* Call once before game threads start. The immutable snapshot is never rescanned.
 * Returns 0 when complete, -1 on any enumeration/allocation failure. */
int resource_index_init(void);
/* Only exact top-level GAME_DATA_DIR regular files are covered.
 * 1: present (optional size), 0: known absent, -1: outside coverage/unavailable.
 * Directories, case aliases, nested paths, dot components and writable data are not covered. */
int resource_index_lookup(const char *mapped_path, size_t *size);
enum {
    RESOURCE_TYPE_UNKNOWN = -1, RESOURCE_TYPE_ABSENT = 0,
    RESOURCE_TYPE_REGULAR = 1, RESOURCE_TYPE_DIRECTORY = 2
};
/* Exact root/top-level type; root '.' and known trailing-slash directories are
 * recognized. Case aliases, parent traversal and nested paths remain unknown. */
int resource_index_type(const char *mapped_path, size_t *size);
/* Canonical nested resource/ZIP-parent evidence. Missing components encountered
 * before the leaf also cover later ancestor candidates, regardless of suffix.
 * Standalone top-level metadata remains unknown. Existing loose files are never
 * byte-cache keys. At most 4 directories x 512 entries + 64 ancestor proofs;
 * incomplete/failed/aliased directories always return UNKNOWN. */
int resource_index_path_type(const char *mapped_path);
/* Call before and after scoped filesystem mutations. Root asset inventory stays
 * immutable; native_io's mutable-path guard overrides stale root evidence. */
void resource_index_invalidate(const char *mapped_path);
#endif
