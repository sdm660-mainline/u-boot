#!/bin/bash
# Uncomment to get verbose output of all commands:
#set -x

# initramfs is not used by U-Boot, does not matter much
INITFS_FILE=initramfs-empty.gz
# We need to use u-boot without bundled dtb, so it can use
# dtb provided by primary BL. That will be the appended one
UBOOT=u-boot-nodtb.bin

# Can be anything, cmdline is not used by u-boot
KERNEL_CMDLINE="uboot2nd"

# We need to provide gzipped kernel image for stock
# bootloader to be able to understand it and find appended DTB
echo "Gzipping: ${UBOOT} > ${UBOOT}.gz"
rm -f ${UBOOT}.gz # just in case
gzip -c -9 ${UBOOT} > ${UBOOT}.gz
ls -l --color ${UBOOT}
ls -l --color ${UBOOT}.gz

# create "empty" initfs
echo 1 > 1
gzip -c -9 1 > ${INITFS_FILE}
rm -f 1

# remove old boot images
rm -f ./boot-uboot-*.img

# Runs mkbootimg to create boot.img for a given dtb path
# paramters:
#   $1 = path to .dtb file to process
function run_mkbootimg() {
	local DTB_PATH=$1
	local DTB_NAME=$( basename ${DTB_PATH} )
	# Convert "sdm660-xiaomi-clover.dtb" -> "sdm660-xiaomi-clover":
	local BOARD_NAME=$( basename ${DTB_NAME} ".dtb" )
	# Assuming all DTS are named as soc-vendor-codename, get soc-vendor pair
	#  as first 2 cuts
	local SOC=$(    echo ${DTB_NAME} | cut -d '-' -f 1 )
	local VENDOR=$( echo ${DTB_NAME} | cut -d '-' -f 2 )
	# "Platform" will be something like "sdm630-sony" or "sdm660-xiaomi"
	local PLATFORM="${SOC}-${VENDOR}"
	local MKBOOTIMG_PARAMS_PLATFORM_FILE="bootimg_params/${PLATFORM}.sh"
	local MKBOOTIMG_PARAMS_BOARD_FILE="bootimg_params/${BOARD_NAME}.sh"
	local OUT_BOOTIMG="boot-uboot-${BOARD_NAME}.img"

	echo " > Processing: ${DTB_PATH} (${BOARD_NAME}) -> ${OUT_BOOTIMG} ..."

	# Append dtb to gzipped "kernel"
	cat ${UBOOT}.gz ${DTB_PATH} > ${UBOOT}.gz-dtb

	# mkbootimg params. They are different per platform/vendor
	# Clear variables so we can check their existence later
	unset flash_offset_base
	unset flash_offset_kernel
	unset flash_offset_ramdisk
	unset flash_offset_second
	unset flash_offset_tags
	unset flash_pagesize
	unset extra_mkbootimg_args

	# Source mkbootimg params from common platform file like "sdm630-sony.sh"
	if [ -f "${MKBOOTIMG_PARAMS_PLATFORM_FILE}" ]; then
		. "${MKBOOTIMG_PARAMS_PLATFORM_FILE}"
	fi
	# In most cases soc-vendor combination is enough. But if desired, this
	#   can be further customized by additionally sourcing board-specific
	#   file like "sdm660-xiaomi-clover.sh"
	if [ -f "${MKBOOTIMG_PARAMS_BOARD_FILE}" ]; then
		. "${MKBOOTIMG_PARAMS_BOARD_FILE}"
	fi

	# check if mkbootimg parameters were sourced from somewhere at all
	if [ -z ${flash_offset_base} ]; then
		echo " >> Failed to find bootimg_params for ${BOARD_NAME}!"
	else
		mkbootimg \
			--kernel         ${UBOOT}.gz-dtb \
			--ramdisk        ${INITFS_FILE} \
			--cmdline        "${KERNEL_CMDLINE}" \
			--base           "${flash_offset_base}" \
			--second_offset  "${flash_offset_second}" \
			--kernel_offset  "${flash_offset_kernel}" \
			--ramdisk_offset "${flash_offset_ramdisk}" \
			--tags_offset    "${flash_offset_tags}" \
			--pagesize       "${flash_pagesize}" \
			${extra_mkbootimg_args} \
			-o ${OUT_BOOTIMG}
	fi

	# Cleanup
	rm -f ${UBOOT}.gz-dtb
}

# With downstream dir, it is simple. All we have there are our 630/660 DTBs
for dtb in arch/arm/dts/qcom/*.dtb; do
	run_mkbootimg "${dtb}"
done

# For mainline dir, everythng is built. So we must filter only sda660/sdm630/sdm636/sdm660
for dtb in dts/upstream/src/arm64/qcom/sda660-*.dtb; do
	run_mkbootimg "${dtb}"
done
for dtb in dts/upstream/src/arm64/qcom/sdm630-*.dtb; do
	run_mkbootimg "${dtb}"
done
for dtb in dts/upstream/src/arm64/qcom/sdm636-*.dtb; do
	run_mkbootimg "${dtb}"
done
for dtb in dts/upstream/src/arm64/qcom/sdm660-*.dtb; do
	run_mkbootimg "${dtb}"
done

echo "Resulting boot images:"
ls -l --color boot-uboot-*.img

echo "Done"
