set(COMPONENT_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(COMPONENT_REQUIRES json esp_mmap_assets esp_emote_gfx_main)

idf_component_register(
    SRCS
        "src/emote_gen_player.c"
        "src/emote_gen_player_assets.c"
        "src/fonts/font_puhui_basic_20_4.c"
    INCLUDE_DIRS "include"
    REQUIRES ${COMPONENT_REQUIRES}
)
