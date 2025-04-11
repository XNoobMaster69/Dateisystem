#!/bin/bash

vs2="192.168.0.181"
vs3="192.168.0.182"
vs4="192.168.0.183"

PASSWORD="rse"
USER="rse"

echo "rpcbind lokal installieren..."

if command -v sudo >/dev/null 2>&1; then
    echo "-> sudo gefunden, verwende sudo"
    sudo apt-get update && sudo apt-get install -y rpcbind sshpass
else
    echo "-> sudo nicht gefunden, verwende su -"
    echo "$PASSWORD" | su - -c "apt-get update && apt-get install -y rpcbind sshpass"
fi

echo "rpcbind remote installieren..."

for server in $vs2 $vs3 $vs4; do
    echo "Verbinde mit $server..."

    sshpass -p "$PASSWORD" ssh "$USER@$server" '
        if command -v sudo >/dev/null 2>&1; then
            echo "-> sudo gefunden, verwende sudo"
            sudo apt-get update && sudo apt-get install -y rpcbind
        else
            echo "-> sudo nicht gefunden, verwende su -"
            echo "'"$PASSWORD"'" | su - -c "apt-get update && apt-get install -y rpcbind"
        fi
    '
done