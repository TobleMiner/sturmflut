Sturmflut
=========

Sturmflut is a very fast pixelflut client with full IPv6 support written entirely in C.
It can handle both ordinary image files and animations.

# Compiling

## Dependencies

- pkg-config
- pthread
- ImageMagick
- MagickWand

On debian-based systems all required dependencies can be installed using

```apt-get install build-essential pkg-config libmagick++-dev libmagickwand-dev```

Use ```make```.

# Usage

```
./sturmflut <host> [file to send] [-p <port>] [-a <source ip address>] [-i <0|1>] [-t <number of threads>] [-m] [-o <offset-spec>] [-O] [-s <percentage>] [-S] [-h]

host: IP address of pixelflut server
file to send: Image/Animation to show

-p: Server port
-i: Ignore broken broken pipe
-t: Number of threads used for flooding
-m: Monochrome mode (send only 0|1 instead of RRGGBBAA)
-o: Offset at which to draw, format: <x-offset>:<y-offset>
-O: Optimize animations, send only pixels that have changed between frames
-s: Sparse mode, send only <percentage> of pixels
-S: Data saver mode, transmit every frame of an animation only once instead of flooding it
-h: Show usage
```

## Example

```
./sturmflut 127.0.0.1 animation.gif
```

Searching for a fast pixelflut server? Check out [shoreline](https://github.com/TobleMiner/shoreline)
