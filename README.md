PREREQUISITES
---------------
To build gRPC & protobuf from source, the following tools are needed:
* autoconf
* automake
* libtool
* make
* g++
* unzip


Following steps needs to be followed to buld Ciena gRPC C++ Client for gRPC stack v1.0.0
------------------------------------------------------------------------------------------

1> Clone Ciena gNMI C++ client repository from github using command
```sh
git clone https://github.com/ciena/gNMI-cpp-client.git
```

2> Go into client repository directory "gNMI-cpp-client"  and then clone gRPC stack v1.0.0 recursively to include third party submodules like boringssl, protobuf etc using the command
```sh
cd gNMI-cpp-client
git clone --recurse-submodules -b v1.0.0  https://github.com/grpc/grpc.git
```

3> You may require to patch a few files of gRPC stack, boringssl for making them compatible to your compiler version.
   For gcc 4.4.6 following files need to be patched.
* src/core/ext/transport/chttp2/server/insecure/server\_chttp2.c
* src/core/lib/security/transport/handshake.c
* third\_party/boringssl/crypto/chacha/chacha\_vec.c
* third\_party/boringssl/crypto/cipher/e\_chacha20poly1305.c
* third\_party/boringssl/crypto/internal.h
* third\_party/boringssl/crypto/poly1305/poly1305\_vec.c
* third\_party/boringssl/include/openssl/sha.h

   The required changes are present in the ciena.grpc.patch file.

   To apply this patch, run the following command

```sh
patch -p0 < ciena.grpc.patch
```

   This will modify the stack to be compatible if needed

4> Build grpc stack
```sh
cd grpc
make NO_SECURE=false EMBED_OPENSSL=true
```

5> Build the ciena C++ gNMI client
```sh
cd ../ciena_grpc_client/v1.0.x/cpp
make
```
