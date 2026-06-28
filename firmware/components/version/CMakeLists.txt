configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/include/app_version.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/include/app_version.h"
    @ONLY
)

idf_component_register(
    INCLUDE_DIRS "${CMAKE_CURRENT_BINARY_DIR}/include"
)
