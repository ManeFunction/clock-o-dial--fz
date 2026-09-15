#!/bin/bash
# Launch app and handle cleanup on exit

APP_ID="crock_o_dail"
UFBT_CMD="ufbt"

# Function to stop the app
stop_app() {
    echo "Stopping app on Flipper..."
    echo "app_close $APP_ID" | $UFBT_CMD cli 2>/dev/null || true
    exit 0
}

# Trap SIGINT and SIGTERM to stop app when task is cancelled
trap stop_app SIGINT SIGTERM

# Launch the app
$UFBT_CMD launch

# If launch exits, stop the app
stop_app
