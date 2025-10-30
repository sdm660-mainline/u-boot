#!/bin/bash

#DEFCONFIG="qcom_defconfig qcom-sdm660-phone.config"
#DEVICES="bbry-luna-boe bbry-athena-boe"
#DTBS="qcom/sdm636-bbry-luna-boe qcom/sdm660-bbry-athena-boe"

source ./common.sh

uboot=u-boot-nodtb.bin
uboot_do_build
pr_info "gzip %s" "$uboot"
uboot_gzip "$uboot" uboot
i=1
    for DEVICE in ${DEVICES}; do
    uboot=u-boot-nodtb.bin.gz
    dtb=`echo ${DTBS} | cut -d ' ' -f $i`
    output="binaries/u-boot-$DEVICE.img"
    pr_info "appending dtb to %s" "$uboot"
    uboot_append_dtb "$uboot" uboot
    pr_info "make boot image %s -> %s" "$uboot" "$output"
    build_android_boot_img "$uboot" "$output"
    pr_info "Successfully built U-Boot for %s\n" "$DEVICE"
    pr_info " - %s\n" "$output"
    ((i++))
done;
