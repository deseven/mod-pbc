# Compatibility shim for module checkouts that live in the old
# mod-playerbots-characters directory. The AzerothCore build system includes
# modules/<dir>/<dir>.cmake based on the directory name, so this file must
# exist for old checkouts. New checkouts use the mod-pbc directory and load
# mod-pbc.cmake directly.
#
# Load the real build configuration from mod-pbc.cmake. It sits next to this
# file during the transition (old directory layout) and also once the module
# directory is renamed to mod-pbc.
include("${CMAKE_CURRENT_LIST_DIR}/mod-pbc.cmake")
