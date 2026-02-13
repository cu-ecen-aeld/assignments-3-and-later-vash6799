#!/bin/sh

DAEMON="aesdsocket"
DAEMON_PATH="/usr/bin/$DAEMON"
PIDFILE="/var/run/$DAEMON.pid"

case "$1" in
    start)
        echo "Starting $DAEMON"
        # -S: Start, -a: App path, -x: Executable, -b: Background (though -d handles this), -m: Make pidfile
        start-stop-daemon -S -n $DAEMON -a $DAEMON_PATH -- -d
        ;;
    stop)
        echo "Stopping $DAEMON"
        # -K: Kill, -n: Name, -s: Signal (SIGTERM is default)
        start-stop-daemon -K -n $DAEMON
        # Cleanup file just in case SIGTERM didn't finish (though code should handle it)
        rm -f /var/tmp/aesdsocketdata
        ;;
    *)
        echo "Usage: $0 {start|stop}"
        exit 1
esac

exit 0