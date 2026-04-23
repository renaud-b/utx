#!/bin/bash
set -e

echo "Downloading utx..."

# Qme3mqytCCXk7Z6zNfTiQh12gnH4kAUiz9Kq1wAzth7WEa => utx-linux-amd64-v1.0.0
# Qmdgb63HtJ2HuXg1id66NGYrVg4NFsuwDxqBnwBiL82SqC => utx-linux-amd64-v1.1.0
curl -L https://metapixia.com/ipfs/Qmdgb63HtJ2HuXg1id66NGYrVg4NFsuwDxqBnwBiL82SqC -o utx

chmod +x utx
sudo mv utx /usr/local/bin/

echo "utx installed!"