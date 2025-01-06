# Use the official Debian image with a specific version tag
FROM debian:11

# Set environment variables
ENV DEBIAN_FRONTEND=noninteractive

EXPOSE 2592 2591 3521 2593 2596 4000 4566 4577 5000 2592

# Install build-essential (C compiler, dev tools), vim, and other utilities
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
    xauth \
    x11-apps && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/* && \
    git clone --recursive https://github.com/quozl/netrek.git /usr/local/src/netrek

# Copy custom configuration files
COPY .bashrc /root/.bashrc
COPY .vimrc /root/.vimrc

# Enable X11 forwarding by exporting DISPLAY inside the container
ENV DISPLAY=host.docker.internal:0

# cd to netrek-server
WORKDIR /usr/local/src/netrek/netrek-server

# Rename configure.in to configure.ac
RUN mv configure.in configure.ac

# Create m4 directory
RUN mkdir -p m4

# Edit configure.ac, add AC_CONFIG_MACRO_DIRS([m4]) after AC_INIT
RUN sed -i 's/AC_INIT/AC_INIT([netrek],[1.0],[netrek@netrek.org])\nAC_CONFIG_MACRO_DIRS([m4])/g' configure.ac

# Create Makefile.am and add ACLOCAL_AMFLAGS = -I m4
RUN echo "ACLOCAL_AMFLAGS = -I m4" > Makefile.am

WORKDIR /usr/local/src/netrek/netrek-client-cow
RUN sed -i '/AC_INIT/a AC_CONFIG_MACRO_DIRS([m4])\nLT_INIT' /usr/local/src/netrek/netrek-client-cow/configure.ac
RUN echo "ACLOCAL_AMFLAGS = -I m4" > Makefile.am

# cd to netrek
WORKDIR /usr/local/src/netrek

#this should include #!/bin/bash
COPY netrek-server/start_server.sh ./start_server.sh
COPY netrek-server/start_client.sh ./start_client.sh
COPY netrek-server/tweak_sysdef.sh ./tweak_sysdef.sh

RUN chmod +x ./start_server.sh
RUN chmod +x ./start_client.sh
RUN chmod +x ./tweak_sysdef.sh

RUN ./build
COPY .xtrekrc /usr/local/src/netrek/netrek-client-cow/

RUN ./tweak_sysdef.sh

# Ensure bash is the default shell
CMD ["/bin/bash"]
