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

This fork by nst0022 (2020-04-23)
----

I have changed the code from XPLMDrawObjects to XPLMInstancing, which is mandatory for X-Plane 11.50 Vulkan.

As I am on Linux, I could not compile for Windows or Mac, but tested the changes in a separate configuration (64-bit Linux only).

In the make files for Windows and Mac, XPSDK213 needs probably to be replaced by XPSDK302, like in the make file for Linux, but I did not want to touch these files, nor did I change the version number.

I have marked all changes with 'nst0022' in the source code, which might be better readable than the diff here on GitHub.

The plugin works (here) now for the following custom sceneries:

- San Francisco Golden Gate Bridge
- San Francisco Oakland Bay Bridge
- San Francisco Airport Vehicles [1]
- San Francisco Cable Cars and Ferries [1]
- San Diego Coronado Bridge

covering the GroundTraffic statements 'route', 'highway', and 'train'.

I will not maintain the source code further on, because I have only a rudimentary understanding of what happens internally. This source code is an offer for scenery maintainers, who want to consider the changes for their own purposes.
____
[1] One peculiarity (fixed with version 1.62): In these two GroundTraffic.txt files, I had to replace all objects, that contained ANIM_begin/_end in their .obj files, which caused X-Plane to crash on first XPLMInstanceSetPosition, with their non-ANIM counter parts:
1. for San Francisco Cable Cars and Ferries I duplicated Ferry_SFO.obj to Ferry_SFO_no_anim.obj and removed the animation
2. for San Francisco Airport Vehicles I simply replaced all '/Active/' with '/' in GroundTraffic.txt

Of course, these changes should only show that it works. What has to be done for the custom sceneries has to be determined by their respective authors.

Update by nst0022 (2020-04-25)
----

Due to [this](https://forums.x-plane.org/index.php?/forums/topic/210452-groundtraffic-plugin-for-x1150-vulkan/&do=findComment&comment=1903841) comment in the org forum (QUOTE: I had one mail exchange with Jonathan a couple of month ago and his interests have wandered far away from flight simulation a few years ago. He has no interest at this time to pick up any activity on his past projects and he didn't sound like he will ever do again.) I have closed the pull request to avoid possible future confusion.

Update by nst0022 (2020-04-28)
----

Changes due to [this](https://forums.x-plane.org/index.php?/forums/topic/210452-groundtraffic-plugin-for-x1150-vulkan/&do=findComment&comment=1908165) comment in the org forum:

Version 1.61

- groundtraffic.c - fix cut&paste error (XPLMDetroyInstance)
- Makefile.win - change XPSDK version to 302
- Makefile.mac - change XPSDK version to 302

All other, Windows-specific changes in the above forum post need review by a knowlegable Windows user.

Especially, the changes need to be enclosed in #if IBM ... #endif

Update by nst0022 (2020-04-29)
----

Changes due to [this](https://developer.x-plane.com/2020/04/xplminstance-two-tricks/) communication between Ben Supnik and Yuri Lyubimkin as of 2020-04-28

Version 1.62

- groundtraffic.c and draw.c - fix crash when ANIM

Update by nst0022 (2020-04-30)
----

Version was still 1.61

Version 1.62 part 2

- version.mak - show version as 1.62

This only affects the version shown in X-Plane's PluginAdmin

Update by nst0022 (2020-05-03)
----

Changes due to [this](https://developer.x-plane.com/sdk/XPLMDisplay/) documentation (find "xplm_Phase_Modern3D" and read thru the rest of the paragraph):

Version 2.1

- draw.c, groundtraffic.c - replace xplm_Phase_Modern3D with xplm_Phase_Window

This should re-enable Mac users under Metal to use the plugin again.

NOTE: This update breaks "debug" mode, until someone redesigns it to use a different mechanism to draw on scenery.

Update by nst0022 (2020-05-06)
----

Changes due to [this](https://forums.x-plane.org/index.php?/forums/topic/210452-groundtraffic-plugin-for-x1150-vulkan/&page=2&tab=comments#comment-1909239) post:

Version 2.2

- draw.c, groundtraffic.c, groundtraffic.h - XPLMInstanceSetPosition not via draw callback but via flight loop

Status quo by nst0022 (2020-05-22)
----

- as of version 2.2, the plugin runs under 64-bit Linux and 64-bit Windows
- the plugin should work on a Mac, which still needs to be confirmed
- the plugin should run with reasonable performance
- "debug" mode is still broken and needs to be redesigned as mentioned above

Nice to have:

- the plugin should be redesigned to act as a general plugin, instead of an individual per custom scenery

Known bugs located in X-Plane itself:

- animations of wheel turnings are erratic (X11.50b9)

See [this](https://forums.x-plane.org/index.php?/forums/topic/210452-groundtraffic-plugin-for-x1150-vulkan) thread for a history on how this fork evolved

The plugin has currently no owner

A potential volunteer needs to:

- have a thorough understanding of the inner workings of the plugin
- have a thorough understanding of the X-Plane API
- be able to develop for three platforms (Windows, Mac, Linux)

This fork has now been frozen
