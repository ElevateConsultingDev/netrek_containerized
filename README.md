# Netrek Container Project

This project automates the building and running of the Netrek game server and client from [Quozl's excellent Netrek source code](https://github.com/quozl/netrek).

The Dockerfile is based on the Debian 11 image and includes all necessary dependencies and configurations to compile and run a local Netrek server and client.

## Prerequisites

- Docker installed on your machine.
- XQuartz installed on your machine. (If you have brew: brew install xquartz)

## To start everything

```
./build_and_start_everything.sh
```

## To incrementally build and run

```
./1.build_and_run_container.sh
./2.start_netrek_server.sh
./3.start_netrek_client.sh
```
## To just connect to the container

```
./connect.sh
```



