# shellcheck shell=ash
# shellcheck disable=SC3047
# shellcheck disable=SC3046
# SPDX-License-Identifier: GPL-2.0-or-later
# Common build helpers for device build scripts
# Source this file!

handle_error() {
	echo "ERROR in line $1" >&2
	exit 1
}

trap 'handle_error $LINENO' ERR

# Colors :D
# shellcheck disable=SC2034
RED="\e[31m"
# shellcheck disable=SC2034
GREEN="\e[32m"
# shellcheck disable=SC2034
YELLOW="\e[33m"
# shellcheck disable=SC2034
BLUE="\e[34m"
# shellcheck disable=SC2034
BOLDBLUE="\e[1;34m"
# shellcheck disable=SC2034
RESET="\e[0m"

pr_debug() {
	local fmt
	fmt="$1"
	shift
	# shellcheck disable=SC2059
	printf "${YELLOW}$fmt${RESET}\n" "$@" >&2
}

pr_info() {
	local fmt
	fmt="$1"
	shift
	# shellcheck disable=SC2059
	printf "${BLUE}$fmt${RESET}\n" "$@" >&2
}

pr_error() {
	local fmt
	fmt="$1"
	shift
	# shellcheck disable=SC2059
	printf "${RED}$fmt${RESET}\n" "$@" >&2
}

logeval() {
	printf "${YELLOW}$ %s\n${RESET}" "$*"
	eval "$*"
}

# Ensure error handling is enabled
set -e
# Nicer formatting for -x
PS4="$ "

# ==== Variable setup ====
export UBOOT_SOURCE
export UBOOT_OUTDIR
export BINARIES_DIR
export DEFCONFIG
export UBOOT_CROSS_COMPILE
export EMPTY_RAMDISK

# Set UBOOT_SOURCE to either CI_PROJECT_DIR or the first argument to
# the script that sourced this file
if [ -n "$CI_PROJECT_DIR" ]; then
	UBOOT_SOURCE="$CI_PROJECT_DIR"
elif [ -n "$1" ]; then
	UBOOT_SOURCE="$1"
else
    UBOOT_SOURCE=`pwd`
fi

assert_ci_variable() {
	local var
	# Set var to the value of the variable named in $1
	eval "var=\$$1"
	if [ -z "$var" ]; then
		pr_error "ERROR: CI variable %s not set!" "$1"
		return 1
	fi
}

assert_ci_variable DEFCONFIG

# U-Boot build directory
UBOOT_OUTDIR="$UBOOT_SOURCE/.output"
BINARIES_DIR="$UBOOT_SOURCE/binaries"

# Ensure the directories exist
mkdir -p "$UBOOT_OUTDIR"
mkdir -p "$BINARIES_DIR"

# We don't need to cross compile if building on the aarch64 runner
UBOOT_CROSS_COMPILE=""
if [ "$(uname -m)" != "aarch64" ]; then
	if [ -x "$(command -v aarch64-linux-gnu-gcc)" ]; then
		UBOOT_CROSS_COMPILE=aarch64-linux-gnu-
	else
		UBOOT_CROSS_COMPILE=aarch64-alpine-linux-musl-
	fi
fi

EMPTY_RAMDISK="$UBOOT_OUTDIR/empty.gz"

# ==== End variable setup ====

# Make an empty gzip archive to use as the ramdisk
printf "\0" | gzip --stdout > "$EMPTY_RAMDISK"

# Make wrapper for U-Boot
uboot_make() {
	set -x
	make "O=$UBOOT_OUTDIR" "CROSS_COMPILE=$UBOOT_CROSS_COMPILE" -j"$(nproc --all)" "$@"
	set +x
}

uboot_configure() {
	# shellcheck disable=SC2086
	uboot_make $DEFCONFIG
}

# Create a .config and then build U-Boot
uboot_do_build() {
	local dtb
	dtb="$1"

	uboot_configure
	uboot_make
	pr_info "Finished building U-Boot!"
}

# $1: uboot binary to gzip
# $2: variable to store gzipped binary name
uboot_gzip() {
	local _uboot varname
	_uboot="$1"
	varname="$2"
	gzip -fk "$UBOOT_OUTDIR/$_uboot"
	# pr_debug "varname: %s" "$varname"
	logeval "$varname=\"${_uboot}.gz\""
}

# $1: uboot binary to gzip
# $2: variable to store gzipped binary name
uboot_append_dtb() {
	local _uboot dtb varname out
	_uboot="$1"
	varname="$2"
	out="${uboot}-dtb"

	cat "$UBOOT_OUTDIR/$uboot" "$UBOOT_OUTDIR/u-boot.dtb" > "$UBOOT_OUTDIR/$out"

	logeval "$varname=\$out"
}

build_android_boot_img_cmd() {
	local uboot output _cmd varname
	uboot="$1"
	output="$2"
	varname="$3"

	_cmd="mkbootimg"
	_cmd="$_cmd --base 0x0"
	_cmd="$_cmd --kernel_offset 0x8000"
	_cmd="$_cmd --pagesize 4096"
	_cmd="$_cmd --os_patch_level 2028-09-21"
	_cmd="$_cmd --ramdisk $EMPTY_RAMDISK"
	_cmd="$_cmd --kernel $UBOOT_OUTDIR/$uboot -o $output"

	eval "$varname=\"$_cmd\""
}

build_android_boot_img() {
	local uboot output cmd
	uboot="$1"
	output="$2"
	# Get the command to run and run it!
	build_android_boot_img_cmd "$uboot" "$output" cmd
	logeval "$cmd"
}

build_android_boot_img_v2() {
	local uboot dtb output cmd
	uboot="$1"
	dtb="$2"
	output="$3"

	build_android_boot_img_cmd "$uboot" "$output" cmd
	cmd="$cmd --header_version 2 --dtb_offset 0x01f00000 --dtb $UBOOT_OUTDIR/dts/upstream/src/arm64/${dtb}.dtb"

	logeval "$cmd"
}

build_uboot_android_v1() {
	local uboot dtb output
	dtb="$1"
	uboot="u-boot-nodtb.bin"
	output="$BINARIES_DIR/u-boot-$DEVICE.img"

	uboot_do_build "$dtb"
	pr_info "gzip %s" "$uboot"
	uboot_gzip "$uboot" uboot
	pr_info "appending dtb to %s" "$uboot"
	uboot_append_dtb "$uboot" uboot
	pr_info "make boot image %s -> %s" "$uboot" "$output"
	build_android_boot_img "$uboot" "$output"
	pr_info "Successfully built U-Boot for %s\n" "$DEVICE"
	pr_info " - %s\n" "$output"
}

build_uboot_android_v2() {
	local uboot dtb output
	dtb="$1"
	uboot="u-boot-nodtb.bin"
	output="$BINARIES_DIR/u-boot-$DEVICE.img"

	uboot_do_build "$dtb"
	pr_info "gzip %s" "$uboot"
	uboot_gzip "$uboot" uboot
	pr_info "make boot image %s -> %s" "$uboot" "$output"
	build_android_boot_img_v2 "$uboot" "$dtb" "$output"
	pr_info "Built U-Boot for %s - %s" "$DEVICE" "$output"
}

build_uboot_android() {
	local dtb
	dtb="$1"

	if [ "$ANDROID_BOOTIMG_HEADER_VERSION" = "2" ]; then
		build_uboot_android_v2 "$dtb"
	elif [ "$ANDROID_BOOTIMG_HEADER_VERSION" = "1" ] || [ "$ANDROID_BOOTIMG_HEADER_VERSION" = "0" ] || [ -z "$ANDROID_BOOTIMG_HEADER_VERSION" ]; then
		build_uboot_android_v1 "$dtb"
	else
		pr_error "Unsupported header version $ANDROID_BOOTIMG_HEADER_VERSION"
		exit 1
	fi
}

uboot_print_version() {
	cat <<-EOF > "$UBOOT_OUTDIR/getver.c"
	#include <stdio.h>
	#include "version_autogenerated.h"

	int main(void) {
		printf("%d%d", U_BOOT_VERSION_NUM, U_BOOT_VERSION_NUM_PATCH);
		return 0;
	}
	EOF

	cc -I"$UBOOT_OUTDIR/include/generated" -o "$UBOOT_OUTDIR/getver" "$UBOOT_OUTDIR/getver.c"
	"$UBOOT_OUTDIR/getver"
}


# cd into the U-Boot source directory to run out commands
pr_info "Moving to $UBOOT_SOURCE"
cd "$UBOOT_SOURCE"
