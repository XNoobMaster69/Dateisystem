# Verteiltes Dateisystem

## For new installation use config.sh with the right username and password. If developing on one machine comment the remote parts of the script out
    cd Dateisystem
    chmod +x config.sh
    ./config.sh

**After the config.sh is done, set local paths**

    export MY_INSTALL_DIR="$HOME/.local"
    mkdir -p "$MY_INSTALL_DIR"
    export PATH="$MY_INSTALL_DIR/bin:$PATH"
## Compile and Build (Unix) 
Before compiling and building, add the right **HOME_PATH** to **CMakeLists.txt** usually /home/<user> and change self_addr in server.cpp to the wished address

    mkdir -p cmake/build
    pushd cmake/build
    cmake -DCMAKE_PREFIX_PATH=$MY_INSTALL_DIR ../..
    make -j 4


## Binaries are found in Dateisystem/cmake/build/<client | server>


## Run
    ./server 192.168.0.180:50051 192.168.0.18X:50051
    ./client "[2001:db8::1234]:50051" ./directory
