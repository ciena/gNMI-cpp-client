Following steps needs to be followed to buld Ciena gRPC C++ gNMI Client for gRPC stack v1.0.0

1> Clone Ciena gNMI C++ client repository from github.

2> Go into client repository directory and then clone gRPC stack v1.0.0 recursively to include third party submodules like boringssl, protobuf etc
```sh
git clone --recurse-submodules -b v1.0.0  https://github.com/grpc/grpc.git
```

3> Apply following patch to fix time out issue with ssl handshake.
```sh
patch -p0 < ciena.grpc.fix.patch
```

4> You may require to apply following patch to make gRPC stack, boringssl compilable with gcc 4.4.6.
```sh
patch -p0 < ciena.grpc.comp.patch
```

5> Build grpc stack
```sh
cd grpc
make
```

6> Build ciena C++ gNMI client
```sh
cd ../ciena_grpc_client/v1.0.x/cpp
make
```
