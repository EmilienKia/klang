# ─────────────────────────────────────────────────────────────────────────────
# libk — K language standard libraries
#
# This directory contains the standard library sources for the K language.
# Each subdirectory is a separate library module:
#   - libk/      → base standard library (module "k"), automatically linked
#   - libkmath/  → optional math library (module "k::math")
#   - (future additional libraries go here)
# ─────────────────────────────────────────────────────────────────────────────
add_subdirectory(libk)
add_subdirectory(libkmath)
