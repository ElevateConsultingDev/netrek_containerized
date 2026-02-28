#!/bin/bash
export DISPLAY=:0

open -a XQuartz

sleep 3

xhost +localhost

# Start PulseAudio for sound forwarding from container to macOS
pulseaudio --kill 2>/dev/null
sleep 1
pulseaudio --load="module-native-protocol-tcp auth-anonymous=1" --exit-idle-time=-1 --daemon

docker build -t netrek-server-image .

docker run -t -d \
    -e DISPLAY=host.docker.internal:0 \
    -e PULSE_SERVER=tcp:host.docker.internal:4713 \
    -e SDL_AUDIODRIVER=pulse \
    -e SOUNDDIR=/usr/local/src/netrek/netrek-client-cow/sounds \
    -v /tmp/.X11-unix:/tmp/.X11-unix \
    -v ~/.Xauthority:/root/.Xauthority \
    -p 2592:2592 \
    -p 2593-2629:2593-2629/udp \
    --name netrek-server netrek-server-image \
    /bin/bash
