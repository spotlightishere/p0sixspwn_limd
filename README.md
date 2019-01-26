# p0sixspwn_limd
`p0sixspwn_limd` is the p0sixspwn jailbreak but utilizing the libimobiledevice library compared to MobileDevice. This allows for many, many more platforms to be supported, and should an API change, no hacky fixes to apply.

## Prerequisites
In order to build, one should simply run `make`.
Make sure that your device is on and unlocked, and in the case of multiple devices, only your iOS 6.1.x device is connected.
If you just restored your device, it _may_ require an additional reboot!

Beyond this point, you can use the following syntax to run:

```
sudo ./mobile_inject Resources/DeveloperDiskImage.dmg Resources/DeveloperDiskImage.dmg.signature Resources/Root.dmg
```
