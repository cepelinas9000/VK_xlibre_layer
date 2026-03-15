vkroots_dep = dependency('vkroots')

xlibre_wsi_layer = shared_library('VkLayer_xlibre_wsi', 'VkLayer_xlibre_wsi.cpp', 
  dependencies     : [ vkroots_dep, xcb_dep, xlib_dep, x11xcb_dep ],
  install          : true )

out_lib_dir = join_paths(prefix, lib_dir)

configure_file(
    input         : 'VkLayer_xlibre_wsi.json.in',
    output        : 'VkLayer_xlibre_wsi.' + build_machine.cpu_family() + '.json',
    configuration : {'family' : build_machine.cpu_family(), 'lib_dir' : out_lib_dir },
    install       : true,
    install_dir   : join_paths(data_dir, 'vulkan', 'implicit_layer.d'),
)