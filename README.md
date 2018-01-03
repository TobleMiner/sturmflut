Sturmflut
=========

** IMPORTANT **
This is the slowwrite variant of Sturmflut. It does not use sendfile and offers higher compatibility with a wider range of pixelflut server implementations but is a lot (~5 times)
slower that the sendfile version. You can find the sendfile version in the [master](https://github.com/TobleMiner/sturmflut/tree/master) branch.

# Compiling

Use ```make```.

# Usage

1. Convert image file to pixelflut commands using ```mktxt.py``` for images without pixels with alpha channel or ```mktxtalpha.py``` for ones with alpha pixels. This requires the pillow image library for python.

2. Run the pixelflut binary sending the generated txt file
