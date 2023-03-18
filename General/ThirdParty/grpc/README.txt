v1.42.0
Compile Commond:

mac：
1. clone grpc and select specific version
2. cd into grpc root dir
3. mkdir cmake/build
4. cd cmake/build
5
6. cmake ../.. -G "Visual Studio 17" -DCMAKE_INSTALL_PREFIX="D:\openssl\install" -DgRPC_INSTALL=ON -DCMAKE_BUILD_TYPE=Release -DgRPC_BUILD+TESTS=OFF -DgRPC_SSL_PROVIDER=package -DOPENSSL_ROOT_DIR=D:\openssl
#-DBUILD_SHARED_LIBS=ON -- compile dylib
7. make -j 20

win:
cmake ../.. -G "Visual Studio 17" -DCMAKE_INSTALL_PREFIX="D:\grpc\install" -DgRPC_INSTALL=ON -DCMAKE_BUILD_TYPE=Release -DgRPC_BUILD_TESTS=OFF -DgRPC_SSL_PROVIDER=package -DOPENSSL_ROOT_DIR=C:\openssl-build -DCMAKE_CXX_FLAGS_RELEASE="/MT" -DCMAKE_C_FLAGS_RELEASE="/MT"

