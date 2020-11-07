project(dmsdos)
cmake_minimum_required(VERSION 3.2)

add_definitions(
    -D__DMSDOS_LIB__ -DUSE_FLOCK -U__linux__
)

add_library(dmsdos
    src/lib_interface.c
    src/dblspace_interface.c
    src/dblspace_dec.c
    src/dblspace_compr.c
    src/dblspace_methsq.c
    src/dblspace_alloc.c
    src/dblspace_chk.c
    src/dblspace_tables.c
    src/dstacker_compr.c
    src/dstacker_dec.c
    src/dstacker_alloc.c

    src/dmsdos.h src/dmsdos-config.h
)

add_executable(dcread src/dcread.c)
target_link_libraries(dcread dmsdos)

add_executable(mcdmsdos src/mcdmsdos.c)
target_link_libraries(mcdmsdos dmsdos)

add_executable(dmsdosfsck src/dmsdosfsck.c)
target_link_libraries(dmsdosfsck dmsdos)

add_executable(cvftest src/cvftest.c)
target_link_libraries(cvftest dmsdos)

add_executable(cvflist src/cvflist.c)
target_link_libraries(cvflist dmsdos)
