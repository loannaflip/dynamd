#!/bin/sh
# shellcheck shell=sh # Written to be posix compatible

: '
MIT/X Consortium License

© 2020-2021 Angel Uniminin <uniminin@zoho.com>

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the "Software"),
to deal in the Software without restriction, including without limitation
the rights to use, copy, modify, merge, publish, distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.
'

#- Monitor Settings -#
primary_monitor="HDMI-1"
primary_monitor_res="1920x1080"
primary_monitor_rate="74.97"
primary_monitor_pos="0x0"
primary_monitor_rotate="normal"

secondary_monitor="DP-1"
secondary_monitor_res="1366x768"
secondary_monitor_rate="59.79"
secondary_monitor_pos="1920x312"
secondary_monitor_rotate="normal"


#- Wallpaper Settings -#
wallpaper_dir="/home/uniminin/dynamd/wallpapers"

primary_monitor_wall="blackstract.png"
secondary_monitor_wall="blackstract.png"


# Monitor #
xrandr --output "$primary_monitor" --primary \
         --mode   "$primary_monitor_res" \
         --rate   "$primary_monitor_rate" \
         --pos    "$primary_monitor_pos" \
         --rotate "$primary_monitor_rotate" \
       --output "$secondary_monitor" \
         --mode   "$secondary_monitor_res" \
         --rate   "$secondary_monitor_rate" \
         --pos    "$secondary_monitor_pos" \
         --rotate "$secondary_monitor_rotate"

# Wallpaper #
feh --bg-fill "$wallpaper_dir/$primary_monitor_wall" \
    --bg-fill "$wallpaper_dir/$secondary_monitor_wall"

while true; do
  xsetroot -name "$( date +"  %l:%M %p | %d/%m/%y | %a  " )"
  sleep 1m    # Update time every minute
done &

# Compositor #
picom --experimental-backends -b

# MISC #
wmname LG3D
xset -dpms
xset s off
