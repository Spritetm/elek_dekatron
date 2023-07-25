cmake_minimum_required(VERSION 3.5)

idf_component_register(
	SRCS "usbpd_esp.cpp" "usb-pd/src/fusb302b.cpp" "usb-pd/src/policy_engine.cpp" "usb-pd/src/policy_engine_states.cpp"
	INCLUDE_DIRS "." "usb-pd/include" "usb-pd/src"
	PRIV_REQUIRES "driver" "freertos" "esp_timer"
)

target_compile_options(${COMPONENT_LIB} PRIVATE -DPD_DEBUG_OUTPUT)
