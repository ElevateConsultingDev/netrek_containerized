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
    socat \
    vim \
    watch \
    wget \
    x11-apps \
    xauth && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/* && \
    git clone --recursive https://github.com/quozl/netrek.git /usr/local/src/netrek && \
    mv /usr/local/src/netrek/netrek-server/configure.in /usr/local/src/netrek/netrek-server/configure.ac && \
    mkdir -p /usr/local/src/netrek/netrek-server/m4

# Copy custom configuration files to make my life easier
COPY .bashrc /root/.bashrc
COPY .vimrc /root/.vimrc

# Enable X11 forwarding by exporting DISPLAY inside the container
ENV DISPLAY=host.docker.internal:0

# Make changes from this directory
WORKDIR /usr/local/src/netrek

#add some helper scripts
COPY netrek-server/start_server.sh ./start_server.sh
COPY netrek-server/start_client.sh ./start_client.sh
RUN chmod +x ./start_server.sh && chmod +x ./start_client.sh

#server changes to overcome discovered issues after running libtoolize
RUN sed -i 's/AC_INIT/AC_INIT([netrek],[1.0],[netrek@netrek.org])\nAC_CONFIG_MACRO_DIRS([m4])/g' netrek-server/configure.ac && \
    sed -i '/AC_INIT/a AC_CONFIG_MACRO_DIRS([m4])\nLT_INIT' netrek-client-cow/configure.ac && \
    echo "ACLOCAL_AMFLAGS = -I m4" > netrek-client-cow/Makefile.am

#client changes
RUN sed -i 's/AC_INIT/AC_INIT([netrek],[1.0],[netrek@netrek.org])\nAC_CONFIG_MACRO_DIRS([m4])/g' netrek-client-cow/configure.ac
RUN echo "ACLOCAL_AMFLAGS = -I m4" > netrek-client-cow/Makefile.am

RUN ./build

#add a config file for the client
COPY .xtrekrc /usr/local/src/netrek/netrek-client-cow/

#post build server changes
#This uses localhost to spawn robots. For some reason 127.0.0.1 doesn't work.
RUN sed -i 's/127.0.0.1/localhost/g' netrek-server/here/etc/sysdef

# Ensure bash is the default shell
CMD ["/bin/bash"]
