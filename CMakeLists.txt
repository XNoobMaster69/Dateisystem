cmake_minimum_required(VERSION 3.10)
project(Dateisystem)

find_package(gRPC)
find_package(Protobuf REQUIRED)

set(CURRENT_USER rse)
set(SRC_DIR ${PROJECT_SOURCE_DIR}/src)
set(PROTO_FILE ${SRC_DIR}/dateisystem.proto)
set(gRPC_CPP_PLUGIN_EXECUTABLE /home/${CURRENT_USER}/.local/bin/grpc_cpp_plugin)
set(GENERATED_DIR ${PROJECT_SOURCE_DIR}/src/generated)

include_directories(${SRC_DIR})
link_directories(/home/${CURRENT_USER}/.local/lib)
include_directories(/home/${CURRENT_USER}/.local/include)


add_custom_command(
  OUTPUT ${GENERATED_DIR}/dateisystem.pb.cc ${GENERATED_DIR}/dateisystem.pb.h
  COMMAND ${Protobuf_PROTOC_EXECUTABLE}
          --cpp_out=${GENERATED_DIR}
          -I ${SRC_DIR}
          ${PROTO_FILE}
  DEPENDS ${PROTO_FILE}
)


add_custom_command(
  OUTPUT ${GENERATED_DIR}/dateisystem.grpc.pb.cc ${GENERATED_DIR}/dateisystem.grpc.pb.h
  COMMAND ${Protobuf_PROTOC_EXECUTABLE}
          --grpc_out=${GENERATED_DIR}
          --plugin=protoc-gen-grpc=${gRPC_CPP_PLUGIN_EXECUTABLE}
          -I ${SRC_DIR}
          ${PROTO_FILE}
  DEPENDS ${PROTO_FILE}
)

add_executable(server ${SRC_DIR}/server.cpp ${GENERATED_DIR}/dateisystem.pb.cc ${GENERATED_DIR}/dateisystem.grpc.pb.cc)
target_link_libraries(server PRIVATE
  gRPC::grpc++
  protobuf::libprotobuf
  /home/${CURRENT_USER}/.local/lib/libutf8_range_lib.a
  /home/${CURRENT_USER}/.local/lib/libutf8_range.a
  /home/${CURRENT_USER}/.local/lib/libabsl_utf8_for_code_point.a
  /home/${CURRENT_USER}/.local/lib/libutf8_validity.a
)

add_executable(client ${SRC_DIR}/client.cpp ${GENERATED_DIR}/dateisystem.pb.cc ${GENERATED_DIR}/dateisystem.grpc.pb.cc)
target_link_libraries(client PRIVATE
  gRPC::grpc++
  protobuf::libprotobuf
  /home/${CURRENT_USER}/.local/lib/libutf8_range_lib.a
  /home/${CURRENT_USER}/.local/lib/libutf8_range.a
  /home/${CURRENT_USER}/.local/lib/libabsl_utf8_for_code_point.a
  /home/${CURRENT_USER}/.local/lib/libutf8_validity.a
)