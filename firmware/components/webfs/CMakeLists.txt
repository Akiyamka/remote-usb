idf_component_register(
    SRCS "webfs.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES joltwallet__littlefs
)
