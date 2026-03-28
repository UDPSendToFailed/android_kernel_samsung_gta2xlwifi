ifeq ($(CONFIG_BUILD_ARM64_DT_OVERLAY),y)

dtbo-$(CONFIG_ARCH_SDM632) += \
	sdm450-mtp-s3-overlay.dtbo

sdm450-mtp-s3-overlay.dtbo-base := \
	sdm632.dtb

endif

always		:= $(dtb-y)
subdir-y	:= $(dts-dirs)
clean-files	:= *.dtb
