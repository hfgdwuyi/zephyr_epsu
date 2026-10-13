file(GLOB CANOPEN_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_LIST_DIR}/source/*.c
)

target_sources(app PRIVATE ${CANOPEN_SOURCES})

target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/include
    ${CMAKE_CURRENT_LIST_DIR}/source
    ${CMAKE_CURRENT_LIST_DIR}/../objdic
    ${CMAKE_CURRENT_LIST_DIR}/../candriver
)
