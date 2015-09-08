#!/bin/sh /etc/rc.common

#
# HAKit - The Home Automation KIT
# Copyright (C) 2014-2015 Sylvain Giroudon
#
# This file is subject to the terms and conditions of the GNU Lesser
# General Public License v2.1. See the file LICENSE in the top level
# directory for more details.
#

START=99

NAME='hakit'
BIN="/usr/bin/$NAME"
LOG="/var/log/$NAME.log"
CONF="/etc/$NAME.conf"

[ -r $CONF ] && . $CONF
[ -z "$APP" ] && APP="/usr/share/$NAME/test.hk"


pidof() {
    ps | grep "$1" | grep -v grep |
    while read pid trash; do
	[ "$pid" = "$$" ] || echo -n "$pid "
    done
}

stop() {
    pids=$(pidof "$NAME")
    if [ -n "$pids" ]; then
	kill $pids 2>/dev/null
    fi

    pids=$(pidof "$NAME")
    if [ -n "$pids" ]; then
	sleep 1
	kill -9 $pids 2>/dev/null
    fi
}

start() {
    stop
    sleep 5
    /bin/rm -f $LOG
    $BIN -d1 --daemon $ARGS $APP 2>>$LOG >>$LOG
}

status() {
    pids=$(pidof "$NAME")
    if [ -n "$pids" ]; then
	echo "Service $NAME started ($pids)"
    else
	echo "Service $NAME stopped"
    fi
}
