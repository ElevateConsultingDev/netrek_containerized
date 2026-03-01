# Use the official Debian image with a specific version tag
FROM debian:11

# Set environment variables
ENV DEBIAN_FRONTEND=noninteractive

EXPOSE 2592 2591 3521 2593 2596 4000 4566 4577 5000 2592

# Install build-essential (C compiler, dev tools), vim, and other utilities
# clone the netrek repo from quozl
# Rename configure.in to configure.ac and create m4 directory
RUN apt-get update && \
apt-get install -y \
    atop \
    autoconf \
    automake \
    build-essential \
    curl \
    gdb \
    git \
    htop \
    iftop \
    libgdbm-dev \
    libglib2.0-dev \
    libgmp-dev \
    libimlib2-dev \
    libncurses5-dev \
    libsdl-mixer1.2-dev \
    libsdl1.2-dev \
    libsdl2-dev \
    libsdl2-image-dev \
    libsdl2-ttf-dev \
    libtool \
    libx11-dev \
    libxt-dev \
    ncdu \
    python3 \
    python3-pip \
    socat \
    vim \
    watch \
    wget \
    x11-apps \
    pulseaudio-utils \
    libpulse0 \
    xauth && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/* && \
    git clone --recursive https://github.com/quozl/netrek.git /usr/local/src/netrek && \
    mv /usr/local/src/netrek/netrek-server/configure.in /usr/local/src/netrek/netrek-server/configure.ac && \
    mkdir -p /usr/local/src/netrek/netrek-server/m4 && \
    mkdir -p /usr/local/src/netrek/.vscode  && \
    wget -P ~ https://github.com/cyrus-and/gdb-dashboard/raw/master/.gdbinit && \
    pip3 install --upgrade pip && \
    pip3 install --upgrade setuptools && \
    pip3 install --upgrade wheel && \
    pip3 install pygments

# Copy custom configuration files to make my life easier
COPY dev/.bashrc /root/.bashrc
COPY dev/.vimrc /root/.vimrc
COPY dev/launch.json /usr/local/src/netrek/.vscode/launch.json

# Enable X11 forwarding by exporting DISPLAY inside the container
ENV DISPLAY=host.docker.internal:0

# PulseAudio: route audio from container to macOS host
ENV PULSE_SERVER=tcp:host.docker.internal:4713
ENV SDL_AUDIODRIVER=pulse

# Make changes from this directory
WORKDIR /usr/local/src/netrek

#add some helper scripts
COPY dev/netrek-server-temp/start_server.sh ./start_server.sh
COPY dev/netrek-server-temp/start_client.sh ./start_client.sh
RUN chmod +x ./start_server.sh && chmod +x ./start_client.sh

#server changes to overcome discovered issues after running libtoolize
RUN sed -i 's/AC_INIT/AC_INIT([netrek],[1.0],[netrek@netrek.org])\nAC_CONFIG_MACRO_DIRS([m4])/g' netrek-server/configure.ac && \
    sed -i '/AC_INIT/a AC_CONFIG_MACRO_DIRS([m4])\nLT_INIT' netrek-client-cow/configure.ac && \
    echo "ACLOCAL_AMFLAGS = -I m4" > netrek-client-cow/Makefile.am

# Enable Sturgeon mode (upgrade/special weapons system)
# config.h.in is the template; configure generates config.h from it
RUN sed -i 's|#undef STURGEON|#define STURGEON 1|' netrek-server/include/config.h.in

#sed in newstartd.c change int debug = 0; to int debug = 1;
# RUN sed -i 's/debug = 0/debug = 1/g' netrek-server/newstartd/newstartd.c

#client changes
RUN sed -i 's/AC_INIT/AC_INIT([netrek],[1.0],[netrek@netrek.org])\nAC_CONFIG_MACRO_DIRS([m4])/g' netrek-client-cow/configure.ac
RUN echo "ACLOCAL_AMFLAGS = -I m4" > netrek-client-cow/Makefile.am

RUN ./build

#add a config file for the client
COPY client-dev/config/.xtrekrc /usr/local/src/netrek/netrek-client-cow/

# Download and install sound files from netrek.org
RUN mkdir -p netrek-client-cow/sounds && \
    wget -q -O /tmp/COW-Sound.3.00.tar.gz https://www.netrek.org/files/archive/COW/COW-Sound.3.00.tar.gz && \
    tar xzf /tmp/COW-Sound.3.00.tar.gz -C /tmp && \
    cp /tmp/sound/sounds/*.wav netrek-client-cow/sounds/ && \
    cp netrek-client-cow/sounds/nt_explosion.wav netrek-client-cow/sounds/nt_sbexplosion.wav && \
    rm -rf /tmp/COW-Sound.3.00.tar.gz /tmp/sound

ENV SOUNDDIR=/usr/local/src/netrek/netrek-client-cow/sounds

#post build server changes
#This uses localhost to spawn robots. For some reason 127.0.0.1 doesn't work.
RUN sed -i 's/127.0.0.1/localhost/g' netrek-server/here/etc/sysdef && \
    sed -i 's/BIND_UDP_PORT_BASE=0/BIND_UDP_PORT_BASE=2593/' netrek-server/here/etc/sysdef && \
    sed -i 's/STURGEON=0/STURGEON=1/' netrek-server/here/etc/sysdef

# Ensure bash is the default shell
CMD ["/bin/bash"]
