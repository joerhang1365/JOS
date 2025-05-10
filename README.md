# JOS

# Install QEMU environemnt on Ubuntu
```
sudo apt update && sudo apt upgrade -y && \
sudo apt install -y autoconf automake autotools-dev curl \
python3 python3-pip libmpc-dev libmpfr-dev libgmp-dev \
gawk build-essential bison flex texinfo gperf libtool \
patchutils bc zlib1g-dev libexpat-dev ninja-build \
git cmake libglib2.0-dev libslirp-dev libpixman-1-dev libgtk-3-dev
```

## Create a directory for RISC-V toolchain:
```
mkdir ~/riscv && cd ~/riscv
git clone --branch 2024.12.16 https://github.com/riscv/riscv-gnu-toolchain
cd riscv-gnu-toolchain
```

## Compile the toolchain
```
cd ~/ece391/riscv-gnu-toolchain
sudo mkdir -p /opt/toolchains/riscv
sudo chown -R $(logname):$(logname) /opt/toolchains/riscv
```

## Clone qemu and apply patch
```
git clone --depth 1 --branch v9.0.2 https://github.com/qemu/qemu
cd qemu
patch -p0 < ../qemu.patch
```

### Configure and make
```
./configure --prefix=/opt/toolchains/riscv \

--target-list=riscv32-softmmu,riscv64-softmmu \
--enable-gtk --enable-system --disable-werror \
--enable-debug --enable-debug-info

make -j $(nproc)
make install
```

## Optional
```
cd ..
./configure --prefix=/opt/toolchains/riscv/ --enable-multilib
make -j $(nproc)
```

## Add toolchain and QEMU to PATH
```
echo "PATH=/opt/toolchains/riscv/bin/:$PATH" >> ~/.bashrc
. ~/.bashrc
```
To verify success run `qemu-system-riscv64`, which should cause a QEMU
window to pop up.
