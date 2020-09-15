# EGBE
EGBE is an emulator for running GameBoy games on Linux.  Written in C with [SDL 2.0](https://www.libsdl.org/).  Currently supports...
- A handful of commercial games
- Persistent SRAM
- Save states
- Video RAM visualization
- Local link cable emulation
- Remote link cable emulation via [cURL](https://curl.haxx.se/) or [libwebsockets](https://libwebsockets.org/)
  - See [EGBE Link Hub](https://github.com/ssh-esoteric/egbe-link-hub) for a sample server implementation
  - A public version is available at [https://egbe.esoteric.website/links/](https://egbe.esoteric.website/links/)
- Ruby debugger

#### Samples

`SERIAL=local ./egbe tetris.gb`

![alt](https://egbe.s3-us-west-2.amazonaws.com/samples/egbe-tetris-2p.png)

`GBC=1 SERIAL=local CART1=crystal.gbc CART2=gold.gbc BOOT=sameboy_cgb.bin ./egbe`

![alt](https://egbe.s3-us-west-2.amazonaws.com/samples/egbe-crystal-2p.png)

## Controls

| Controls        |               |
| -------------   |:------------- |
| Arrow Keys      | Up, Down, Left, Right
| A               | A
| D               | B
| Right Shift     | Select
| Enter/Return    | Start
| **Save States** |               |
| F1 - F4         | Select save state 1 - 4
| F5              | Save state
| F8              | Load state
| **Audio**       |               |
| 1               | Toggle Square 1 audio channel
| 2               | Toggle Square 2 audio channel
| 3               | Toggle Wave audio channel
| 4               | Toggle Noise audio channel
| **Extra**       |               |
| Q, Escape       | Exit
| H               | Advance RTC by one hour
| J               | Advance RTC by one day
| Left Ctrl       | Swap focused Game Boy (with `SERIAL=local`)
| L               | Open remote link cable (with `SERIAL=curl` or `SERIAL=lws`; see requirements below)
| G               | Enter Ruby debugger shell (see requirements below)

## Runtime Flags

EGBE supports a number of environment variables to configure runtime behavior.

| Variable              | Description   |
| --------------------- |:------------- |
| `GBC=1`               | Launch EGBE in GBC mode
| `MUTED=1`             | Launch EGBE with audio muted (audio controls above still work)
| `BOOT=$file`          | Set path to Boot ROM file
| `CART=$file`          | Set path to ROM file
|                       | (Aliased as `BOOT1` and `CART1` below)
| **Local Link Cable**  |
| `SERIAL=local`        | Launch EGBE with two connected Game Boys (host on left, guest on right)
| `BOOT1=$file`         | Host: Set path to Boot ROM file
| `CART1=$file`         | Host: Set path to ROM file
| `BOOT2=$file`         | Guest: Set path to Boot ROM file (defaults to `BOOT1`)
| `CART2=$file`         | Guest: Set path to ROM file (defaults to `CART1`)
| **Remote Link Cable** |
| `SERIAL=curl`         | Enables remote link cables with cURL (see build requirements below)
| `SERIAL=lws`          | Enables remote link cables with libwebsockets (see build requirements below)
| `SERIAL_URL=$url`     | API endpoint to use for remote link cables

## Build Flags

At the moment, EGBE uses a simple Makefile for its build process.
Usually, any extra features that would require a new development library
(beyond SDL) can be enabled during the build process.

EX: `DEBUG=1 CURL=1 LWS=1 make -Bj9`

### Curl Link Cable Client

`CURL=1 make`

Requires the cURL development libraries and [JSON-C](https://github.com/json-c/json-c) libraries.

### LWS Link Cable Client

`LWS=1 make`

Requires the libwebsockets development libraries and [JSON-C](https://github.com/json-c/json-c) libraries.

### Ruby Debugger

`DEBUG=1 make`

Requires the Ruby development libraries.  When the debugger is first
launched, a file called `local.rb` will be loaded if it exists.

# License

GPL 3.0 or later
