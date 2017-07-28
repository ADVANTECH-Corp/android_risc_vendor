NOTES:
	* This driver is verified on Linux Kernel 3.11 and 4.2
	* Pleasee make sure the SD card of your laptop/PC supports the SDIO.
		** Not every SD card supports this
		** Known laptop model that supported SDIO :Dell Latitude E6430, E6420


How to build:
	1. cd DHD_REL_1_363_124
	2. make -C src/dhd/linux dhd-cdc-sdmmc-p2p-android-cfg80211-gpl
		* Do not care about the 'android' if you want to use it under Linux

How to bring up WIFI driver:
	* The dhd driver is shared between 43438 and 43455, here we use the 43455 as example
	1. cd DHD_REL_1_363_124
	2. cp src/dhd/linux/dhd-cdc-sdmmc-cfg80211-gpl-`uname -r`/dhd.ko firmware/43455c0-roml/      # Here the `uname -r` is your kernel vresion
	3. cd firmware/43455c0-roml/
	3  sudo insmod bcmdhd.ko firmware_path=sdio-ag-p2p-aoe-pktfilter-keepalive-pktctx-proptxstatus-ampduhostreorder-txbf-sr-swdiv-aibss-relmcast.bin \
		nvram_path=bcm943457wlsagb.txt iface_name=wlan
	4. sudo ifconfig -a		## Make sure the wlan0 is created
	5. sudo ifconfig wlan0 up

	** If you want to test the 43438, using the firmware/43438a1/ directory and the file under it.

How to verify WIFI basic function:
	* Using wl tool, need to build it first, but it can only be used to connect OPEN, No security AP
		1. make -C src/wl/exe/
		2. cd src/wl/exe
		3. sudo ./wl up
		4. sudo ./wl scan
		5. sudo ./wl scanresults
		6. sudo ./wl join "AP name"
		7. sudo ./wl status
		

	* Using the wpa_supplicant
