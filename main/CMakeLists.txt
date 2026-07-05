idf_component_register(SRCS  
                    "main.c" 
                    "UI/ui.c" 
                    "UI/ui_helpers.c"
                    "UI/screens/ui_Primary.c"
                    "UI/screens/ui_splash.c"
                    "UI/components/ui_comp_hook.c"
                    "UI/images/ui_img_router_png.c"
                            
                            
                    INCLUDE_DIRS "." 
                        "../components" 
                        "../managed_components"
                        "UI"
                        "include")
