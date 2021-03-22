<h1 align="center">
  dynamd
</h1>
<h3 align="center">dynamd is an extremely fast, light-weight, efficient, highly-customizable and dynamic window manager based on <a href=https://dwm.suckless.org>DWM</a> for X</h3>


## Build Requirements
* Xlib header files
* libxft
* libxcb
* libX11
* xcb

## Installation
```bash
<superuser> make --jobs "$(nproc || printf '%s\n' 1)" install
```

## Running dynamd
Add the following line to your `~/.xinitrc` to start dynamd using `startx`:
```bash
exec dynamd
```

## Java Applications
Java applications are known to misbehave as java doesn't know which WM is running. This results in GUI of specific java applications to not work properly. Therefore, <a href=https://tools.suckless.org/x/wmname>WMNAME</a> can be used and set it to `LG3D`, to solve the issue.
* Install <a href=https://tools.suckless.org/x/wmname>WMNAME</a> and execute `wmname LG3D` to fix Java applications misbehaving. To make it permanent it can either be added in the startup script (**`startup/startup.sh`**) or `~/.xinitrc`.

## LICENSE
The project is licensed under the MIT license. For more information, see the `LICENSE` file.
