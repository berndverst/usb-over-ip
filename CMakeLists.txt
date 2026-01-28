cmake_minimum_required(VERSION 3.16)
project(VirtualUSB VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Windows-specific settings
if(WIN32)
    add_definitions(-D_WIN32_WINNT=0x0A00)
    add_definitions(-DWIN32_LEAN_AND_MEAN)
    add_definitions(-DNOMINMAX)
endif()

# Protocol library (header-only)
add_library(vusb_protocol INTERFACE)
target_include_directories(vusb_protocol INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/protocol)

# Server application
add_executable(vusb_server
    server/vusb_server.c
    server/vusb_server.h
)
target_link_libraries(vusb_server PRIVATE vusb_protocol)
if(WIN32)
    target_link_libraries(vusb_server PRIVATE ws2_32)
endif()

# Client application
add_executable(vusb_client
    client/vusb_client.c
    client/vusb_client.h
)
target_link_libraries(vusb_client PRIVATE vusb_protocol)
if(WIN32)
    target_link_libraries(vusb_client PRIVATE ws2_32)
endif()

# Install targets
install(TARGETS vusb_server vusb_client
    RUNTIME DESTINATION bin
)

# Install protocol headers
install(FILES 
    protocol/vusb_protocol.h
    protocol/vusb_ioctl.h
    DESTINATION include/vusb
)

# Copy driver files if building on Windows
if(WIN32)
    # Note: Driver must be built separately with WDK
    message(STATUS "Driver source is in driver/ directory - build with WDK/VS")
endif()

# Package configuration
set(CPACK_PACKAGE_NAME "VirtualUSB")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_VENDOR "Virtual USB Project")
include(CPack)
