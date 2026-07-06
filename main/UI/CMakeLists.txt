SET(SOURCES screens/ui_splash.c
    screens/ui_Primary.c
    ui.c
    components/ui_comp_hook.c

    ui_helpers.c
    images/ui_img_router_png.c)

add_library(ui ${SOURCES})
