cmake_minimum_required(VERSION 3.16)
project(pet_display_host C CXX)
if(NOT LVGL_SOURCE OR NOT PET_ROOT)
  message(FATAL_ERROR "LVGL_SOURCE and PET_ROOT required")
endif()
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h" CACHE PATH "" FORCE)
add_subdirectory("${LVGL_SOURCE}" lvgl)
set(UI "${PET_ROOT}/firmware/pet/components/pet_ui")
add_executable(pet-font-test font_harness.c "${UI}/pet_fonts.c"
  "${UI}/fonts/pet_font_22.c" "${UI}/fonts/pet_font_28.c"
  "${UI}/fonts/pet_font_30.c" "${UI}/fonts/pet_font_rare16.c"
  "${UI}/fonts/muse_font_cjk_16.c")
target_include_directories(pet-font-test PRIVATE "${UI}")
target_link_libraries(pet-font-test PRIVATE lvgl m)
target_compile_options(pet-font-test PRIVATE -Wall -Wextra -Werror)
# Keep assertions active in the optimized native rendering harness.
target_compile_options(pet-font-test PRIVATE -UNDEBUG)
