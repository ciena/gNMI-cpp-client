Following steps needs to be followed to buld Ciena gRPC C++ Client for gRPC stack v1.0.0

1> Clone gRPC v1.0.0 recursively to include third party submodules like boringssl, protobuf etc

```sh
$ git clone --recurse-submodules -b v1.0.0  https://github.com/grpc/grpc.git
```

2> You may require to patch a few files of gRPC stack, boringssl for making them compatible to your compiler version.
   For gcc 4.4.6 following files need to be patched.
	src/core/ext/transport/chttp2/server/insecure/server_chttp2.c
	src/core/lib/security/transport/handshake.c
	third_party/boringssl/crypto/chacha/chacha_vec.c
	third_party/boringssl/crypto/cipher/e_chacha20poly1305.c
	third_party/boringssl/crypto/internal.h
	third_party/boringssl/crypto/poly1305/poly1305_vec.c
	third_party/boringssl/include/openssl/sha.h

   The required changes are present in the ciena.grpc.patch file.  
  
   To apply this patch, run the following command

```sh
patch -p0 < ciena.grpc.patch
```

   This will modify the stack to be compatible if needed

3> Build grpc stack

```sh
$ cd grpc
$ make
```

4> Build the ciena C++ gNMI client
```sh
$ cd ../ciena_grpc_client/v1.0.x/cpp
$ make
```
