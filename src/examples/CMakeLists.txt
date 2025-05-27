


find_package(OpenSSL)



set(exampleFiles webserver.cpp)

foreach (f ${exampleFiles})
    string(REGEX MATCH "([^\/]+$)" filename ${f})
    string(REGEX MATCH "[^.]*" executable_name ${filename})
    add_executable(${executable_name} ${f})
if (OpenSSL_FOUND) 
         Set(OpenSSL_LIBS OpenSSL::SSL OpenSSL::Crypto)
         target_compile_definitions(${executable_name} PRIVATE -DWITH_TLS)
         target_include_directories(${executable_name} PRIVATE ${OpenSSL_INCLUDE_DIR})
endif()
    target_link_libraries(${executable_name} zerobus_epoll zerobus ${OpenSSL_LIBS} ${STANDARD_LIBRARIES} )
endforeach ()
