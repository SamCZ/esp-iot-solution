idf_component_register(SRCS infiray.c wave_1ch_16bits.c libusb.c libuvc.c
                    INCLUDE_DIRS "."
										PRIV_INCLUDE_DIRS "${IDF_PATH}/components/usb/private_include" "private_include"
										REQUIRES usb esp_ringbuf)
target_compile_options(${COMPONENT_LIB} PRIVATE "-Wno-address-of-packed-member")