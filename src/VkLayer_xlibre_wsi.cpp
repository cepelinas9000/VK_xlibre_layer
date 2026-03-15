#define VK_USE_PLATFORM_XCB_KHR
#define VK_USE_PLATFORM_XLIB_KHR

#include "vkroots.h"
#include <vulkan/vk_icd.h>

#include <stdlib.h>
#include <unistd.h>


#include <xcb/xcb.h>
#include <xcb/xproto.h>
#include <xcb/xcbext.h>

#include <X11/Xlib-xcb.h>

#include <X11/Xlib.h>
#include <X11/Xlibint.h>
#include <X11/Xutil.h>


#define HDRColor 6



/* tipped by https://github.com/Quackdoc and adapted from https://github.com/Zamundaaa/VK_hdr_layer.git  */
using namespace std::literals;

namespace XlibreLayer {

static xcb_extension_t xcb_hdrproto_id = { "HDRProto", 0 };

typedef struct {
    CARD8 reqType;
    CARD8   HDRReqType;
    CARD16  length;
    CARD16   deviceIdType;
    CARD16   deviceIdLen;
} xQuerySupportedXlibreSurfaces;

typedef struct {
    CARD8  size;         /* structure size in CARD32 words */
    CARD8  pad1;
    CARD16 visualClass;
    CARD32 format;      /* Vkformat */
    CARD32 colorSpace;  /* VkColorSpaceKHR */
    CARD32 pad3;
    CARD64 flags;

} xSupportedXlibreSurfacesSurfaceType;


typedef struct {
    BYTE type;              /* X_Reply */
    BYTE pad1;             /* depends on reply type */
    CARD16 sequenceNumber;  /* of last request received by server */
    CARD32 length;          /* 4 byte quantities beyond size of GenericReply */

    CARD32 pad00;
    CARD32 pad01;
    CARD32 pad02;
    CARD32 pad03;
    CARD32 pad04;
    CARD32 pad05;

} xSupportedXlibreSurfacesReply;


/**
 * @brief getFilteredSurfaceList
 * @return list of supported surfaces
 */
static std::vector<VkSurfaceFormat2KHR> getFilteredSurfaceList(xcb_connection_t *conn){

    xcb_protocol_request_t proto_req = {
        .count = 1,
        .ext = &xcb_hdrproto_id,
        .opcode = 1,
        .isvoid = 0
    };


    xcb_prefetch_extension_data(conn, &xcb_hdrproto_id);
    const xcb_query_extension_reply_t *hdrext    = xcb_get_extension_data(conn, &xcb_hdrproto_id);

    if ( hdrext->present == 0){
        return  std::vector<VkSurfaceFormat2KHR>();
    }

    uint32_t buffer[5]; /* XXX: here should be identifier e.g. /dev/dri/cardX */
    memset(&buffer,0,sizeof(buffer));

    struct iovec vec[1] = { { .iov_base = buffer, .iov_len = sizeof(buffer)} };
    int seq = xcb_send_request(conn, 0, vec, &proto_req);

    xcb_generic_error_t *e;

    xSupportedXlibreSurfacesReply *r = reinterpret_cast<xSupportedXlibreSurfacesReply *>(xcb_wait_for_reply(conn,seq,&e));
    xSupportedXlibreSurfacesSurfaceType*s = reinterpret_cast<xSupportedXlibreSurfacesSurfaceType*>(&r[1]);

    std::vector<VkSurfaceFormat2KHR> surfaces;

    size_t num_surfaces = r->length  / ( sizeof(xSupportedXlibreSurfacesSurfaceType) >> 2);


    for(size_t i=0;i<num_surfaces;++i){
         surfaces.push_back(VkSurfaceFormat2KHR{ .sType = VK_STRUCTURE_TYPE_SURFACE_FORMAT_2_KHR, .pNext = nullptr, .surfaceFormat = {(VkFormat)s[i].format, (VkColorSpaceKHR)s[i].colorSpace} });
     }


     //surfaces.push_back(VkSurfaceFormat2KHR{ .sType = VK_STRUCTURE_TYPE_SURFACE_FORMAT_2_KHR, .pNext = nullptr, .surfaceFormat = {VK_FORMAT_R16G16B16A16_SFLOAT, VK_COLOR_SPACE_HDR10_ST2084_EXT} });
     //surfaces.push_back(VkSurfaceFormat2KHR{ .sType = VK_STRUCTURE_TYPE_SURFACE_FORMAT_2_KHR, .pNext = nullptr, .surfaceFormat = {VK_FORMAT_A2B10G10R10_UINT_PACK32, VK_COLOR_SPACE_HDR10_ST2084_EXT} });

     return surfaces;

}

void getSurfaceBits(VkFormat format, int &pixel_bits, int &bits_per_channel){

    switch (format) {
    case VK_FORMAT_B8G8R8A8_UNORM:
    case VK_FORMAT_B8G8R8A8_SRGB:
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
    buffer[7] = bits_per_rgb_color;
    struct iovec vec[1] = { { .iov_base = buffer, .iov_len = sizeof(buffer)} };
    xcb_send_request(conn, 0, vec, &proto_req);

}

typedef enum {
    GraphicsObjectE_SCREEN = 0,
    GraphicsObjectE_WINDOW,
    GraphicsObjectE_COLORMAP,
    GraphicsObjectE_DEVICE, /* xrandr provider id */
    GraphicsObjectE_PIXMAP,
    GraphicsObjectE_COLORMAP_XRANDR_CRT,
    GraphicsObjectE_MAX = 255
} HDR_GraphicsObjectE;

typedef struct HDRGraphicObjectId {
    CARD16  flags_lo;
    CARD8   flags_hi;
    CARD8   GraphicsObjectE;
    CARD32  ObjectID;
} HDRGraphicObjectId;


typedef struct X11_HDR_VULKANSTRUCTCHAIN {
    CARD32 vkStructure;
    CARD16 structflags;
    CARD16 total_lo_size;
    CARD8  total_hi_size;
    CARD16 current_lo_size;
    CARD8  current_hi_size;
    CARD32 data[];
} X11_HDR_VULKANSTRUCTCHAIN;

typedef struct {
    CARD8   reqType;
    CARD8   HDRReqType;
    CARD16  length;
    CARD32  pad0;
    CARD64  flags;
    CARD32  msc;
    CARD32  pad;
    CARD16 objects_to_apply_size;
    CARD16 pad2;
    CARD32 pad3;

} xHDRApplyVulkanProperties;

static void apply_vulkan_struct(xcb_connection_t *conn, HDR_GraphicsObjectE trg_obj_type, XID obj_xid, X11_HDR_VULKANSTRUCTCHAIN *single_chain, size_t chain_size_bytes){

    xcb_protocol_request_t proto_req = {
        .count = 3,
        .ext = &xcb_hdrproto_id,
        .opcode = 2,
        .isvoid = 1
    };


   xHDRApplyVulkanProperties p;
   memset(&p,0,sizeof(p));

   p.objects_to_apply_size = 1;

   HDRGraphicObjectId trg;
   memset(&trg,0,sizeof(trg));

   trg.GraphicsObjectE = trg_obj_type;
   trg.ObjectID = obj_xid;

   single_chain->total_hi_size = single_chain->current_hi_size = 0;
   single_chain->total_lo_size = single_chain->current_lo_size = chain_size_bytes >> 2;
   single_chain->structflags = 0;

    struct iovec vec[3] = {
      { .iov_base = reinterpret_cast<uint32_t*>(&p), .iov_len = sizeof(p)},
      { .iov_base = reinterpret_cast<void*>(&trg),         .iov_len = sizeof(trg)},
      { .iov_base = reinterpret_cast<void*>(single_chain), .iov_len = chain_size_bytes},
      };

    xcb_send_request(conn, 0, vec, &proto_req);

};

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

    xcb_connection_t*             connection;
    xcb_window_t                  window;

};

VKROOTS_DEFINE_SYNCHRONIZED_MAP_TYPE(XlibreSurface, VkSurfaceKHR);


struct XlibreSwapChainData {
    xcb_connection_t*             connection;
    xcb_window_t                  window;

};

VKROOTS_DEFINE_SYNCHRONIZED_MAP_TYPE(XlibreSwapChain, VkSwapchainKHR)

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

        xcb_connection_t *conn = nullptr;

        auto s = reinterpret_cast<VkIcdSurfaceXcb*>(surface);
        if (s->base.platform == VK_ICD_WSI_PLATFORM_XLIB){
          conn = XGetXCBConnection(reinterpret_cast<VkIcdSurfaceXlib*>(surface)->dpy);
        } else {
          conn = s->connection;
        }

        std::vector<VkSurfaceFormat2KHR> formats = getFilteredSurfaceList(conn);

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

        xcb_connection_t *conn = nullptr;

        auto s = reinterpret_cast<VkIcdSurfaceXcb*>(pSurfaceInfo->surface);
        if (s->base.platform == VK_ICD_WSI_PLATFORM_XLIB){
          conn = XGetXCBConnection(reinterpret_cast<VkIcdSurfaceXlib*>(pSurfaceInfo->surface)->dpy);
        } else {
          conn = s->connection;
        }

        std::vector<VkSurfaceFormat2KHR> formats = getFilteredSurfaceList(conn);


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
                                                                  .connection = XGetXCBConnection(pCreateInfo->dpy),
                                                                  .window = (uint32_t)pCreateInfo->window,

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


                auto Xlibreswapchain = XlibreSwapChain::get(pSwapchains[i]);


                if (Xlibreswapchain.has()){

                    const VkHdrMetadataEXT &metadata = pMetadata[i];

                    fprintf(stderr, "[XLibre Layer] pSwapchain[%u] VkHdrMetadataEXT: mastering luminance min %f nits, max %f nits\n", i, metadata.minLuminance, metadata.maxLuminance);
                    fprintf(stderr, "[XLibre Layer] pSwapchain[%u] VkHdrMetadataEXT: maxContentLightLevel %f nits\n", i, metadata.maxContentLightLevel);
                    fprintf(stderr, "[XLibre Layer] pSwapchain[%u] VkHdrMetadataEXT: maxFrameAverageLightLevel %f nits\n", i, metadata.maxFrameAverageLightLevel);
                    fprintf(stderr, "[XLibre Layer] pSwapchain[%u] VkHdrMetadataEXT CIE XYZ White (%.3f, %.3f)\n", i, metadata.whitePoint.x,metadata.whitePoint.y);
                    fprintf(stderr, "[XLibre Layer] pSwapchain[%u] VkHdrMetadataEXT CIE XYZ Red: (%.3f,%.3f) Green: (%.3f, %.3f) Blue: (%.3f, %.3f)\n", i,
                        metadata.displayPrimaryRed.x,metadata.displayPrimaryRed.y,metadata.displayPrimaryGreen.x,metadata.displayPrimaryGreen.y,metadata.displayPrimaryBlue.x,metadata.displayPrimaryBlue.y);

                    fprintf(stderr, "[XLibre Layer] pSwapchain[%u] Probably not impemented !!!\n",i);4
                    VkHdrMetadataEXT chain;
                    memcpy(&chain,&pMetadata[i],sizeof(VkHdrMetadataEXT));
                    chain.pNext = nullptr;
                    apply_vulkan_struct(Xlibreswapchain->connection,GraphicsObjectE_WINDOW, Xlibreswapchain->window, reinterpret_cast<X11_HDR_VULKANSTRUCTCHAIN *>(&chain), sizeof(chain));
                } else {
                    pDispatch->SetHdrMetadataEXT(device,swapchainCount,pSwapchains,pMetadata);
                }
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


        fprintf(stderr, "[XLibre Layer] CreateSwapchainKHR XlibreLayer on XCB/XLIB\n");


        int pixel_bps;
        int bits_per_rgb_value;

        getSurfaceBits(pCreateInfo->imageFormat,pixel_bps,bits_per_rgb_value);

        if (bits_per_rgb_value <= 8){
            fprintf(stderr, "[XLibre Layer] CreateSwapchainKHR create SDR surface\n");
            return pDispatch->CreateSwapchainKHR(device,pCreateInfo,pAllocator,pSwapchain);
        }

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


            latch_visual(XlibreSurface->connection,vis,pixel_bps,bits_per_rgb_value);

            uint32_t cmap_values[] = { colormap };
            xcb_change_window_attributes(
                XlibreSurface->connection,
                XlibreSurface->window,
                XCB_CW_COLORMAP,
                cmap_values
                );


        {
          VkSurfaceFormat2KHR chain;
          chain.sType = VK_STRUCTURE_TYPE_SURFACE_FORMAT_2_KHR;
          chain.pNext = nullptr;
          chain.surfaceFormat.format     = pCreateInfo->imageFormat;
          chain.surfaceFormat.colorSpace = pCreateInfo->imageColorSpace;

          apply_vulkan_struct(XlibreSurface->connection,GraphicsObjectE_COLORMAP, colormap, reinterpret_cast<X11_HDR_VULKANSTRUCTCHAIN *>(&chain), sizeof(chain));
        }

        VkResult r = pDispatch->CreateSwapchainKHR(device,pCreateInfo,pAllocator,pSwapchain);


        XlibreSwapChain::create(*pSwapchain, XlibreSwapChainData {
            .connection = XlibreSurface->connection,
            .window = XlibreSurface->window,

        });

        latch_visual(XlibreSurface->connection,0,0,0);

        return r;
    }

};

};
VKROOTS_DEFINE_LAYER_INTERFACES(XlibreLayer::VkInstanceOverrides,
                                vkroots::NoOverrides,
                                XlibreLayer::VkDeviceOverrides);

VKROOTS_IMPLEMENT_SYNCHRONIZED_MAP_TYPE(XlibreLayer::XlibreSurface);
VKROOTS_IMPLEMENT_SYNCHRONIZED_MAP_TYPE(XlibreLayer::XlibreSwapChain);
