# JOS

# Install QEMU environemnt on Ubuntu

sudo apt update && sudo apt upgrade -y && \
sudo apt install -y autoconf automake autotools-dev curl \
python3 python3-pip libmpc-dev libmpfr-dev libgmp-dev \
gawk build-essential bison flex texinfo gperf libtool \
patchutils bc zlib1g-dev libexpat-dev ninja-build \
git cmake libglib2.0-dev libslirp-dev libpixman-1-dev libgtk-3-dev

# Create a directory for your class work to set up the RISC-V toolchain:

mkdir ~/ece391 && cd ~/ece391
git clone --branch 2024.12.16 https://github.com/riscv/riscv-gnu-toolchain
cd riscv-gnu-toolchain

# Compile the toolchain

cd ~/ece391/riscv-gnu-toolchain, if not already there
sudo mkdir -p /opt/toolchains/riscv
sudo chown -R $(logname):$(logname) /opt/toolchains/riscv

# Clone qemu, apply patch
git clone --depth 1 --branch v9.0.2 https://github.com/qemu/qemu
cd qemu
patch -p0 < ../qemu.patch

# configure and make
./configure --prefix=/opt/toolchains/riscv \

--target-list=riscv32-softmmu,riscv64-softmmu \
--enable-gtk --enable-system --disable-werror \
--enable-debug --enable-debug-info

# the argument after -j defines the number of jobs to run
# this improves build performance on a multicore system
# you can replace $(nproc) with a number of your choice
make -j $(nproc)
make install

4

# optional -- enable the TUI for qemu
# You can recompile with these later if you want it
# You will need to install a curses library or something to support the TUI
# This is not an "expected/supported" part of the setup
cd gdb
./configure --enable-tui
make
cd ..
# optional
# go out into toolchain dir, conf+make (can take a very long time)
cd ..
./configure --prefix=/opt/toolchains/riscv/ --enable-multilib
make -j $(nproc)
7. You should now have the toolchain and QEMU, but we still need to add them to PATH:
echo "PATH=/opt/toolchains/riscv/bin/:$PATH" >> ~/.bashrc
. ~/.bashrc
To verify successful installation, run qemu-system-riscv64, which should cause a QEMU
window to pop up.
