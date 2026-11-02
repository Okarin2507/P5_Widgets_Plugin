#!/bin/bash
# Persona-only notifications:
#   persona          -> p5notify banners ON, DMS stock popups hidden (DND)
#   saber/elden-ring -> p5notify OFF, DND off, DMS stock popups back

# DMS also runs this script as the profile "saveCommand", where no profile slug
# is set. Without a slug there is nothing to do, and writing an empty value
# would wipe the profile file the plugin depends on.
[ -z "$DMS_PROFILE_SLUG" ] && exit 0

# Tell the p5notify plugin which profile is active. The plugin only draws
# banners while this file says "persona".
echo "$DMS_PROFILE_SLUG" > "$HOME/.config/dms/p5notify-profile"

case "$DMS_PROFILE_SLUG" in
    "saber"|"elden-ring")
        # Turn the P5 plugin off FIRST, then release DND.
        dms ipc call plugins disable p5notify 2>/dev/null
        dms ipc call notifications disableDoNotDisturb 2>/dev/null

        dms ipc call plugins enable depthscape 2>/dev/null
        sleep 0.2
        dms ipc call depthscape generate 2>/dev/null
        ;;
    "persona")
        dms ipc call plugins disable depthscape 2>/dev/null
        rm -rf ~/.cache/dms/depthscape/* 2>/dev/null
        pkill -f "cava" 2>/dev/null

        dms ipc call plugins enable personaMusic 2>/dev/null
        dms ipc call plugins enable p5notify 2>/dev/null
        # DMS wants a FUTURE EPOCH IN MILLISECONDS here (an ISO date is rejected).
        until_ms=$(( ( $(date +%s) + 315360000 ) * 1000 ))   # ~10 years from now
        dms ipc call notifications enableDoNotDisturbUntil "$until_ms" 2>/dev/null
        ;;
esac

# Profile-switch notification goes last, after the states above are set.
"$HOME/.local/bin/dms-profile-notify" "$DMS_PROFILE_SLUG"
