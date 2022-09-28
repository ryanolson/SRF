add_subdirectory(tests)

find_package(CURL)

find_package(opentelemetry-cpp CONFIG REQUIRED)
message(STATUS "otel c++ include dirs: ${OPENTELEMETRY_CPP_INCLUDE_DIRS}")
message(STATUS "otel c++ libs: ${OPENTELEMETRY_CPP_LIBRARIES}")

add_library(libsre SHARED
  libsre/coro/event.cpp
  libsre/coro/sync_wait.cpp
  libsre/coro/thread_pool.cpp
  libsre/coro/thread_local_state.cpp
  libsre/system/thread.cpp
  libsre/trace/trace.cpp
  libsre/trace/runtime_context.cpp
)

add_library(${PROJECT_NAME}::libsre ALIAS libsre)

target_link_libraries(libsre
  PUBLIC
  glog::glog
  opentelemetry-cpp::api
  opentelemetry-cpp::sdk
  opentelemetry-cpp::ext
  opentelemetry-cpp::version
  opentelemetry-cpp::common
  opentelemetry-cpp::trace
  opentelemetry-cpp::metrics
  opentelemetry-cpp::logs
  opentelemetry-cpp::in_memory_span_exporter
  opentelemetry-cpp::ostream_span_exporter
  opentelemetry-cpp::otlp_http_exporter
  CURL::libcurl
  PRIVATE

  # hwloc::hwloc
)

target_include_directories(libsre
  PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
  ${OPENTELEMETRY_CPP_INCLUDE_DIRS}
  PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_definitions(libsre
  PUBLIC
  $<$<BOOL:${sre_BUILD_BENCHMARKS}>:sre_ENABLE_BENCHMARKING>
)

set_target_properties(libsre PROPERTIES OUTPUT_NAME sre)

# Finally, set the install RPATH to include the stubs folder for CUDA::nvml. If thats made private, this can be removed
set_target_properties(libsre PROPERTIES INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib:\$ORIGIN:${CMAKE_INSTALL_PREFIX}/lib/stubs")

# ##################################################################################################
# - install targets -------------------------------------------------------------------------------
rapids_cmake_install_lib_dir(lib_dir)
include(CPack)
include(GNUInstallDirs)

install(
  TARGETS libsre
  DESTINATION ${lib_dir}
  EXPORT ${PROJECT_NAME}-core-exports
  COMPONENT Core
)

install(
  DIRECTORY include/
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  COMPONENT Core
)

rapids_export(BUILD sre
  EXPORT_SET ${PROJECT_NAME}-core-exports
  GLOBAL_TARGETS libsre
  VERSION ${PROJECT_VERSION}
  LANGUAGES C CXX CUDA
  NAMESPACE sre::
  DOCUMENTATION doc_string
  FINAL_CODE_BLOCK code_string
)
