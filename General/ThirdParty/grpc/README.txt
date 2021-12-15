Compile Commond:

1. clone grpc and select specific version
2. cd into grpc root dir
3. mkdir cmake/build
4. Change CMakeLists.txt, add 

set (CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum OS X deployment version")
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_POLICY_DEFAULT_CMP0063 NEW)

5. vim  <grpcdir>/thirdparty/zlib/CMakeLists.txt, comment out these lines

#============================================================================
# Example binaries
#============================================================================

#add_executable(example test/example.c)
#target_link_libraries(example zlib)
#add_test(example example)

#add_executable(minigzip test/minigzip.c)
#target_link_libraries(minigzip zlib)

if(HAVE_OFF64_T)
   # add_executable(example64 test/example.c)
   # target_link_libraries(example64 zlib)
   # set_target_properties(example64 PROPERTIES COMPILE_FLAGS "-D_FILE_OFFSET_BITS=64")
   # add_test(example64 example64)

   # add_executable(minigzip64 test/minigzip.c)
   # target_link_libraries(minigzip64 zlib)
   # set_target_properties(minigzip64 PROPERTIES COMPILE_FLAGS "-D_FILE_OFFSET_BITS=64")
endif()

6. execute cmd, change prefix to your installdir:  
 cmake ../.. -DCMAKE_INSTALL_PREFIX="~/Desktop/grpc/cmake/build/installdir/" -DgRPC_INSTALL=ON                \     
              -DCMAKE_BUILD_TYPE=Release       \
-DgRPC_BUILD+TESTS=OFF    \
-DCMAKE_OSX_DEPLOYMENT_TARGET=10.13 \
-DCMAKE_CXX_VISIBILITY_PRESET=hidden\
_DCMAKE_C_VISIBILITY_PRESET=hidden


