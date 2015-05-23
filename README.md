GroundTraffic kit for X-Plane<sup>®</sup>
====

This kit allows [X-Plane](x-plane.com) scenery designers to add animated ground vehicle traffic to airport scenery packages.

The repo contains assets intended for scenery designers, plus the source code to the X-Plane plugin that performs the animations. Scenery designer oriented documention is contained in the file [ReadMe.html](http://htmlpreview.github.io/?https://raw.githubusercontent.com/Marginal/GroundTraffic/master/ReadMe.html).

Building the plugin
----
The plugin is built from the `src` directory.

Mac 32 & 64 bit fat binary:

    make -f Makefile.mac

Linux 32 & 64 bit binaries:

    make -f Makefile.lin

Windows 32 or 64 bit binary:

    vcvarsall [target]
    nmake -f Makefile.win

