add_library(common_pico INTERFACE)
target_sources(common_pico INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/i_glob.c
        ${CMAKE_CURRENT_LIST_DIR}/i_input.c
        ${CMAKE_CURRENT_LIST_DIR}/i_system.c
        ${CMAKE_CURRENT_LIST_DIR}/piconet.c
        ${CMAKE_CURRENT_LIST_DIR}/i_timer.c
        ${CMAKE_CURRENT_LIST_DIR}/i_video.c
        ${CMAKE_CURRENT_LIST_DIR}/pico_hdmi_glue.c
        ${CMAKE_CURRENT_LIST_DIR}/stubs.c

        ${CMAKE_CURRENT_LIST_DIR}/i_picosound.c
)
if (PICO_ON_DEVICE)
    target_sources(common_pico INTERFACE
            ${CMAKE_CURRENT_LIST_DIR}/blit.S
            ${CMAKE_CURRENT_LIST_DIR}/picoflash.c
            ${CMAKE_CURRENT_LIST_DIR}/i_usbhid.cpp
            )
    pico_wrap_function(common_pico malloc)
    pico_wrap_function(common_pico calloc)
    pico_wrap_function(common_pico free)
    target_compile_definitions(common_pico INTERFACE
            PICO_HEAP_SIZE=0
            USE_ZONE_FOR_MALLOC=1
    )
    # hardware_i2c: TLV320 codec I2C. pico_shared_nespad: legacy NES/SNES
    # pads on PIO — compiled everywhere, only polled when the board's cflags
    # header defines NES_PIN_CLK != -1 (see i_usbhid.cpp).
    target_link_libraries(common_pico INTERFACE  hardware_i2c pico_shared_nespad)
endif()
target_include_directories(common_pico INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/..
        ${CMAKE_CURRENT_LIST_DIR}/../../textscreen
        )

target_compile_definitions(common_pico INTERFACE
        PICO_USE_SW_SPIN_LOCKS=0
        PICO_USE_STACK_GUARDS=0 # todo we can actually use these sensibly, but right now we do overflow!
)

target_link_libraries(common_pico INTERFACE pico_stdlib pico_multicore)

if (USE_HSTX)
    target_compile_definitions(common_pico INTERFACE USE_HSTX=1)
    target_link_libraries(common_pico INTERFACE
            pico_hdmi
            pico_audio_i2s_shared
            tlv320dac3100
            hardware_dma
            hardware_pio
            hardware_i2c
    )
else()
    pico_generate_pio_header(common_pico ${CMAKE_CURRENT_LIST_DIR}/video_doom.pio)
    target_link_libraries(common_pico INTERFACE pico_scanvideo_dpi)
    target_compile_definitions(common_pico INTERFACE USE_HSTX=0)
endif()

add_library(pico_cd INTERFACE)
if (TARGET tinyusb_host)
    # ENABLE_PIO_USB picks the USB host transport at link time and must agree
    # with the HAS_USBPIO macro in the board's cflags header (a PIO header
    # without this link fails loudly on the missing pio_usb includes; the
    # native boards pass -DENABLE_PIO_USB=0). Default ON so the original
    # fruitjam build scripts keep working unchanged.
    if (NOT DEFINED ENABLE_PIO_USB)
        set(ENABLE_PIO_USB 1)
    endif()
    target_link_libraries(pico_cd INTERFACE tinyusb_host pico_shared_usb_hid)
    if (ENABLE_PIO_USB)
        target_link_libraries(pico_cd INTERFACE tinyusb_pico_pio_usb)
    endif()
endif()
