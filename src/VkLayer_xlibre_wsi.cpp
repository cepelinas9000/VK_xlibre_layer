#define VK_USE_PLATFORM_XCB_KHR
#define VK_USE_PLATFORM_XLIB_KHR

#include "vkroots.h"

#include <xcb/xcb.h>
#include <xcb/xproto.h>
#include <xcb/xcbext.h>

#include <X11/Xlib.h>
#include <X11/Xlibint.h>
#include <X11/Xutil.h>

#include <stdlib.h>

#define HDRColor 6

/* tipped by https://github.com/Quackdoc and adapted from https://github.com/Zamundaaa/VK_hdr_layer.git  */
using namespace std::literals;

namespace XlibreLayer {

/**
 * @brief getFilteredSurfaceList
 * XXX: there should be some sort check
 * @return
 */
static std::vector<VkSurfaceFormat2KHR> getFilteredSurfaceList(){

    const char *mode = std::getenv("XLIBRE_HDR_MODE");
    std::string hdr_mode  = std::string(mode == nullptr ? "10i" : mode);

    std::vector<VkSurfaceFormat2KHR> surfaces;


    /*
        if (hdr_mode == "8i"s){
            surfaces.push_back(VkSurfaceFormat2KHR{ .sType = VK_STRUCTURE_TYPE_SURFACE_FORMAT_2_KHR, .pNext = nullptr, .surfaceFormat = {VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_PASS_THROUGH_EXT} });
        };
    */

        if (hdr_mode == "10i"s || hdr_mode == "16f"s){
            surfaces.push_back(VkSurfaceFormat2KHR{ .sType = VK_STRUCTURE_TYPE_SURFACE_FORMAT_2_KHR, .pNext = nullptr, .surfaceFormat = {VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_COLOR_SPACE_PASS_THROUGH_EXT} });
            surfaces.push_back(VkSurfaceFormat2KHR{ .sType = VK_STRUCTURE_TYPE_SURFACE_FORMAT_2_KHR, .pNext = nullptr, .surfaceFormat = {VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_COLOR_SPACE_HDR10_ST2084_EXT} });
            surfaces.push_back(VkSurfaceFormat2KHR{ .sType = VK_STRUCTURE_TYPE_SURFACE_FORMAT_2_KHR, .pNext = nullptr, .surfaceFormat = {VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT} });
        };

        if (hdr_mode == "16f"s){
            surfaces.push_back(VkSurfaceFormat2KHR{ .sType = VK_STRUCTURE_TYPE_SURFACE_FORMAT_2_KHR, .pNext = nullptr, .surfaceFormat = {VK_FORMAT_R16G16B16A16_SFLOAT, VK_COLOR_SPACE_PASS_THROUGH_EXT} });
            surfaces.push_back(VkSurfaceFormat2KHR{ .sType = VK_STRUCTURE_TYPE_SURFACE_FORMAT_2_KHR, .pNext = nullptr, .surfaceFormat = {VK_FORMAT_R16G16B16A16_SFLOAT, VK_COLOR_SPACE_HDR10_ST2084_EXT} });
            surfaces.push_back(VkSurfaceFormat2KHR{ .sType = VK_STRUCTURE_TYPE_SURFACE_FORMAT_2_KHR, .pNext = nullptr, .surfaceFormat = {VK_FORMAT_R16G16B16A16_SFLOAT, VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT} });

        }

    return surfaces;

}

void getSurfaceBits(VkFormat format, int &pixel_bits, int &bits_per_channel){

    switch (format) {
    case VK_FORMAT_B8G8R8A8_UNORM:
        pixel_bits = 32;
        bits_per_channel = 8;
        break;
    case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
    case VK_FORMAT_A2R10G10B10_UNORM_PACK32:
        pixel_bits = 32;
        bits_per_channel = 10;
        break;
    case VK_FORMAT_R16G16B16A16_SFLOAT:
        pixel_bits = 64;
        bits_per_channel = 16;
        break;
    case VK_FORMAT_R32G32B32A32_SFLOAT:
        pixel_bits = 128;
        bits_per_channel = 32;
        break;
    case VK_FORMAT_R64G64B64A64_SFLOAT:
        pixel_bits = 256;
        bits_per_channel = 64;
        break;
    case VK_FORMAT_B8G8R8A8_SRGB:
        pixel_bits = 32;
	bits_per_channel = 8;
        break;
    default:
        abort();
        break;
    }
}

/**
 * @brief latch_visual currently: relaxes window visual requiment - allows apply any visual and on the fly changes depth and on some use the depths, bit_per_rgb values
 * my minimal playing around showed `CreateSwapchainKHR` is called once which need attention
 *
 * @note it is in prototype stage thus why it written like this
 * @param conn
 * @param visual
 * @param depth
 * @param bits_per_rgb_color
 */
static void latch_visual(xcb_connection_t *conn, xcb_visualid_t visual, uint8_t depth,uint8_t bits_per_rgb_color){


    xcb_protocol_request_t proto_req = {
        .count = 1,
        .ext = NULL,
        .opcode = 125,
        .isvoid = 1
    };

    uint32_t buffer[8]; // remainder after req is 7 CARD32 words
    memset(buffer,0,sizeof(buffer));

    buffer[0] = ((uint32_t)depth) << 8;
    buffer[1] = 0x55aa55aa ; // wid field
    buffer[2] = 0xffffffff; // parent field
    buffer[6] = visual; // visual field
    buffer[7] = bits_per_rgb_color * 3; // mask field
    struct iovec vec[1] = { { .iov_base = buffer, .iov_len = sizeof(buffer)} };
    xcb_send_request(conn, 0, vec, &proto_req);

}

/**
 * @brief latch_visualXlib same as latch_visual
 * @param dpy
 * @param visual
 * @param depth
 * @param bits_per_rgb_color
 */
static void latch_visualXlib(Display *dpy,VisualID visual, uint8_t depth,uint8_t bits_per_rgb_color){
    xCreateWindowReq *req;



    XLockDisplay(dpy);

    GetReq(CreateWindow, req);

    req->reqType = 125;
    req->depth = depth;
    req->wid = 0x55aa55aa;
    req->parent = 0xffffffff;
    req->visual = visual;
    req->mask = bits_per_rgb_color;


    XUnlockDisplay(dpy);

}
/**
 * @brief find_hdr_visual
 * XServer cares that visual depth to be equal of pixel_size in bytes
 * @param pCreateInfo
 * @param pixel_size the size of pixel in bits (it should be divisible by 8)
 * @param bits_per_channel (how many bits are in one channel)
 * @return
 */
xcb_visualid_t find_hdr_visual(xcb_connection_t*connection, int pixel_bps, int bits_per_rgb_value ){

    //   taken and adapted from https://gitlab.freedesktop.org/mesa/mesa/-/blame/main/src/vulkan/wsi/wsi_common_x11.c?ref_type=heads#L486

    const xcb_setup_t *setup = xcb_get_setup (connection);
    xcb_screen_iterator_t iter = xcb_setup_roots_iterator (setup);

    /* XXX: only works only on screen 0 */


     xcb_screen_t *screen = iter.data;

     xcb_depth_iterator_t depth_iter =
         xcb_screen_allowed_depths_iterator(screen);


     for (; depth_iter.rem; xcb_depth_next (&depth_iter)) {
         if (depth_iter.data->depth != pixel_bps) {
             continue;
         }
         xcb_visualtype_iterator_t visual_iter =
             xcb_depth_visuals_iterator (depth_iter.data);

         for (; visual_iter.rem; xcb_visualtype_next (&visual_iter)) {
             if (visual_iter.data->_class == HDRColor && visual_iter.data->bits_per_rgb_value == bits_per_rgb_value) {
                 return visual_iter.data->visual_id;
             }
         }
     }

     return 0;

}


struct XlibreSurfaceData {

    bool is_xcb = false;
    xcb_connection_t*             connection;
    xcb_window_t                  window;
    Display *xdpy;
    uint64_t  xwnd;
};

VKROOTS_DEFINE_SYNCHRONIZED_MAP_TYPE(XlibreSurface, VkSurfaceKHR);

class VkInstanceOverrides
{
public:

    static VkResult GetPhysicalDeviceSurfaceFormatsKHR(
        const vkroots::VkInstanceDispatch *pDispatch,
        VkPhysicalDevice physicalDevice,
        VkSurfaceKHR surface,
        uint32_t *pSurfaceFormatCount,
        VkSurfaceFormatKHR *pSurfaceFormats)
    {

        fprintf(stderr, "[XLibre Layer] GetPhysicalDeviceSurfaceFormatsKHR called\n");


        std::vector<VkSurfaceFormat2KHR> formats = getFilteredSurfaceList();

        if (formats.size() == 0){
            fprintf(stderr, "[XLibre Layer] GetPhysicalDeviceSurfaceFormatsKHR no HDRColor visuals found  - pass though driver formats \n");
            return pDispatch->GetPhysicalDeviceSurfaceFormatsKHR(physicalDevice,surface,pSurfaceFormatCount,pSurfaceFormats);
        }

        *pSurfaceFormatCount = formats.size();


        if (pSurfaceFormats == nullptr){
            return VK_SUCCESS;
        }

        std::vector<VkSurfaceFormatKHR> extraFormats = {};

        size_t i = 0;
        for (const auto &desc : formats) {
            extraFormats.push_back(desc.surfaceFormat);
            pSurfaceFormats[i++] = desc.surfaceFormat;

            fprintf(stderr, "[XLibre Layer] Enabling format: %s colorspace: %s\n", vkroots::helpers::enumString(desc.surfaceFormat.format), vkroots::helpers::enumString(desc.surfaceFormat.colorSpace));
        }


        return VK_SUCCESS;
    }

    static VkResult GetPhysicalDeviceSurfaceFormats2KHR(const vkroots::VkInstanceDispatch *pDispatch,
                                                        VkPhysicalDevice physicalDevice,
                                                        const VkPhysicalDeviceSurfaceInfo2KHR* pSurfaceInfo,
                                                        uint32_t* pSurfaceFormatCount,
                                                        VkSurfaceFormat2KHR* pSurfaceFormats){

        fprintf(stderr, "[XLibre Layer] GetPhysicalDeviceSurfaceFormats2KHR called\n");

        std::vector<VkSurfaceFormat2KHR> formats = getFilteredSurfaceList();


        if (formats.size() == 0){
            fprintf(stderr, "[XLibre Layer] GetPhysicalDeviceSurfaceFormatsKHR no HDRColor visuals found  - pass though driver formats \n");
            return pDispatch->GetPhysicalDeviceSurfaceFormats2KHR(physicalDevice,pSurfaceInfo,pSurfaceFormatCount,pSurfaceFormats);
        }


        *pSurfaceFormatCount = formats.size();

        if (pSurfaceFormats == nullptr){
            return VK_SUCCESS;
        }

        size_t i = 0;
        for (const auto &desc : formats) {
            fprintf(stderr, "[XLibre Layer] Enabling format: %s colorspace: %s\n", vkroots::helpers::enumString(desc.surfaceFormat.format), vkroots::helpers::enumString(desc.surfaceFormat.colorSpace));
            pSurfaceFormats[i++] = desc;
        }



        return  VK_SUCCESS;

    }


    static VkResult CreateXcbSurfaceKHR (const vkroots::VkInstanceDispatch *pDispatch,
                                    VkInstance instance,
                                    const VkXcbSurfaceCreateInfoKHR* pCreateInfo,
                                    const VkAllocationCallbacks* pAllocator,
                                    VkSurfaceKHR* pSurface) {

        fprintf(stderr, "[XLibre Layer] Begin CreateXcbSurfaceKHR\n");

        auto result = pDispatch->CreateXcbSurfaceKHR(instance,pCreateInfo,pAllocator, pSurface);

            auto xlibreSurface = XlibreSurface::create(*pSurface, XlibreSurfaceData {
                .is_xcb = true,
                .connection = pCreateInfo->connection,
                .window = pCreateInfo->window
            });


        return result;
    }


    static VkResult CreateXlibSurfaceKHR(const vkroots::VkInstanceDispatch *pDispatch,
                                  VkInstance instance,
                                  const VkXlibSurfaceCreateInfoKHR* pCreateInfo,
                                  const VkAllocationCallbacks* pAllocator,
                                  VkSurfaceKHR* pSurface){


        fprintf(stderr, "[XLibre Layer] Warning  CreateXlibSurfaceKHR called - hdr implementation as is\n");

        auto result = pDispatch->CreateXlibSurfaceKHR(instance,pCreateInfo,pAllocator,pSurface);

        auto xlibreSurface = XlibreSurface::create(*pSurface, XlibreSurfaceData {
                                                                  .is_xcb = false,
                                                                  .connection = nullptr,
                                                                  .window = 0,
                                                                  .xdpy = pCreateInfo->dpy,
                                                                  .xwnd = pCreateInfo->window,
                                                              });


        return result;

    }

};


class VkDeviceOverrides
{
public:
    /**
     * @brief SetHdrMetadataEXT
     * The metadata will be applied to the specified VkSwapchainKHR objects at the next vkQueuePresentKHR call using that VkSwapchainKHR object. The metadata will persist until a subsequent vkSetHdrMetadataEXT changes it.
     * @see https://registry.khronos.org/VulkanSC/specs/1.0-extensions/man/html/vkSetHdrMetadataEXT.html#_description
     * @param pDispatch
     * @param device
     * @param swapchainCount
     * @param pSwapchains
     * @param pMetadata
     */
    static void SetHdrMetadataEXT(
        const vkroots::VkDeviceDispatch *pDispatch,
        VkDevice device,
        uint32_t swapchainCount,
        const VkSwapchainKHR *pSwapchains,
        const VkHdrMetadataEXT *pMetadata){

        for (uint32_t i = 0; i < swapchainCount; i++) {

            const VkHdrMetadataEXT &metadata = pMetadata[i];

            fprintf(stderr, "[HDR Layer] VkHdrMetadataEXT: mastering luminance min %f nits, max %f nits\n", metadata.minLuminance, metadata.maxLuminance);
            fprintf(stderr, "[HDR Layer] VkHdrMetadataEXT: maxContentLightLevel %f nits\n", metadata.maxContentLightLevel);
            fprintf(stderr, "[HDR Layer] VkHdrMetadataEXT: maxFrameAverageLightLevel %f nits\n", metadata.maxFrameAverageLightLevel);

            fprintf(stderr, "[HDR Layer] VkHdrMetadataEXT: not implemented !!!!\n");
            //XXX: not implemented yet
            // On X probably we want : to have immediate effect, before next flip or on later flip (present extension)

        }

    }


    static VkResult CreateSwapchainKHR(
        const vkroots::VkDeviceDispatch* pDispatch,
        VkDevice                   device,
        const VkSwapchainCreateInfoKHR*  pCreateInfo,
        const VkAllocationCallbacks*     pAllocator,
        VkSwapchainKHR*            pSwapchain) {


        auto XlibreSurface = XlibreSurface::get(pCreateInfo->surface);



        if (!XlibreSurface){
            fprintf(stderr, "[XLibre Layer] CreateSwapchainKHR is called not on XlibreSurface\n");
            return pDispatch->CreateSwapchainKHR(device,pCreateInfo,pAllocator,pSwapchain);
        }

        if (XlibreSurface->is_xcb){
            fprintf(stderr, "[XLibre Layer] CreateSwapchainKHR XlibreLayer on XCB\n");

        } else {
            fprintf(stderr, "[XLibre Layer] CreateSwapchainKHR XlibreLayer on XLIB\n");
        }


        int pixel_bps;
        int bits_per_rgb_value;

        getSurfaceBits(pCreateInfo->imageFormat,pixel_bps,bits_per_rgb_value);

        if (XlibreSurface->is_xcb){
            xcb_visualid_t vis = find_hdr_visual(XlibreSurface->connection,pixel_bps,bits_per_rgb_value);

            if (vis == 0){
                fprintf(stderr, "[XLibre Layer] CreateXcbSurfaceKHR cannot find visual for pixel_bps=%d bits_per_rgb_value=%d  probably xlibre hdr innactive trying 0x24 visual (it should be VK_FORMAT_A2B10G10R10_UNORM_PACK32)\n",pixel_bps,bits_per_rgb_value);
                vis = 0x24;
            }


            xcb_colormap_t colormap = xcb_generate_id(XlibreSurface->connection);
            xcb_create_colormap(
                XlibreSurface->connection,
                XCB_COLORMAP_ALLOC_NONE,  // We'll allocate colors ourselves
                colormap,
                XlibreSurface->window,
                vis
                );

            uint32_t cmap_values[] = { colormap };
            xcb_change_window_attributes(
                XlibreSurface->connection,
                XlibreSurface->window,
                XCB_CW_COLORMAP,
                cmap_values
                );

            latch_visual(XlibreSurface->connection,vis,pixel_bps,bits_per_rgb_value);



        } else {

            XVisualInfo vinfo;

            if (XMatchVisualInfo(XlibreSurface->xdpy,0,pixel_bps,HDRColor,&vinfo) == 0) {
                fprintf(stderr, "[XLibre Layer] No HDRColor visual found,  trying 0x24 (it should be VK_FORMAT_A2B10G10R10_UNORM_PACK32)\n");
                latch_visualXlib(XlibreSurface->xdpy,0x24,pixel_bps,bits_per_rgb_value);

            } else {

                latch_visualXlib(XlibreSurface->xdpy,vinfo.visualid,pixel_bps,bits_per_rgb_value);

                Colormap colormap = XCreateColormap(XlibreSurface->xdpy,
                                           XlibreSurface->xwnd,
                                           vinfo.visual,
                                           AllocNone);  // AllocNone means we'll allocate colors manually

                XSetWindowColormap(XlibreSurface->xdpy,XlibreSurface->xwnd,colormap);
                fprintf(stderr, "[XLibre Layer] Changing window (XID:0x%lx) colormap to 0x%lx\n",XlibreSurface->xwnd,colormap);
            }

        }

        VkResult r = pDispatch->CreateSwapchainKHR(device,pCreateInfo,pAllocator,pSwapchain);

        if (XlibreSurface->is_xcb){
                latch_visual(XlibreSurface->connection,0,0,0);
        } else {
            latch_visualXlib(XlibreSurface->xdpy,0,0,0);
        }
        return r;
    }

};

};
VKROOTS_DEFINE_LAYER_INTERFACES(XlibreLayer::VkInstanceOverrides,
                                vkroots::NoOverrides,
                                XlibreLayer::VkDeviceOverrides);

VKROOTS_IMPLEMENT_SYNCHRONIZED_MAP_TYPE(XlibreLayer::XlibreSurface);
