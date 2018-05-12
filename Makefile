# Project = p0sixspwn
# ProductType = tool
# Install_Dir = /usr/bin

CFILES = mobile_inject.c

# include $(MAKEFILEPATH)/CoreOS/ReleaseControl/BSDCommon.make

mobile_inject: mobile_inject.c
	$(CC) -o mobile_inject mobile_inject.c -Wall -dead_strip -limobiledevice -lplist



# after_install:
# 	make -C Resources
# 	$(INSTALL_DIRECTORY) "$(DSTROOT)"/usr/local/share/p0sixspwn/
# 	$(INSTALL_FILE) Resources/Root.dmg "$(DSTROOT)"/usr/local/share/p0sixspwn/
# 	$(INSTALL_FILE) Resources/DeveloperDiskImage.dmg "$(DSTROOT)"/usr/local/share/p0sixspwn/
# 	$(INSTALL_FILE) Resources/DeveloperDiskImage.dmg.signature "$(DSTROOT)"/usr/local/share/p0sixspwn/
