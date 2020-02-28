#!/bin/bash
docker build --network=host -t brocky-build-rpi .
docker run --name temp-brocky brocky-build-rpi /bin/true
docker cp temp-brocky:/brocky/brocky-client ./brocky-client
docker cp temp-brocky:/brocky/deps/quiche/target/debug/libquiche.so ./lib/libquiche.so
docker rm temp-brocky
