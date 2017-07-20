#! /bin/sh

#
# HAKit - The Home Automation KIT
# Copyright (C) 2014-2015 Sylvain Giroudon
#
# This file is subject to the terms and conditions of the GNU Lesser
# General Public License v2.1. See the file LICENSE in the top level
# directory for more details.
#

### BEGIN INIT INFO
# Provides:          hakit
# Required-Start:    $local_fs $syslog $network
# Required-Stop:
# Default-Start:     2 3 4 5
# Default-Stop:      0 1 6
# Short-Description: Start/stop HAKit
# Description:       Start/stop HAKit, the Home Automation Kit service
### END INIT INFO

DEBUG=1

PATH=/sbin:/usr/sbin:/bin:/usr/bin
DESC="HAKit daemon"
NAME=hakit
DAEMON="/usr/bin/$NAME"
DAEMON_ARGS=""
PIDFILE="/var/run/$NAME.pid"
SCRIPTNAME="/etc/init.d/$NAME"

LAUNCHER="/usr/bin/$NAME-launcher"
CONF_DEFAULT=/etc/default/$NAME
CONF_PLATFORM=/etc/$NAME/platform

# Read configuration variable file if it is present
[ -r $CONF_DEFAULT ] && . $CONF_DEFAULT

if [ -z "$APP" ]; then
    if [ -f $CONF_PLATFORM ]; then
        # If no APP is specified and a HAKit platform config file is found,
        # start the HAKit launcher
        DAEMON=$LAUNCHER
    else
        # If no APP is specified and no HAKit platform config file is present,
        # start a default test app
        APP="/usr/share/$NAME/test.hk"
    fi
fi

# Exit if the program is not installed
[ -x "$DAEMON" ] || exit 0

# Load the VERBOSE setting and other rcS variables
. /lib/init/vars.sh

# Define LSB log_* functions.
# Depend on lsb-base (>= 3.0-6) to ensure that this file is present.
. /lib/lsb/init-functions


do_start()
{
	# Return
	#   0 if daemon has been started
	#   1 if daemon was already running
	#   2 if daemon could not be started
	start-stop-daemon --start --quiet --pidfile $PIDFILE --exec $DAEMON --test > /dev/null \
		|| return 1
	start-stop-daemon --start --quiet --pidfile $PIDFILE --exec $DAEMON -- --daemon --debug=$DEBUG $DAEMON_ARGS $APP \
		|| return 2
	return 0;
}


do_stop()
{
	# Return
	#   0 if daemon has been stopped
	#   1 if daemon was already stopped
	#   2 if daemon could not be stopped
	#   other if a failure occurred
	start-stop-daemon --stop --quiet --retry=TERM/5/KILL/5  --pidfile $PIDFILE --name $NAME
	RETVAL=$?

	if [ $RETVAL = 2 ]; then
	    sleep 1
	    start-stop-daemon --stop --quiet --retry=TERM/5/KILL/5 --exec $DAEMON
	    RETVAL=$?
	fi

	return $RETVAL
}


case "$1" in
   start)
	[ "$VERBOSE" != no ] && log_daemon_msg "Starting $DESC" "$NAME"
	do_start
	case "$?" in
		0|1) [ "$VERBOSE" != no ] && log_end_msg 0 ;;
		2) [ "$VERBOSE" != no ] && log_end_msg 1 ;;
	esac
   ;;
  stop)
	[ "$VERBOSE" != no ] && log_daemon_msg "Stopping $DESC" "$NAME"
	do_stop
	case "$?" in
		0|1) [ "$VERBOSE" != no ] && log_end_msg 0 ;;
		2) [ "$VERBOSE" != no ] && log_end_msg 1 ;;
	esac
	;;
  restart)
	"$0" stop
	"$0" start
	;;
  status)
	status_of_proc "$DAEMON" "$NAME" && exit 0 || exit $?
	;;
   *)
	echo "Usage: $0 [start|stop|restart|status]" >&2
	exit 3
   ;;
esac

:
