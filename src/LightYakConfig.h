// SPDX-License-Identifier: MIT
#ifndef LIGHTYAK_CONFIG_H
#define LIGHTYAK_CONFIG_H

// Shared by the sketch and the library implementation. Edit defaults HERE,
// then rebuild the whole sketch/library. Do not define these in a sketch alone:
// the registry capacity changes Device's memory layout in every translation unit.
// Advanced builds may override these with compiler -D flags applied project-wide.

// Reserved application-tag slots (system properties do not consume these).
// Each slot uses 8 bytes on a classic Uno, plus the application's own value/name.
// Need more tags? Change 10 to (for example) 48 here and rebuild the whole library.
#ifndef LIGHTYAK_MAX_TAGS
#define LIGHTYAK_MAX_TAGS 10
#endif

// Keep these budgets small to return from Update() quickly. Tag capacity and
// batch size do not increase these limits. The wire-v1 batch limit remains six.
#ifndef LIGHTYAK_RX_BUDGET
#define LIGHTYAK_RX_BUDGET 4
#endif
#ifndef LIGHTYAK_TX_BUDGET
#define LIGHTYAK_TX_BUDGET 4
#endif
#ifndef LIGHTYAK_PREP_BUDGET
#define LIGHTYAK_PREP_BUDGET 4
#endif

static_assert(LIGHTYAK_MAX_TAGS > 0 && LIGHTYAK_MAX_TAGS <= 255, "tag capacity must be 1..255");
static_assert(LIGHTYAK_RX_BUDGET > 0 && LIGHTYAK_RX_BUDGET <= 32, "RX budget must be 1..32");
static_assert(LIGHTYAK_TX_BUDGET > 0 && LIGHTYAK_TX_BUDGET <= 32, "TX budget must be 1..32");
static_assert(LIGHTYAK_PREP_BUDGET > 0 && LIGHTYAK_PREP_BUDGET <= 16, "prepare budget must be 1..16");

#endif
