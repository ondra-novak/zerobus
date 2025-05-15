
set(COMMON_FILES
bridge.cpp
local_bus.cpp
bridge_tcp_client.cpp
bridge_tcp_common.cpp
bridge_tcp_server.cpp
bridge_pipe.cpp
direct_bridge.cpp
websocket.cpp
serialization.cpp
)

if(MSVC)
    set(PLATFORM_SPECIFIC_FILES network_windows.cpp)
else()
    set(PLATFORM_SPECIFIC_FILES network_linux.cpp)
endif()

add_library(zerobus ${COMMON_FILES} ${PLATFORM_SPECIFIC_FILES})
    
