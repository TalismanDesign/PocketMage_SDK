
SDK_ROOT ?= $(PM_SDK_ROOT)
APP_NAME ?= $(notdir $(CURDIR))

$(APP_NAME)_ICON.bin: assets/icon.png tools/make_icon.py
	python3 tools/make_icon.py assets/icon.png $@

include $(SDK_ROOT)/tools/app.mk

icon: $(APP_NAME)_ICON.bin