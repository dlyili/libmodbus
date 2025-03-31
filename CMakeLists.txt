cmake_minimum_required(VERSION 3.23)
project(modbus)

if(NOT TARGET ${PROJECT_NAME})
  if(WIN32)
    message(STATUS "Running cscript")
    execute_process(COMMAND cscript ${CMAKE_CURRENT_LIST_DIR}/src/win32/configure.js
                  WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/src/win32
                  RESULT_VARIABLE CSCRIPT_RESULT)

    if(NOT CSCRIPT_RESULT EQUAL "0")
      message(FATAL_ERROR "cscript failed")
    else()
      file(RENAME ${CMAKE_CURRENT_LIST_DIR}/src/win32/config.h ${CMAKE_CURRENT_LIST_DIR}/config.h)
    endif()

    add_definitions(-DWIN32_LEAN_AND_MEAN)
    add_definitions(-DMODBUS_DLLEXPORTS)
    add_library(${PROJECT_NAME} SHARED)

    target_link_libraries(${PROJECT_NAME} ws2_32)
  else()
    message(STATUS "Running ./autogen.sh")
    execute_process(COMMAND ./autogen.sh
                  WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
                  RESULT_VARIABLE AUTOGEN_RESULT)

    if(NOT AUTOGEN_RESULT EQUAL "0")
      message(FATAL_ERROR "autogen.sh failed")
    endif()

    message(STATUS "Running ./configure --host=${CMAKE_SYSTEM_PROCESSOR}")
    execute_process(COMMAND ./configure --host=${CMAKE_SYSTEM_PROCESSOR}
                    WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
                    RESULT_VARIABLE CONFIGURE_RESULT)
   
    if(NOT CONFIGURE_RESULT EQUAL "0")
      message(FATAL_ERROR "configure failed")
    endif()

    add_library(${PROJECT_NAME} SHARED)
  endif()

  file(READ ${CMAKE_CURRENT_LIST_DIR}/src/modbus-version.h ver)
  string(REGEX MATCH "LIBMODBUS_VERSION_MAJOR \\(([0-9]*)\\)" _ ${ver})
  set(ver_major ${CMAKE_MATCH_1})
  string(REGEX MATCH "LIBMODBUS_VERSION_MINOR \\(([0-9]*)\\)" _ ${ver})
  set(ver_minor ${CMAKE_MATCH_1})
  string(REGEX MATCH "LIBMODBUS_VERSION_MICRO \\(([0-9]*)\\)" _ ${ver})
  set(ver_micro ${CMAKE_MATCH_1})
  message("Libmodbus version: ${ver_major}.${ver_minor}.${ver_micro}")

  set(CPACK_PACKAGE_VENDOR ${PROJECT_NAME})
  set(CPACK_PACKAGE_NAME ${PROJECT_NAME})
  set(CPACK_GENERATOR "ZIP")
  set(package_name ${PROJECT_NAME}-${ver_major}.${ver_minor}.${ver_micro}-${CMAKE_SYSTEM_NAME})
  if (MINGW)
    string(TOLOWER ${package_name}-${CMAKE_C_PLATFORM_ID} package_name)
  else()
    string(TOLOWER ${package_name}-${CMAKE_C_COMPILER_ID} package_name)
  endif()
  if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(package_name ${package_name}-x64)
  else()
    set(package_name ${package_name}-x86)
  endif()
  set(CPACK_PACKAGE_FILE_NAME ${package_name})
  include(CPack)

  target_sources(${PROJECT_NAME} PRIVATE FILE_SET configuration TYPE HEADERS FILES config.h)
  add_subdirectory(src)
endif()
