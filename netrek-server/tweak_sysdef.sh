#!/bin/bash

#change ROBOTHOST from 127.0.0.1 to localhost in /usr/local/src/netrek/netrek-server/here/etc/sysdef
sed -i 's/127.0.0.1/localhost/g' /usr/local/src/netrek/netrek-server/here/etc/sysdef

touch /usr/local/src/netrek/netrek-server/here/etc/sysdef
