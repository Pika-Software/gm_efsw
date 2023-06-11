# configure_file(config.hpp.in ${CMAKE_BINARY_DIR}/include/config.hpp)

# Include every source file in source directory
file(GLOB_RECURSE SOURCES RELATIVE ${CMAKE_CURRENT_LIST_DIR} *.cpp *.hpp)
source_group(TREE ${CMAKE_CURRENT_LIST_DIR} PREFIX "Sources" FILES ${SOURCES})

add_library(gm_efsw SHARED ${SOURCES})
target_link_libraries(gm_efsw PRIVATE 
    gmod::common
    gmod::helpers_extended
    sourcesdk::tier1
    efsw
)

target_include_directories(gm_efsw PRIVATE 
    ${CMAKE_CURRENT_LIST_DIR}
    # ${CMAKE_BINARY_DIR}/include
)


set_gmod_suffix_prefix(gm_efsw)

# Autoinstall
set(AUTOINSTALL "" CACHE PATH "Autoinstall path")
if(IS_DIRECTORY ${AUTOINSTALL})
    autoinstall(gm_efsw ${AUTOINSTALL})
endif()

set_target_properties(gm_efsw PROPERTIES OUTPUT_NAME efsw)
