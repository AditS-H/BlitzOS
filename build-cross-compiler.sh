#!/bin/bash
set -e

export TARGET=x86_64-elf
export PREFIX=/usr/local/cross
export PATH=$PREFIX/bin:$PATH

cd ~
mkdir -p cross-compiler
cd cross-compiler

echo '[1/8] Downloading Binutils...'
[ -f binutils-2.41.tar.gz ] || wget -q --show-progress https://ftp.gnu.org/gnu/binutils/binutils-2.41.tar.gz

echo '[2/8] Downloading GCC...'
[ -f gcc-13.2.0.tar.gz ] || wget -q --show-progress https://ftp.gnu.org/gnu/gcc/gcc-13.2.0/gcc-13.2.0.tar.gz

echo '[3/8] Extracting Binutils...'
[ -d binutils-2.41 ] || tar -xf binutils-2.41.tar.gz

echo '[4/8] Extracting GCC...'
[ -d gcc-13.2.0 ] || tar -xf gcc-13.2.0.tar.gz

echo '[5/8] Building Binutils (10-15 minutes)...'
mkdir -p build-binutils
cd build-binutils
../binutils-2.41/configure --target=$TARGET --prefix=$PREFIX --with-sysroot --disable-nls --disable-werror
make -j4
sudo make install
cd ..

echo '[6/8] Configuring GCC...'
mkdir -p build-gcc
cd build-gcc
../gcc-13.2.0/configure --target=$TARGET --prefix=$PREFIX --disable-nls --enable-languages=c,c++ --without-headers

echo '[7/8] Building GCC (30-40 minutes)...'
make -j4 all-gcc
make -j4 all-target-libgcc

echo '[8/8] Installing GCC...'
sudo make install-gcc
sudo make install-target-libgcc

grep -q '/usr/local/cross/bin' ~/.bashrc || echo 'export PATH=/usr/local/cross/bin:$PATH' >> ~/.bashrc

echo ''
echo '✅ Cross-compiler installed successfully!'
echo ''
export PATH=/usr/local/cross/bin:$PATH
x86_64-elf-gcc --version
