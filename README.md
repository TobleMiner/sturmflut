Sturmflut
=========

** IMPORTANT **

This version of sturmflut uses the sendfile syscall to achieve very high speeds but many pixelflut implementations are flawed and do not work well with sendfile.
Please check out the [slowwrite](https://github.com/TobleMiner/sturmflut/tree/slowwrite) branch if you are experiencing issues. If you are searching for a nice
pixelflut server check out [shoreline](https://github.com/TobleMiner/shoreline) a super fast pixelflut server written in C.

# Compiling

Use ```make```.

# Usage

1. Convert image file to pixelflut commands using ```mktxt.py``` for images without pixels with alpha channel or ```mktxtalpha.py``` for ones with alpha pixels. This requires the pillow image library for python.

2. Run the pixelflut binary sending the generated txt file
