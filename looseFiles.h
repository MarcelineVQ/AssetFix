// =============================================================================
// looseFiles.h - Pre-indexed loose file lookup for fast disk override
// =============================================================================

#pragma once

// Scan Data/ for non-MPQ files, build hash set. wowDir=NULL to auto-detect.
void looseFiles_init(const char* wowDir);
void looseFiles_cleanup();

// O(1) lookup: returns disk-relative path (e.g. "Data\Character\...") or NULL.
// Case-insensitive, normalizes / to \.
const char* looseFiles_lookup(const char* gamePath);
