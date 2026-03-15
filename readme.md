Currently only for https://github.com/cepelinas9000/xserver/tree/wip/x11hdr

This project is with git submodule, you need:
```
git submodule init
git submodule update
```
or use specific git switch during clone

**This only tested on 16 bit half float and only modesetting driver with NVIDIA 590.48.01 using NVIDIA GeForce 4080 **

When layer enabled it will return XLibre server supported surfaces, for example:
```
...
        Formats: count = 4
                SurfaceFormat[0]:
                        format = FORMAT_B8G8R8A8_UNORM
                        colorSpace = COLOR_SPACE_SRGB_NONLINEAR_KHR
                SurfaceFormat[1]:
                        format = FORMAT_B8G8R8A8_SRGB
                        colorSpace = COLOR_SPACE_SRGB_NONLINEAR_KHR
                SurfaceFormat[2]:
                        format = FORMAT_R16G16B16A16_SFLOAT
                        colorSpace = COLOR_SPACE_HDR10_ST2084_EXT
                SurfaceFormat[3]:
                        format = FORMAT_R16G16B16A16_SFLOAT
                        colorSpace = COLOR_SPACE_BT2020_LINEAR_EXT
...
```

Then when `CreateSwapchainKHR` is called:
* latches depth, bpp from surface format and visual from HDRColor class (id 6)
* Changes `XWindow` depth and visual class according surface format (currently it is implemented and colormap change)
* When dri3 pixmap are created it uses latched depth & bpp (from visual style) and with aditional call provided colorSpace
* second latch call unlatches this behaviour


To enable you need add "HDR" to xorg.conf (currently only 16f supported):
```
Section "Screen"
    Identifier "default"
    HDR "16f"
    #HDR "10i"

EndSection

```

Then start X with it (don't forget libinput driver):
```
# X :2 vt2 -config /path/to/hdr-xorg.conf
```

After starting you need envirioment variables for layer (don't need install it - just enough build):
```bash
export VK_ADD_IMPLICIT_LAYER_PATH=/home/to/VK_xlibre_layer/build/src # path to json metadata json (it only one here)
export ENABLE_XLIBRE_WSI=1

# you can check with vulkaninfo
vulkaninfo
# change in VkLayer_xlibre_wsi.x86_64.json library path to absolute location of libVkLayer_xlibre_wsi.so
#then start mpv:

mpv hdr_video.mp4 --target-colorspace-hint=yes

```
