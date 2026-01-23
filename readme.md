Currently only for https://github.com/cepelinas9000/xserver/tree/wip/x11hdr

This project is with git submodule, you need:
```
git submodule init
git submodule update
```
or use switch during clone


**This only tested on 10 bit and only modesetting driver with NVIDIA 590.48.01 using NVIDIA GeForce 4080 **

This is working the following:

When layer enabled (by default 10bit hdr) it return only hdr surfaces (not checks), for example:
```
...
       Formats: count = 3
                SurfaceFormat[0]:
                        format = FORMAT_A2B10G10R10_UNORM_PACK32
                        colorSpace = COLOR_SPACE_PASS_THROUGH_EXT
                SurfaceFormat[1]:
                        format = FORMAT_A2B10G10R10_UNORM_PACK32
                        colorSpace = COLOR_SPACE_HDR10_ST2084_EXT
                SurfaceFormat[2]:
                        format = FORMAT_A2B10G10R10_UNORM_PACK32
                        colorSpace = COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT
...
```

Then when `CreateSwapchainKHR` is called:
* latches depth, bpp from surface format and visual from HDRColor class (id 6)
* Changes `XWindow` depth and visual class according surface format (currently depth 32 or depth 64)
* When dri3 pixmap are created uses latched depth & bpp
* when lower layer call executes unlatches


To enable you need small xorg.conf:
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

After starting you need envirioment variables for layer (don't need install):
```bash
export VK_ADD_IMPLICIT_LAYER_PATH=/home/to/VK_xlibre_layer/build/src # path to json metadata json (it only one here)
export ENABLE_XLIBRE_WSI=1

# you can check with vulkaninfo
vulkaninfo
# change in VkLayer_xlibre_wsi.x86_64.json library path to absolute location of libVkLayer_xlibre_wsi.so
#then start mpv in passthough mode:

mpv hdr_video.mp4 --no-config --vo=gpu-next --hwdec=vulkan-copy --target-trc=pq --target-colorspace-hint=yes --msg-level=vo=v

```
