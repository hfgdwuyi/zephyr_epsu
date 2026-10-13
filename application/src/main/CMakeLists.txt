# CANopen 的用户回调/对象字典接口（getNodeId, sdoRdInd/sdoWrInd, emcyInd, pdo* …）
target_sources(app PRIVATE main.c nmtslave.c usr_301.c)

target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/../objdic
    ${CMAKE_CURRENT_LIST_DIR}/../canopen/include
    ${CMAKE_CURRENT_LIST_DIR}/../candriver
    ${CMAKE_CURRENT_LIST_DIR}/../bsp
    ${CMAKE_CURRENT_LIST_DIR}/../external
    ${CMAKE_CURRENT_LIST_DIR}/../state_machine
    ${CMAKE_CURRENT_LIST_DIR}/../scheduler
    ${CMAKE_CURRENT_LIST_DIR}/../ac_meter
)
