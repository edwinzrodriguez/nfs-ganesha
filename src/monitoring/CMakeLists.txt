########### next target ###############

set(CMAKE_CXX_STANDARD 17)

SET(ganesha_monitoring_SRCS
  prometheus_exposer.cc
  dynamic_metrics.cc
)

add_library(ganesha_monitoring SHARED ${ganesha_monitoring_SRCS})

if(LINUX)
  # Prefer libproc2 (procps-ng >= 4.x); fall back to legacy libprocps.
  find_library(LIBPROC2_LIB proc2)
  find_path(LIBPROC2_INCLUDE_DIR NAMES libproc2/pids.h)
  if(LIBPROC2_LIB AND LIBPROC2_INCLUDE_DIR)
    target_link_libraries(ganesha_monitoring ${LIBPROC2_LIB})
    target_include_directories(ganesha_monitoring PRIVATE ${LIBPROC2_INCLUDE_DIR})
    target_compile_definitions(ganesha_monitoring PRIVATE HAVE_LIBPROC2)
    message(STATUS "Found libproc2: ${LIBPROC2_LIB}")
  else()
    find_library(PROCPS_LIB procps)
    find_path(PROCPS_INCLUDE_DIR NAMES proc/readproc.h)
    if(PROCPS_LIB AND PROCPS_INCLUDE_DIR)
      target_link_libraries(ganesha_monitoring ${PROCPS_LIB})
      target_include_directories(ganesha_monitoring PRIVATE ${PROCPS_INCLUDE_DIR})
      target_compile_definitions(ganesha_monitoring PRIVATE HAVE_PROCPS)
      message(STATUS "Found procps: ${PROCPS_LIB}")
    else()
      message(WARNING "Neither libproc2 nor procps found; ganesha resource metrics disabled. Install procps-ng-devel (libproc2) or procps/libprocps-dev.")
    endif()
  endif()
else()
  # On non-Linux platforms, skip procps and warning
  message(STATUS "Non-Linux platform detected; skipping procps library check")
endif()


add_sanitizers(ganesha_monitoring)
set_target_properties(ganesha_monitoring PROPERTIES COMPILE_FLAGS "-fPIC")
target_include_directories(ganesha_monitoring     PRIVATE ${PROJECT_SOURCE_DIR}/libntirpc/src/monitoring/prometheus-cpp-lite/core/include)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pedantic-errors -Werror -Wall -Wextra")

install(TARGETS ganesha_monitoring LIBRARY DESTINATION ${LIB_INSTALL_DIR})

########### install files ###############
