#!/bin/env python

from PIL import Image
import sys

def pixToHex(pix):
	return "{0:02x}{1:02x}{2:02x}".format(pix[0], pix[1], pix[2])

if len(sys.argv) < 2:
	raise Exception("Please specify a filename")
	sys.exit(1)

filename = sys.argv[1]
image = Image.open(filename)

for y in range(image.height):
	for x in range(image.width):
		if image.getpixel((x,y))[3] > 50:
			print("PX {0} {1} {2}".format(x, y, pixToHex(image.getpixel((x,y)))))
