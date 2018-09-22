CFILES = mobile_inject.c

mobile_inject: mobile_inject.c
	$(CC) -o mobile_inject mobile_inject.c -Wall -limobiledevice -lplist

clean:
	rm mobile_inject
