#!/bin/sh

# Define variables
DAEMON="/usr/bin/aesdsocket" # Adjust if your binary is in a different location
DAEMON_NAME="aesdsocket"
PID_FILE="/var/run/aesdsocket.pid"
DAEMON_OPTS="-d" # daemon mode

# Function to start the daemon
start() {
  echo "Starting $DAEMON_NAME..."
  start-stop-daemon --start --quiet --pidfile "$PID_FILE" --exec "$DAEMON" -- $DAEMON_OPTS
  if [ $? -eq 0 ]; then
    echo "$DAEMON_NAME started."
  else
    echo "Failed to start $DAEMON_NAME."
  fi
}

# Function to stop the daemon
stop() {
  echo "Stopping $DAEMON_NAME..."
  start-stop-daemon --stop --quiet --pidfile "$PID_FILE" --exec "$DAEMON"
  if [ $? -eq 0 ]; then
    echo "$DAEMON_NAME stopped."
    rm -f "$PID_FILE" # Clean up the PID file
  else
    echo "Failed to stop $DAEMON_NAME."
  fi
}

# Function to get status of the daemon
status() {
  if [ -f "$PID_FILE" ]; then
    PID=$(cat "$PID_FILE")
    if ps -p "$PID" > /dev/null; then
      echo "$DAEMON_NAME is running with PID $PID."
      return 0
    else
      echo "$DAEMON_NAME is not running (PID file exists, but process not found)."
      return 1
    fi
  else
    echo "$DAEMON_NAME is not running (PID file not found)."
    return 1
  fi
}

# Process command line arguments
case "$1" in
  start)
    start
    ;;
  stop)
    stop
    ;;
  restart)
    stop
    start
    ;;
  status)
    status
    ;;
  *)
    echo "Usage: $0 {start|stop|restart|status}"
    exit 1
    ;;
esac

exit 0