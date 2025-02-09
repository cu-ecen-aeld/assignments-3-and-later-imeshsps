#!/bin/bash
# Script outline to install and build kernel.
# Author: Siddhant Jajoo.

set -e
set -u

OUTDIR=/tmp/aeld
KERNEL_REPO=git://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable.git
KERNEL_VERSION=v5.15.163
BUSYBOX_VERSION=1_33_1
FINDER_APP_DIR=$(realpath $(dirname $0))
ARCH=arm64
CROSS_COMPILE=aarch64-none-linux-gnu-

if [ $# -lt 1 ]
then
	echo "Using default directory ${OUTDIR} for output"
else
	OUTDIR=$1
	echo "Using passed directory ${OUTDIR} for output"
fi

mkdir -p ${OUTDIR}

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/linux-stable" ]; then
    #Clone only if the repository does not exist.
	echo "CLONING GIT LINUX STABLE VERSION ${KERNEL_VERSION} IN ${OUTDIR}"
	git clone ${KERNEL_REPO} --depth 1 --single-branch --branch ${KERNEL_VERSION}
fi
if [ ! -e ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ]; then
    cd linux-stable
    echo "Checking out version ${KERNEL_VERSION}"
    git checkout ${KERNEL_VERSION}

    # TODO: Add your kernel build steps here
    make -j16 ARCH=arm64 CROSS_COMPILE=aarch64-none-linux-gnu- mrproper
    make -j16 ARCH=arm64 CROSS_COMPILE=aarch64-none-linux-gnu- defconfig
    #make -j16 ARCH=arm64 CROSS_COMPILE=aarch64-none-linux-gnu- all
    make -j16 ARCH=arm64 CROSS_COMPILE=aarch64-none-linux-gnu- Image
    #make -j16 ARCH=arm64 CROSS_COMPILE=aarch64-none-linux-gnu- module
    #make -j16 ARCH=arm64 CROSS_COMPILE=aarch64-none-linux-gnu- dtbs

fi

echo "Adding the Image in outdir"
# TODO
cp ${OUTDIR}/linux-stable/arch/arm64/boot/Image ${OUTDIR}

echo "Creating the staging directory for the root filesystem"
cd "$OUTDIR"
if [ -d "${OUTDIR}/rootfs" ]
then
	echo "Deleting rootfs directory at ${OUTDIR}/rootfs and starting over"
    sudo rm  -rf ${OUTDIR}/rootfs
fi

# TODO: Create necessary base directories
mkdir rootfs
mkdir -p rootfs/bin rootfs/dev rootfs/etc rootfs/home rootfs/lib rootfs/lib64 rootfs/proc rootfs/sbin rootfs/sys rootfs/tmp rootfs/usr rootfs/var
mkdir -p rootfs/usr/bin rootfs/usr/lib rootfs/usr/sbin rootfs/var/log


cd "$OUTDIR"
if [ ! -d "${OUTDIR}/busybox" ]
then
git clone git://busybox.net/busybox.git
    cd busybox
    git checkout ${BUSYBOX_VERSION}
    # TODO:  Configure busybox

    make distclean
    make defconfig

else
    cd busybox
fi

# TODO: Make and install busybox
    make -j16 ARCH=arm64 CROSS_COMPILE=aarch64-none-linux-gnu-
    make CONFIG_PREFIX=${OUTDIR}/rootfs ARCH=arm64 CROSS_COMPILE=aarch64-none-linux-gnu- install

echo "Library dependencies"
${CROSS_COMPILE}readelf -a ${OUTDIR}/rootfs/bin/busybox | grep "program interpreter"
${CROSS_COMPILE}readelf -a ${OUTDIR}/rootfs/bin/busybox | grep "Shared library"

# Find the toolchain path dynamically
TOOLCHAIN_PATH=$(dirname $(dirname $(which ${CROSS_COMPILE}gcc)))

# Define toolchain library paths
TOOLCHAIN_LIB="${TOOLCHAIN_PATH}/aarch64-none-linux-gnu/libc/lib/"
TOOLCHAIN_LIB64="${TOOLCHAIN_PATH}/aarch64-none-linux-gnu/libc/lib64/"

# Extract program interpreter
program_interpreter=$(${CROSS_COMPILE}readelf -a ${OUTDIR}/rootfs/bin/busybox | grep "program interpreter" | awk -F': ' '{print $2}' | tr -d '[]')

# Extract shared libraries
shared_libraries=$(${CROSS_COMPILE}readelf -a ${OUTDIR}/rootfs/bin/busybox | grep "Shared library" | awk -F'[][]' '{print $2}')

# Ensure target directories exist
mkdir -p ${OUTDIR}/rootfs/lib ${OUTDIR}/rootfs/lib64

# Copy the program interpreter
if [ -f "${TOOLCHAIN_LIB64}${program_interpreter}" ]; then
    cp "${TOOLCHAIN_LIB64}${program_interpreter}" "${OUTDIR}/rootfs/lib/"
    echo "Copied program interpreter: ${program_interpreter}"
else
    echo "Error: Program interpreter ${program_interpreter} not found in ${TOOLCHAIN_LIB64}!"
fi

# Copy shared libraries from the toolchain
for lib in $shared_libraries; do
    if [ -f "${TOOLCHAIN_LIB}${lib}" ]; then
        cp "${TOOLCHAIN_LIB}${lib}" "${OUTDIR}/rootfs/lib/"
        echo "Copied: ${lib} to lib/"
    elif [ -f "${TOOLCHAIN_LIB64}${lib}" ]; then
        cp "${TOOLCHAIN_LIB64}${lib}" "${OUTDIR}/rootfs/lib64/"
        echo "Copied: ${lib} to lib64/"
    else
        echo "Error: Library ${lib} not found in toolchain!"
    fi
done

# TODO: Make device nodes
sudo mknod -m 666 ${OUTDIR}/rootfs/dev/null c 1 3
sudo mknod -m 666 ${OUTDIR}/rootfs/dev/ttyAMA0 c 5 1


# TODO: Clean and build the writer utility
cd ${FINDER_APP_DIR}
make clean
make ARCH=arm64 CROSS_COMPILE=aarch64-none-linux-gnu-


# TODO: Copy the finder related scripts and executables to the /home directory
# on the target rootfs

cp autorun-qemu.sh ${OUTDIR}/rootfs/home/
cp autorun-qemu.sh ${OUTDIR}/rootfs/home/
cp finder-test.sh ${OUTDIR}/rootfs/home/
cp -r conf/ ${OUTDIR}/rootfs/home/
cp writer ${OUTDIR}/rootfs/home/
cp finder.sh ${OUTDIR}/rootfs/home/

# TODO: Chown the root directory
sudo chown -R root:root ${OUTDIR}/rootfs

# TODO: Create initramfs.cpio.gz

cd ${OUTDIR}/rootfs
#find . | cpio -H newc -ov --owner root:root > ${OUTDIR}/initramfs.cpio
find . -print0 | cpio --null -H newc -ov --owner root:root | gzip > ${OUTDIR}/initramfs.cpio.gz
# cd ${OUTDIR}
# gzip -f initramfs.cpio
