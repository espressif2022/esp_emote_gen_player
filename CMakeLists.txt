set(COMPONENT_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(COMPONENT_REQUIRES json)

idf_component_register(
    SRCS
        "src/emote_gen_player.c"
        "src/emote_gen_player_assets.c"
        "src/fonts/font_puhui_basic_20_4.c"
    INCLUDE_DIRS "include"
    REQUIRES ${COMPONENT_REQUIRES}
)
