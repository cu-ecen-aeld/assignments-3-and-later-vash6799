#!/bin/bash
# Script outline to install and build kernel.
# Author: Siddhant Jajoo.
# Modified for Assignment 3 Part 2

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

# Create OUTDIR if it doesn't exist; fail if it can't be created
if ! mkdir -p "${OUTDIR}"; then
    echo "Error: Could not create directory ${OUTDIR}"
    exit 1
fi

OUTDIR=$(realpath "${OUTDIR}")

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/linux-stable" ]; then
    #Clone only if the repository does not exist.
    echo "CLONING GIT LINUX STABLE VERSION ${KERNEL_VERSION} IN ${OUTDIR}"
    git clone ${KERNEL_REPO} --depth 1 --single-branch --branch ${KERNEL_VERSION} linux-stable
fi

# Check if the kernel image exists. If not, build it.
if [ ! -e ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ]; then
    cd linux-stable
    echo "Checking out version ${KERNEL_VERSION}"
    git checkout ${KERNEL_VERSION}

    # 1. Clean the build
    # Only mrproper if we don't have a config, to save time on re-runs
    if [ ! -e .config ]; then
        echo "Cleaning and configuring kernel..."
        make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} mrproper
        make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} defconfig
    fi

    # 2. Build the kernel image
    echo "Building kernel (this may take a while)..."
    make -j$(nproc) ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} all
    # 3. Build device tree
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} dtbs
fi

echo "Adding the Image in outdir"
# Ensure we copy the Image even if we didn't rebuild it this run
cp "${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image" "${OUTDIR}/"

echo "Creating the staging directory for the root filesystem"
cd "$OUTDIR"
if [ -d "${OUTDIR}/rootfs" ]
then
    echo "Deleting rootfs directory at ${OUTDIR}/rootfs and starting over"
    sudo rm  -rf ${OUTDIR}/rootfs
fi

# Create base directories
mkdir -p "${OUTDIR}/rootfs"
cd "${OUTDIR}/rootfs"
mkdir -p bin dev etc home lib lib64 proc sbin sys tmp usr var
mkdir -p usr/bin usr/lib usr/sbin
mkdir -p var/log

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/busybox" ]
then
    git clone git://busybox.net/busybox.git
    cd busybox
    git checkout ${BUSYBOX_VERSION}
    make distclean
    make defconfig
else
    cd busybox
    # FIX: Ensure config exists even if repo was already cloned
    if [ ! -f .config ]; then
        make defconfig
    fi
fi

# Make and install busybox
echo "Building Busybox..."
make -j$(nproc) ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE}
make CONFIG_PREFIX="${OUTDIR}/rootfs" ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} install

echo "Library dependencies"
cd "${OUTDIR}/rootfs"
${CROSS_COMPILE}readelf -a bin/busybox | grep "program interpreter"
${CROSS_COMPILE}readelf -a bin/busybox | grep "Shared library"

# Add library dependencies to rootfs from the toolchain sysroot
SYSROOT=$(${CROSS_COMPILE}gcc -print-sysroot)

# 1. Copy the loader (Program Interpreter)
cp "${SYSROOT}/lib/ld-linux-aarch64.so.1" lib/
# 2. Copy shared libraries
cp "${SYSROOT}/lib64/libm.so.6" lib64/
cp "${SYSROOT}/lib64/libresolv.so.2" lib64/
cp "${SYSROOT}/lib64/libc.so.6" lib64/

# Make device nodes
sudo mknod -m 666 dev/null c 1 3
sudo mknod -m 600 dev/console c 5 1

# Clean and build the writer utility
cd "${FINDER_APP_DIR}"
make clean
make CROSS_COMPILE=${CROSS_COMPILE}

# Copy the finder related scripts and executables to /home
cp writer finder.sh finder-test.sh autorun-qemu.sh "${OUTDIR}/rootfs/home/"
mkdir -p "${OUTDIR}/rootfs/home/conf"
cp conf/username.txt conf/assignment.txt "${OUTDIR}/rootfs/home/conf/"

# Modify finder-test.sh to reference the local conf directory
sed -i 's|\.\./conf/assignment.txt|conf/assignment.txt|g' "${OUTDIR}/rootfs/home/finder-test.sh"

# FIX: Remove Windows line endings (\r) from scripts to prevent "not found" errors
sed -i 's/\r$//' "${OUTDIR}/rootfs/home/finder.sh"
sed -i 's/\r$//' "${OUTDIR}/rootfs/home/finder-test.sh"
sed -i 's/\r$//' "${OUTDIR}/rootfs/home/autorun-qemu.sh"

# Chown the root directory
cd "${OUTDIR}/rootfs"
sudo chown -R root:root *

# Create initramfs.cpio.gz
find . | cpio -H newc -ov --owner root:root > "${OUTDIR}/initramfs.cpio"
cd "${OUTDIR}"
gzip -f initramfs.cpio
