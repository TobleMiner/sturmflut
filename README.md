Sturmflut
=========

# Compiling

Use ```make```.

# Usage

1. Convert image file to pixelflut commands using ```mktxt.py``` for images without pixels with alpha channel or ```mktxtalpha.py``` for ones with alpha pixels. This requires the pillow image library for python.

2. Run the pixelflut binary sending the generated txt file.


If you do run into issues and get frequent connection resets or painting on the server
does not work try invoking sturmflut with ```-m 1```. This will stop sturmflut from using
the sendfile syscall and offers higher compatibility with broken pixelflut server implementations.

If you are searching for a fast and well implemented pixelflut server check out [shoreline](https://github.com/TobleMiner/shoreline)
