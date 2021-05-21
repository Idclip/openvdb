
# This module configures GLOBAL CXX settings for the VDB project. Primarily:
#  - CXX standard
#  - CXX warnings and errors
#  - CXX platform options and defines
#
# This module reads from the following variables:
#   CMAKE_VERSION
#   CMAKE_CXX_COMPILER_ID
#   CMAKE_CXX_STANDARD
#   CMAKE_CXX_COMPILER_VERSION
#   FUTURE_MINIMUM_GCC_VERSION
#   FUTURE_MINIMUM_ICC_VERSION
#   FUTURE_MINIMUM_MSVC_VERSION
#   MINIMUM_CLANG_VERSION
#   MINIMUM_GCC_VERSION
#   MINIMUM_ICC_VERSION
#   MINIMUM_MSVC_VERSION
#   OPENVDB_CXX_STRICT
#   USE_COLORED_OUTPUT

set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

###############################################################################

# CXX version validation

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD ${MINIMUM_CXX_STANDARD} CACHE STRING
    "The C++ standard whose features are requested to build OpenVDB components." FORCE)
elseif(CMAKE_CXX_STANDARD LESS ${MINIMUM_CXX_STANDARD})
  message(FATAL_ERROR "Provided C++ Standard is less than the supported minimum."
    "Required is at least \"${MINIMUM_CXX_STANDARD}\" (found ${CMAKE_CXX_STANDARD})")
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "(Clang|AppleClang)")
  if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS MINIMUM_CLANG_VERSION)
    message(FATAL_ERROR "Insufficient clang++ version. Minimum required is "
      "\"${MINIMUM_CLANG_VERSION}\". Found version \"${CMAKE_CXX_COMPILER_VERSION}\""
    )
  endif()
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS MINIMUM_GCC_VERSION)
    message(FATAL_ERROR "Insufficient g++ version. Minimum required is "
      "\"${MINIMUM_GCC_VERSION}\". Found version \"${CMAKE_CXX_COMPILER_VERSION}\""
    )
  endif()
  if(OPENVDB_FUTURE_DEPRECATION AND FUTURE_MINIMUM_GCC_VERSION)
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS FUTURE_MINIMUM_GCC_VERSION)
      message(DEPRECATION "Support for GCC versions < ${FUTURE_MINIMUM_GCC_VERSION} "
        "is deprecated and will be removed.")
    endif()
  endif()
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Intel")
  if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS MINIMUM_ICC_VERSION)
    message(FATAL_ERROR "Insufficient ICC version. Minimum required is "
      "\"${MINIMUM_ICC_VERSION}\". Found version \"${CMAKE_CXX_COMPILER_VERSION}\""
    )
  endif()
  if(OPENVDB_FUTURE_DEPRECATION AND FUTURE_MINIMUM_ICC_VERSION)
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS FUTURE_MINIMUM_ICC_VERSION)
      message(DEPRECATION "Support for ICC versions < ${FUTURE_MINIMUM_ICC_VERSION} "
        "is deprecated and will be removed.")
    endif()
  endif()
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
  if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS MINIMUM_MSVC_VERSION)
    message(FATAL_ERROR "Insufficient MSVC version. Minimum required is "
      "\"${MINIMUM_MSVC_VERSION}\". Found version \"${CMAKE_CXX_COMPILER_VERSION}\""
  )
  endif()
  if(OPENVDB_FUTURE_DEPRECATION AND FUTURE_MINIMUM_MSVC_VERSION)
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS FUTURE_MINIMUM_MSVC_VERSION)
      message(DEPRECATION "Support for MSVC versions < ${FUTURE_MINIMUM_MSVC_VERSION} "
        "is deprecated and will be removed.")
    endif()
  endif()
endif()

###############################################################################

# CXX required/useful defines and options
# Increase the number of sections that an object file can contain
add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/bigobj>")
# Excludes APIs such as Cryptography, DDE, RPC, Shell, and Windows Sockets
add_compile_definitions("$<$<PLATFORM_ID:WIN32>:-DWIN32_LEAN_AND_MEAN>")
# Disable non-secure CRT library function warnings
# https://docs.microsoft.com/en-us/cpp/error-messages/compiler-warnings/
#     compiler-warning-level-3-c4996?view=vs-2019#unsafe-crt-library-functions
add_compile_definitions("$<$<PLATFORM_ID:WIN32>:-D_CRT_SECURE_NO_WARNINGS>")
# Disable POSIX function name warnings
# https://docs.microsoft.com/en-us/cpp/error-messages/compiler-warnings/
#     compiler-warning-level-3-c4996?view=vs-2019#posix-function-names
add_compile_definitions("$<$<PLATFORM_ID:WIN32>:-D_CRT_NONSTDC_NO_WARNINGS>")

###############################################################################

# CXX warnings and errors

if(NOT OPENVDB_CXX_STRICT)
  set(OPENVDB_CXX_STRICT 0)
endif()

if(OPENVDB_CXX_STRICT GREATER 0)
  if(NOT (CMAKE_CXX_COMPILER_ID MATCHES "(Clang|AppleClang|GNU)"))
    message(WARNING "No available CXX warnings for compiler ${CMAKE_CXX_COMPILER_ID}")
  endif()
endif()

if(OPENVDB_CXX_STRICT EQUAL 0)
  add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/W0>")
endif()

if(OPENVDB_CXX_STRICT GREATER_EQUAL 1)
  # clang warnings
  set(_CLANG_CXX_WARNINGS
    -Wall
    -Wextra
    -Wconversion
    -Wno-sign-conversion)
  add_compile_options("$<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:${_CLANG_CXX_WARNINGS}>")
  unset(_CLANG_CXX_WARNINGS)

  # GCC warnings
  set(_GNU_CXX_WARNINGS
    -Wall
    -Wextra
    -pedantic
    -Wcast-align
    -Wcast-qual
    -Wconversion
    -Wdisabled-optimization
    -Woverloaded-virtual)
  add_compile_options("$<$<CXX_COMPILER_ID:GNU>:${_GNU_CXX_WARNINGS}>")
  unset(_GNU_CXX_WARNINGS)

  # MSVC warnings
  add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/W1>")

  # Linker warnings
  if(${CMAKE_VERSION} VERSION_GREATER_EQUAL 3.13)
    add_link_options("$<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-Wall>")
  endif()
endif()

if(OPENVDB_CXX_STRICT EQUAL 2)
  # clang/gcc
  add_compile_options("$<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-Werror>")
  # MSVC
  add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/WX>")
  if(${CMAKE_VERSION} VERSION_GREATER_EQUAL 3.13)
    add_link_options("$<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-Werror>")
    add_link_options("$<$<CXX_COMPILER_ID:MSVC>:/WX>")
  endif()
endif()

###############################################################################

if(USE_COLORED_OUTPUT)
  message(STATUS "Enabling colored compiler output")
  add_compile_options("$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-fcolor-diagnostics>")
  add_compile_options("$<$<CXX_COMPILER_ID:GNU>:-fdiagnostics-color=always>")
endif()

###############################################################################
