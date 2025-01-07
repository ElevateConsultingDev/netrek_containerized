# Netrek Container Project

This project automates the building and running of the Netrek game server and client from [Quozl's excellent Netrek source code](https://github.com/quozl/netrek) using Docker and XQuartz to display the Netrek client GUI on the host macOS machine.

The Dockerfile is based on the Debian 11 image and includes all necessary dependencies and configurations to compile and run a local Netrek server and client.

This project is designed for MacOX hosts. It uses XQuartz and X11 forwarding to display the Netrek client GUI from the Docker container to the host macOS machine.

* XQuartz acts as the X11 server on macOS, enabling GUI applications from the container to display on the host.

* Docker is configured to forward the GUI output to XQuartz using the DISPLAY environment variable and volume mounts (/tmp/.X11-unix and ~/.Xauthority).

This setup allows you to run the Netrek client inside the container and view the GUI directly on your macOS desktop.

## But, why?

You might be wondering why it’s worth the effort to run a Netrek server and forward the client GUI to your Mac. The answer is simple: I love this game. I’ve been playing Netrek since 2000, and I’ve never been satisfied with the clients available for modern Macs. The Linux and PC clients have received the most support and development over the years.

I’ve used several Windows clients, and my favorite is [XPMod](https://www.netrek.org/downloads/clients/#xpmod). However, getting it to run on macOS is a real hassle. After some research, I discovered how to forward the Linux GUI to a Mac using a Docker container. With that knowledge, and by taking the time to read through the documentation to overcome the challenges of building the server and client, I was able to get everything up and running.

So, if this setup allows other Netrek enthusiasts or newcomers to enjoy it, then I’m happy to share it!

## Prerequisites

* Docker installed on your machine.
* XQuartz installed on your machine. (If you have brew: ```brew install xquartz```)

## To build and start everything

```
./build_and_start_everything.sh
```

## To incrementally build and run

```
./1.build_and_run_container.sh

./2.start_netrek_server.sh

./3.start_netrek_client.sh
```
## To connect to the container

```
./connect.sh
```



