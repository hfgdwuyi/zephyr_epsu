target_sources(app PRIVATE
    http_api.c
    http_routes.c
    http_task_echo.c
    http_task_uptime.c
    http_task_heartbeat.c
    http_task_update.c
    http_task_control.c
    http_task_diag.c
    http_task_diag_debug.c
)

target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)