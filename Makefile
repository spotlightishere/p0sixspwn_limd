CFILES = mobile_inject.c

mobile_inject: $(CFILES)
	$(CC) $(CFILES) -o mobile_inject -Wall `pkg-config libimobiledevice-1.0 --libs`

clean:
	rm mobile_inject
