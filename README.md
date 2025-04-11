# Verteiltes Dateisystem

## For new installation use config.sh with the right username and password. If developing on one machine comment the remote parts of the script out

## Compile and Build (Unix) 
    cd Dateisystem
    mkdir -p cmake/build
    pushd cmake/build
    cmake -DCMAKE_PREFIX_PATH=$MY_INSTALL_DIR ../..
    make -j 4


## Binaries are found in Dateisystem/cmake/build/<client | server>
