
set(COMMON_FILES
bus.cpp
local_bus.cpp
utils/random_channel_gen.cpp
bridge.cpp
channel_list_storage.cpp

)


add_library(zerobus ${COMMON_FILES})
add_subdirectory(transport/zmq)
add_subdirectory(transport/ws_epoll)

