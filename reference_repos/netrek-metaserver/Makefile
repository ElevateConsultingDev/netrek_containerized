#
# Makefile for MetaServerII
#
#CC = purify -log-file="/home/netrek/metaserver/purify.log" -show-directory=yes -show-pc=yes -user-path="/home/netrek/metaserver" gcc
CC = gcc
CFILES	= main.c scan.c server.c disp_old.c disp_new.c disp_web.c disp_udp.c disp_info.c BecomeDaemon.c
OFILES	= $(CFILES:.c=.o)
HDRS	= meta.h packets.h copyright2.h
TARGET	= metaserverII
UTSLIBS	= -lbsd -lsocket

#CFLAGS	= -O -s -DSYSV  -DDEBUG
#CFLAGS	= -O -s -DDEBUG
#CFLAGS	= -O -s
#CFLAGS	= -O -s -DSYSV
CFLAGS	= -ggdb -DSYSV 
#CFLAGS	= -g -DDEBUG
#CFLAGS	= -g -p -DPROF
LIBS	= 

# Amdahl UTS stuff
#LIBS	= $(UTSLIBS)
#LIBS	= -lnsl -lsocket
# hpux stuff
#LIBS	= -lBSD
# Solaris stuff
#LIBS	= -lsocket -lnsl

all: $(TARGET)

$(TARGET): $(OFILES)
	$(CC) $(CFLAGS) -o $(TARGET) $(OFILES) $(LIBS)

$(OFILES):	meta.h

tar:
	tar cvf metaII.tar metarc rsa_keys Makefile features \
			   metaII_info server_faq $(CFILES) $(HDRS)
#			   sample.metarc Customers $(CFILES) $(HDRS)

clean:
	-rm -f $(OFILES)
clobber: clean
	-rm -f $(TARGET)
