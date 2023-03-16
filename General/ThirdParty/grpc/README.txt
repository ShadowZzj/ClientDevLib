v1.47.0
Compile Commond:

1. clone grpc and select specific version
2. cd into grpc root dir
3. mkdir cmake/build
4. cd cmake/build
5
6. cmake ../.. -DCMAKE_INSTALL_PREFIX="~/Desktop/grpc/cmake/build/static/" -DgRPC_INSTALL=ON -DCMAKE_BUILD_TYPE=Release -DgRPC_BUILD+TESTS=OFF -DCMAKE_OSX_DEPLOYMENT_TARGET=10.15  -DgRPC_SSL_PROVIDER=package -DOPENSSL_ROOT_DIR=~/Desktop/openssl
#-DBUILD_SHARED_LIBS=ON -- compile dylib
7. make -j 20

