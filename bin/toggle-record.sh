#!/usr/bin/env bash
# Screen recording toggle for Umbriel + PipeWire (wf-recorder)

if pgrep -x "wf-recorder" >/dev/null; then
  pkill -INT wf-recorder
  notify-send "Screen Recorder" "Recording saved to Videos!"
else
  sink="$(pactl get-default-sink 2>/dev/null)"
  audio_dev="${sink:+$sink.monitor}"
  audio_dev="${audio_dev:-alsa_output.pci-0000_00_1f.3-platform-skl_hda_dsp_generic.HiFi__Speaker__sink.monitor}"

  # Detect active output from Umbriel if running, fallback to eDP-1
  output="$(umbriel outputs 2>/dev/null | awk '/^[A-Za-z0-9-]+/ {print $1; exit}')"
  output="${output:-eDP-1}"

  mkdir -p "$HOME/Videos"
  wf-recorder -o "$output" --audio="$audio_dev" -f "$HOME/Videos/recording-$(date +%Y%m%d_%H%M%S).mp4" &
  notify-send "Screen Recorder" "Recording started on $output (with audio)..."
fi
