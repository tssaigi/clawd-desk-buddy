#!/bin/zsh
# Pause or resume the bridge (it holds the Core2's serial port, so pause it
# before flashing):  ./pause.sh        stop it
#                    ./pause.sh resume start it again
LABEL="com.clawd-desk-buddy.bridge"
if [ "$1" = "resume" ]; then
  launchctl bootstrap "gui/$(id -u)" "$HOME/Library/LaunchAgents/$LABEL.plist" && echo "Bridge running."
else
  launchctl bootout "gui/$(id -u)/$LABEL" 2>/dev/null; echo "Bridge paused."
fi
