#!/bin/bash

vs2="192.168.0.181"
vs3="192.168.0.182"
vs4="192.168.0.183"

PASSWORD="rse"
USER="rse"


# Lokale Pfade setzen
export MY_INSTALL_DIR="$HOME/.local"
mkdir -p "$MY_INSTALL_DIR"
export PATH="$MY_INSTALL_DIR/bin:$PATH"

echo "Lokale Abhängigkeiten installieren..."
if command -v sudo >/dev/null 2>&1; then
    echo "-> sudo gefunden, verwende sudo"
    sudo apt-get update && sudo apt-get install -y sshpass build-essential cmake git autoconf libtool pkg-config
else
    echo "-> sudo nicht gefunden, verwende su -"
    echo "$PASSWORD" | su - -c "apt-get update && apt-get install -y sshpass build-essential cmake git autoconf libtool pkg-config"
fi

# Auskommentieren falls lokale dev umgebung ohne remote
echo "Remote Abhängigkeiten installieren..."
for server in $vs2 $vs3 $vs4; do
    echo "Verbinde mit $server..."

    sshpass -p "$PASSWORD" ssh "$USER@$server" '
        if command -v sudo >/dev/null 2>&1; then
            echo "-> sudo gefunden, verwende sudo"
            sudo apt-get update && sudo apt-get install -y build-essential cmake git autoconf libtool pkg-config
        else
            echo "-> sudo nicht gefunden, verwende su -"
            echo "'"$PASSWORD"'" | su - -c "apt-get update && build-essential cmake git autoconf libtool pkg-config"
        fi
    '
done
# Bis hier


git clone --recurse-submodules -b v1.71.0 --depth 1 --shallow-submodules https://github.com/grpc/grpc

cd grpc
mkdir -p cmake/build
pushd cmake/build

cmake -DgRPC_INSTALL=ON \
      -DgRPC_BUILD_TESTS=OFF \
      -DCMAKE_CXX_STANDARD=17 \
      -DCMAKE_INSTALL_PREFIX="$MY_INSTALL_DIR" \
      ../..

make -j 4
make install
popd
