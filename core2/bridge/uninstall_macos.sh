#!/bin/zsh
# Stop the Clawd status bridge and remove its login item.
LABEL="com.clawd-desk-buddy.bridge"
launchctl bootout "gui/$(id -u)/$LABEL" 2>/dev/null || true
rm -f "$HOME/Library/LaunchAgents/$LABEL.plist"
echo "Bridge stopped and removed."
