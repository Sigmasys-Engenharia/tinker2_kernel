# Tinker board 2 kernel buiding instructions for the AIW version 2:

cd <directory>
make clean && make distclean && make mrproper
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- aiw_defconfig
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- menuconfig
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- rk3399-tinker_board_2.img -j6
sudo make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- modules_install
sudo dd if=boot.img of=/dev/mmcblk2p4 # it may be necessary to identify and change the target partition
