# Copies the committed New Dawn runtime assets (mm/assets/newdawn) next to the executable.
# Usage: cmake -DSRC=<mm/assets/newdawn> -DDEST=<build>/mm/assets/newdawn -P SyncNewDawnAssets.cmake
# Older dev setups symlink the packs into DEST (pointing at a private asset directory); copying "through" such a link
# would write into its target, so top-level symlinks are replaced by real directories first.
file(GLOB entries LIST_DIRECTORIES true "${DEST}/*")
foreach(entry IN LISTS entries)
    if(IS_SYMLINK "${entry}")
        file(REMOVE "${entry}")
    endif()
endforeach()
file(MAKE_DIRECTORY "${DEST}")
file(COPY "${SRC}/" DESTINATION "${DEST}")
