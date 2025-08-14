# 定义了SRC_LIST变量，包含当前目录下所有的源文件
aux_source_directory(. SRC_LIST)

# 指定生成可执行文件
add_executable(ChatClient main.cpp ${PROTO_SRCS})
add_executable(ProtobufClient protobuf_client.cpp ${PROTO_SRCS})
add_executable(ProtobufChatClient protobuf_chat_client.cpp ${PROTO_SRCS})

# 指定可执行文件链接时需要的库文件
target_link_libraries(ChatClient pthread)
target_link_libraries(ProtobufClient pthread ${Protobuf_LIBRARIES})
target_link_libraries(ProtobufChatClient pthread ${Protobuf_LIBRARIES})