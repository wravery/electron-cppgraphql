cmake_minimum_required(VERSION 2.8)

project(electron-cppgraphql)

if(WIN32)
  # Let CMake figure out the exports for the SHARED library (DLL) on Windows.
  set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS TRUE)
endif()

find_package(cppgraphqlgen CONFIG REQUIRED)

add_custom_command(
  OUTPUT
    TodaySchema.cpp
    TodaySchema.h
  COMMAND cppgraphqlgen::schemagen ${CMAKE_CURRENT_SOURCE_DIR}/schema.today.graphql Today today
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/schema.today.graphql
  COMMENT "Generating mock TodaySchema files"
)

execute_process(COMMAND node -e "require('nan')"
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  OUTPUT_VARIABLE NAN_INCLUDE)

add_library(${PROJECT_NAME} SHARED
  NodeBinding.cpp
  Today.cpp
  TodaySchema.cpp)

set_target_properties(${PROJECT_NAME} PROPERTIES PREFIX "" SUFFIX ".node")
target_compile_definitions(${PROJECT_NAME} PRIVATE PROJECT_NAME=${PROJECT_NAME})
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_JS_INC} ${CMAKE_CURRENT_SOURCE_DIR}/${NAN_INCLUDE})
target_link_libraries(${PROJECT_NAME} cppgraphqlgen::graphqlservice cppgraphqlgen::graphqljson ${CMAKE_JS_LIB})