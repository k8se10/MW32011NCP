#pragma once

// Embeds the same bundled texture-name basemap the live mod itself embeds
// (proxy_d3d9/resource.h's own IDR_TEXTURE_BASEMAP, proxy_d3d9/bundled_data/
// texture_names_basemap.txt) -- a separate, independent embed (its own
// resource ID space, its own .rc entry) rather than a shared/reused one,
// matching this project's own "never link against or depend on
// proxy_d3d9-specific build artifacts" convention for standalone tools. Used
// here purely for REAL COVERAGE PROGRESS reporting (how many of the ~17,000
// known real textures already have a cache entry) -- this tool never forces
// anything to load, it only reports against this list.
#define IDR_TEXTURE_BASEMAP 1
