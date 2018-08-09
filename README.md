# bt-tether

## Description

*bt-tether* is a small script that enables Bluetooth Tethering on your Jolla Phone. I could not find any software that did that realiably so I tried my hand at it.

It is **quite experimental** and a little hacky. It’s only been tested on my Jolla Phone in developer mode. **I’m distributing it here without any warranty**, however I’d be glad to receive feedback, bug reports and pull requests; I’ll do my best to improve it.

*bt-tether* can connect several devices at once to whatever is the current connection of the phone (Mobile or WLAN). It uses the `pand` daemon that should be installed already, along with specially crafted scripts and an adhoc DNS relay.

## Licence

This software is distributed under the terms of the MIT Lincence. See the included `LICENCE` file.

## Building RPM package

Run the `rpm/prep` script with a version (x.y) and a release number as arguments, it should create a `.tgz` and a `.spec` file that can be used by `rpmbuild`. You can use the following command in the `mersdk` VM of the SailfishOS dev environment:

```
sb2 -t SailfishOS-2.2.0.29-armv7hl rpmbuild -v -bb SPECS/bt-tether-0.1-1.spec
```

(you may have to change some version numbers and path)

## Notes

* The `pand` util doesn’t seem to be packaged with `bluez5`, so for now **this package most likely only works with `bluez` version 4**.
* There is no GUI, everything should work right away!
* My tablet, using PAN Bluetooth Tethering to my phone, which is connected to the Mobile network, achieves a bandwidth of **1.5Mbps on average** (for download and upload, not at the same time). This is usually what I get when transfering files from my phone using Bluetooth.
* After some casual testing, it appears that **battery consumption seems lower than WLAN Tethering**, which is good! In my test, the phone’s battery went down 30% during 24h (with very little direct use) with both Mobile Data and Bluetooth activated, while my tablet was constantly connected to the Bluetooth Tethering and running Telegram.
* If you had a paired device before you installed *bt-tether*, **you have to re-pair your device for it to see that your phone now has PAN capability**.
* While *bt-tether* should work right away after being installed, you may have to reboot the phone if it doesn’t.
* Since the package has to modify a configuration file that it doesn’t own during installation, an **update of SailfishOS may break bt-tether**. If this happens, try to reinstall the package.

## How does it work?

*This section is based on my partial understanding of bluez, it may be inaccurate!*

The package contains a `systemd` service configuration that will launch `pand` at boot. A post-install script will also disable the native `network` plugin of the bluetooth stack that doesn’t work by modifying `/etc/bluetooth/main.conf`. (A pre-uninstall script reverts this modification).

Once `pand` runs, all devices pairing with the phone will see the PAN capability.

When a paired device wants to use the PAN capability, pand will spawn an interface on the phone and launch a up-script. This script will configure the interface with a sensible IP, add a `MASQUERADE` target with `iptables`, launch a tiny DHCP server (`udhcpd`) and the `dns-relay` utility that I coded for the specific purpose of this package. A down-script reverts everything when the device disconnects.

All this happens for each device requesting to use the PAN capability. Fortunately, `udhcpd` and `dns-relay` both have extremely tiny CPU and memory footprints. At some point I’ll try to use only one instance of `udhcpd` and `dns-relay`.


