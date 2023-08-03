# 添加自定义库的源文件路径
aux_source_directory(buffer SRC_buffer)
aux_source_directory(config SRC_config)
aux_source_directory(http SRC_http)
aux_source_directory(log SRC_log)
aux_source_directory(pool SRC_pool)
aux_source_directory(server SRC_server)
aux_source_directory(timer SRC_timer)

# 构建server可执行文件
add_executable(server main.cpp ${SRC_buffer} ${SRC_config} ${SRC_http} ${SRC_log} ${SRC_pool} ${SRC_server} ${SRC_timer})

# 包含自定义库的根目录
target_include_directories(server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# 链接相关库到server中
target_link_libraries(server pthread mysqlclient)
