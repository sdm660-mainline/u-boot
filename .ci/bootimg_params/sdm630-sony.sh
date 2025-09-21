flash_offset_base="0x00000000"
flash_offset_kernel="0x00008000"
flash_offset_ramdisk="0x01000000"
flash_offset_second="0x00f00000"
# tags offset is different for sony platform
flash_offset_tags="0x01e00000"
flash_pagesize="4096"

# just in case
extra_mkbootimg_args="--os_version 15.0.0 --os_patch_level 2025-04"
