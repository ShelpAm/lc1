if(NOT DEFINED SOURCE_DIR OR NOT DEFINED DESTINATION_DIR)
    message(FATAL_ERROR "SOURCE_DIR and DESTINATION_DIR are required")
endif()

# This destination is generated build output. Recreate it so removed or moved
# assets cannot linger and hide stale references; local unused sources stay out.
file(REMOVE_RECURSE "${DESTINATION_DIR}")
file(COPY "${SOURCE_DIR}/" DESTINATION "${DESTINATION_DIR}" PATTERN "unused" EXCLUDE)
