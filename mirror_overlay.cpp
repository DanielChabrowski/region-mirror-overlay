#include <xcb/render.h>
#include <xcb/shape.h>
#include <xcb/xcb.h>
#include <xcb/xproto.h>

#include <cstring>
#include <iostream>
#include <unistd.h>
#include <vector>

template <typename T> struct Vec2
{
    T x;
    T y;
};

struct Region
{
    int x, y, w, h; // Region relative to target window
    int dest_x, dest_y; // Position on overlay
};

std::vector<Region> get_regions()
{
    int base_x = 1115;
    int base_y = 350;

    return {
        { 353, 218, 32, 32, base_x + 36 * 0, base_y },
        { 353, 362, 32, 32, base_x + 36 * 1, base_y },
        { 480, 1117, 32, 32, base_x + 36 * 2, base_y },
        { 768, 1117, 32, 32, base_x + 36 * 3, base_y },
        { 948, 1117, 32, 32, base_x + 36 * 4, base_y },
        { 2389, 134, 32, 32, base_x + 36 * 5, base_y },
    };
}

xcb_window_t find_window_by_title(xcb_connection_t *c, xcb_window_t root, const char *title)
{
    // Use _NET_CLIENT_LIST to find windows in reparenting WMs (like i3)
    const char *atom_name = "_NET_CLIENT_LIST";
    auto atom_cookie = xcb_intern_atom(c, 0, strlen(atom_name), atom_name);
    auto atom_reply = xcb_intern_atom_reply(c, atom_cookie, nullptr);

    if(!atom_reply) return XCB_NONE;

    xcb_atom_t client_list = atom_reply->atom;
    free(atom_reply);

    auto cookie = xcb_get_property(c, 0, root, client_list, XCB_ATOM_WINDOW, 0, 1024);
    auto reply = xcb_get_property_reply(c, cookie, nullptr);

    if(!reply) return XCB_NONE;

    xcb_window_t result = XCB_NONE;
    if(reply->type == XCB_ATOM_WINDOW && reply->format == 32)
    {
        xcb_window_t *wins = (xcb_window_t *)xcb_get_property_value(reply);
        int len = xcb_get_property_value_length(reply) / 4;

        for(int i = 0; i < len; ++i)
        {
            auto cookie_prop = xcb_get_property(c, 0, wins[i], XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 0, 128);
            auto reply_prop = xcb_get_property_reply(c, cookie_prop, nullptr);

            if(reply_prop)
            {
                if(xcb_get_property_value_length(reply_prop) > 0)
                {
                    const char *name = (const char *)xcb_get_property_value(reply_prop);
                    int val_len = xcb_get_property_value_length(reply_prop);
                    std::vector<char> buf(val_len + 1, 0);
                    memcpy(buf.data(), name, val_len);

                    if(strstr(buf.data(), title))
                    {
                        result = wins[i];
                        free(reply_prop);
                        break;
                    }
                }
                free(reply_prop);
            }
        }
    }
    free(reply);
    return result;
}

// Find a 32-bit ARGB visual for transparency
xcb_visualid_t find_argb_visual(xcb_screen_t *screen)
{
    auto depth_iter = xcb_screen_allowed_depths_iterator(screen);
    for(; depth_iter.rem; xcb_depth_next(&depth_iter))
    {
        if(depth_iter.data->depth == 32)
        {
            auto vis_iter = xcb_depth_visuals_iterator(depth_iter.data);
            for(; vis_iter.rem; xcb_visualtype_next(&vis_iter))
            {
                if(vis_iter.data->_class == XCB_VISUAL_CLASS_TRUE_COLOR)
                {
                    return vis_iter.data->visual_id;
                }
            }
        }
    }
    return XCB_NONE;
}

// Find the XRender format associated with a specific Visual ID
xcb_render_pictformat_t find_visual_format(xcb_connection_t *c, xcb_visualid_t visual)
{
    auto cookie = xcb_render_query_pict_formats(c);
    auto reply = xcb_render_query_pict_formats_reply(c, cookie, nullptr);
    if(!reply) return XCB_NONE;

    auto screen_iter = xcb_render_query_pict_formats_screens_iterator(reply);
    for(; screen_iter.rem; xcb_render_pictscreen_next(&screen_iter))
    {
        auto depth_iter = xcb_render_pictscreen_depths_iterator(screen_iter.data);
        for(; depth_iter.rem; xcb_render_pictdepth_next(&depth_iter))
        {
            auto vis_iter = xcb_render_pictdepth_visuals_iterator(depth_iter.data);
            for(; vis_iter.rem; xcb_render_pictvisual_next(&vis_iter))
            {
                if(vis_iter.data->visual == visual)
                {
                    xcb_render_pictformat_t fmt = vis_iter.data->format;
                    free(reply);
                    return fmt;
                }
            }
        }
    }
    free(reply);
    return XCB_NONE;
}

const char *xcb_error_string(uint8_t code)
{
    switch(code)
    {
    case XCB_REQUEST:
        return "BadRequest";
    case XCB_VALUE:
        return "BadValue";
    case XCB_WINDOW:
        return "BadWindow";
    case XCB_ACCESS:
        return "BadAccess";
    case XCB_ALLOC:
        return "BadAlloc";
    case XCB_ATOM:
        return "BadAtom";
    case XCB_COLORMAP:
        return "BadColormap";
    case XCB_CURSOR:
        return "BadCursor";
    case XCB_DRAWABLE:
        return "BadDrawable";
    case XCB_FONT:
        return "BadFont";
    case XCB_G_CONTEXT:
        return "BadGC";
    case XCB_ID_CHOICE:
        return "BadIDChoice";
    case XCB_IMPLEMENTATION:
        return "BadImplementation";
    case XCB_LENGTH:
        return "BadLength";
    case XCB_MATCH:
        return "BadMatch";
    case XCB_NAME:
        return "BadName";
    case XCB_PIXMAP:
        return "BadPixmap";
    default:
        return "Unknown X error";
    }
}

void log_xcb_error(xcb_generic_error_t *error)
{
    printf("X error: code=%u (%s), major=%u, minor=%u, resource=0x%x\n", error->error_code,
        xcb_error_string(error->error_code), error->major_code, error->minor_code, error->resource_id);
}

int main(int argc, char **argv)
{
    if(argc < 2)
    {
        std::cerr << "program requires title of the target window to mirror" << std::endl;
        return 1;
    }

    const char *target_title = argv[1];

    Vec2<int> position_cache = { 0, 0 };
    Vec2<int> size_cache = { 0, 0 };

    xcb_connection_t *c = xcb_connect(nullptr, nullptr);
    if(xcb_connection_has_error(c)) return 1;

    auto setup = xcb_get_setup(c);
    auto screen = xcb_setup_roots_iterator(setup).data;

    xcb_window_t target = find_window_by_title(c, screen->root, target_title);
    if(target == XCB_NONE)
    {
        std::cerr << "Target window '" << target_title << "' not found" << std::endl;
        return 1;
    }

    uint16_t width = screen->width_in_pixels;
    uint16_t height = screen->height_in_pixels;

    xcb_visualid_t argb_visual = find_argb_visual(screen);
    if(argb_visual == XCB_NONE)
    {
        std::cerr << "No ARGB visual found\n";
        return 1;
    }

    xcb_colormap_t colormap = xcb_generate_id(c);
    xcb_create_colormap(c, XCB_COLORMAP_ALLOC_NONE, colormap, screen->root, argb_visual);

    xcb_window_t overlay = xcb_generate_id(c);
    uint32_t mask = XCB_CW_BACK_PIXEL | XCB_CW_BORDER_PIXEL | XCB_CW_OVERRIDE_REDIRECT | XCB_CW_COLORMAP;
    uint32_t values[] = { 0, 0, 1, colormap };

    xcb_create_window(c, 32, overlay, screen->root, 0, 0, width, height, 0,
        XCB_WINDOW_CLASS_INPUT_OUTPUT, argb_visual, mask, values);

    // Make overlay input click-through
    xcb_shape_rectangles(c, XCB_SHAPE_SO_SET, XCB_SHAPE_SK_INPUT, XCB_CLIP_ORDERING_UNSORTED,
        overlay, 0, 0, 0, nullptr);
    xcb_map_window(c, overlay);

    xcb_render_pictformat_t root_fmt = find_visual_format(c, screen->root_visual);
    xcb_render_pictformat_t overlay_fmt = find_visual_format(c, argb_visual);

    xcb_render_picture_t src_pic = xcb_generate_id(c);
    xcb_render_create_picture(c, src_pic, screen->root, root_fmt, 0, nullptr);

    xcb_render_picture_t dst_pic = xcb_generate_id(c);
    xcb_render_create_picture(c, dst_pic, overlay, overlay_fmt, 0, nullptr);

    std::vector<Region> regions = get_regions();

    while(true)
    {
        if(target == XCB_NONE)
        {
            xcb_window_t new_target_window = find_window_by_title(c, screen->root, target_title);
            if(new_target_window == XCB_NONE)
            {
                sleep(5);
                continue;
            }
            else
            {
                std::cout << "Found a new window: " << new_target_window << std::endl;
                target = new_target_window;
                xcb_map_window(c, overlay);
            }
        }

        auto cookie = xcb_translate_coordinates(c, target, screen->root, 0, 0);

        xcb_generic_error_t *translate_error;
        auto reply = xcb_translate_coordinates_reply(c, cookie, &translate_error);

        if(translate_error != nullptr)
        {
            log_xcb_error(translate_error);

            if(translate_error->error_code == XCB_WINDOW)
            {
                target = XCB_NONE;

                // hide the overlay
                xcb_unmap_window(c, overlay);
            }

            free(translate_error);
        }

        auto geom_r = xcb_get_geometry_reply(c, xcb_get_geometry(c, target), nullptr);

        if(reply && geom_r)
        {
            int wx = reply->dst_x;
            int wy = reply->dst_y;
            int ww = geom_r->width;
            int wh = geom_r->height;

            if(wx != position_cache.x || wy != position_cache.y || ww != size_cache.x ||
                wh != size_cache.y)
            {
                position_cache = { wx, wy };
                size_cache = { ww, wh };

                uint32_t cfg[] = { (uint32_t)wx, (uint32_t)wy, (uint32_t)ww, (uint32_t)wh };
                xcb_configure_window(c, overlay,
                    XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT,
                    cfg);
            }

            for(const auto &r : regions)
                xcb_render_composite(c, XCB_RENDER_PICT_OP_SRC, src_pic, XCB_NONE, dst_pic,
                    wx + r.x, wy + r.y, 0, 0, r.dest_x, r.dest_y, r.w, r.h);

            xcb_flush(c);
        }
        if(reply) free(reply);
        if(geom_r) free(geom_r);
        usleep(16000);
    }
    return 0;
}
