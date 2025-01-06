#!/bin/bash
export DISPLAY=:0

open -a XQuartz

sleep 3

xhost +localhost


docker build -t netrek-server-image .

docker run -t -d \
    -e DISPLAY=host.docker.internal:0 \
    -v /tmp/.X11-unix:/tmp/.X11-unix \
    -v ~/.Xauthority:/root/.Xauthority \
    -p 2592:2592 \
    --name netrek-server netrek-server-image \
    /bin/bash