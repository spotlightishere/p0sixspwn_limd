CFILES = mobile_inject.c

mobile_inject: $(CFILES)
	$(CC) $(CFILES) -o mobile_inject -Wall `pkg-config libimobiledevice-1.0 --libs`

dmg:
	hdiutil create -fs "HFS+" -format UDZO -srcfolder Root -layout NONE -ov Resources/Root.dmg

clean:
	rm mobile_inject
