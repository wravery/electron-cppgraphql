cmake_minimum_required(VERSION 3.15)

project(electron-cppgraphql)

set(CMAKE_CXX_STANDARD 20)

find_package(pegtl 3.2.0 CONFIG REQUIRED)
find_package(cppgraphqlgen 4.0.0 CONFIG REQUIRED)

add_subdirectory(schema)

execute_process(COMMAND node -e "require('nan')"
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  OUTPUT_VARIABLE NAN_INCLUDE
  OUTPUT_STRIP_TRAILING_WHITESPACE)

add_library(${PROJECT_NAME} SHARED
  NodeBinding.cpp
  TodayMock.cpp
  ${CMAKE_JS_SRC})

set_target_properties(${PROJECT_NAME} PROPERTIES PREFIX "" SUFFIX ".node")
target_compile_definitions(${PROJECT_NAME} PRIVATE PROJECT_NAME=${PROJECT_NAME}
  BUILDING_NODE_EXTENSION
  V8_REVERSE_JSARGS
  V8_COMPRESS_POINTERS)
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_JS_INC} ${CMAKE_CURRENT_SOURCE_DIR}/${NAN_INCLUDE})
target_link_libraries(${PROJECT_NAME} cppgraphqlgen::graphqlservice cppgraphqlgen::graphqljson today_schema ${CMAKE_JS_LIB})