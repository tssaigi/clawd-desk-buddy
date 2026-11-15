#!/bin/zsh
# Install the Clawd status bridge as a login item on macOS (a LaunchAgent).
# It creates a private Python environment next to this script, installs
# pyserial into it, and starts the bridge now and at every login.
# Remove it again with uninstall_macos.sh.
set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
LABEL="com.clawd-desk-buddy.bridge"
PLIST="$HOME/Library/LaunchAgents/$LABEL.plist"

python3 -m venv "$DIR/.venv"
"$DIR/.venv/bin/pip" install -q -r "$DIR/requirements.txt"

mkdir -p "$HOME/Library/LaunchAgents"
cat > "$PLIST" <<PLIST_END
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key><string>$LABEL</string>
  <key>ProgramArguments</key>
  <array>
    <string>$DIR/.venv/bin/python</string>
    <string>$DIR/clawd_status_bridge.py</string>
  </array>
  <key>RunAtLoad</key><true/>
  <key>KeepAlive</key><true/>
  <key>ThrottleInterval</key><integer>10</integer>
  <key>ProcessType</key><string>Background</string>
  <key>LowPriorityIO</key><true/>
  <key>Nice</key><integer>10</integer>
  <key>StandardOutPath</key><string>$DIR/bridge.log</string>
  <key>StandardErrorPath</key><string>$DIR/bridge.log</string>
</dict>
</plist>
PLIST_END

launchctl bootout "gui/$(id -u)/$LABEL" 2>/dev/null || true
launchctl bootstrap "gui/$(id -u)" "$PLIST"
echo "Bridge installed and running. Log: $DIR/bridge.log"
echo "Keep this folder where it is: the login item points at it."
