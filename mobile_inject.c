#include <assert.h>
#include <fcntl.h>
#include <libimobiledevice/afc.h>
#include <libimobiledevice/libimobiledevice.h>
#include <libimobiledevice/lockdown.h>
#include <libimobiledevice/property_list_service.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

static char *real_dmg, *real_dmg_signature, *root_dmg;
int timesl;

// Taken from
// https://github.com/libimobiledevice/libimobiledevice/blob/b3d35fbcf7a1ac669c2e80fbd58920941a5d4c0c/tools/ideviceimagemounter.c#L146-L153
static void print_xml(plist_t node) {
  char *xml = NULL;
  uint32_t len = 0;
  plist_to_xml(node, &xml, &len);
  if (xml)
    puts(xml);
}

void qwrite(afc_client_t afc, const char *from, const char *to) {
  printf("Sending %s -> %s... ", from, to);
  uint64_t ref;

  int fd = open(from, O_RDONLY);
  assert(fd != -1);
  size_t size = (size_t)lseek(fd, 0, SEEK_END);
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
    lockdownd_error_t err =
        lockdownd_client_new_with_handshake(dev, &lockdown_client, "CopyIt");
    if (err != LOCKDOWN_E_SUCCESS) {
      printf("Failed to talk to lockdown: %d\n", err);
      exit(2);
    }

  Retry : {}
    printf("Starting AFC service...\n");
    lockdownd_service_descriptor_t afc_descriptor = 0;
    assert(!lockdownd_start_service(lockdown_client, "com.apple.afc",
                                    &afc_descriptor));
    assert(afc_descriptor);

    afc_client_t afc_client;
    assert(!afc_client_new(dev, afc_descriptor, &afc_client));

    printf("Sending DMGs...\n");
    // Now we create the directory to mount our DMGs.
    assert(!afc_make_directory(afc_client, "PublicStaging"));
    afc_remove_path(afc_client, "PublicStaging/staging.dimage");
    afc_remove_path(afc_client, "PublicStaging/root.dimage");
    qwrite(afc_client, real_dmg, "PublicStaging/staging.dimage");
    qwrite(afc_client, root_dmg, "PublicStaging/root.dimage");

    // Ask to start up the image mounting daemon.
    printf("Asking to mount DMGs...\n");

    // Shortly here we'll be sending plists.
    property_list_service_client_t mim_client = 0;
    lockdownd_service_descriptor_t mim_descriptor = 0;
    assert(!lockdownd_start_service(lockdown_client,
                                    "com.apple.mobile.mobile_image_mounter",
                                    &mim_descriptor));
    assert(!property_list_service_client_new(dev, mim_descriptor, &mim_client));

    // Get real DMG signature
    int fd = open(real_dmg_signature, O_RDONLY);
    assert(fd != -1);
    uint8_t sig[128];
    assert(read(fd, sig, sizeof(sig)) == sizeof(sig));
    close(fd);

    // Formulate mount request
    plist_t mount_request_dict = plist_new_dict();
    plist_dict_set_item(mount_request_dict, "Command",
                        plist_new_string("MountImage"));
    plist_dict_set_item(
        mount_request_dict, "ImagePath",
        plist_new_string("/var/mobile/Media/PublicStaging/staging.dimage"));
    plist_dict_set_item(mount_request_dict, "ImageType",
                        plist_new_string("Developer"));
    plist_dict_set_item(mount_request_dict, "ImageSignature",
                        plist_new_data((const char *)sig, sizeof(sig)));
    // If you want to debug what's being sent, check this out.
    // print_xml(mount_request_dict);

    property_list_service_error_t plist_send_err =
        property_list_service_send_xml_plist(mim_client, mount_request_dict);
    if (plist_send_err != PROPERTY_LIST_SERVICE_E_SUCCESS) {
      printf("Failed sending mount request: %d\n", plist_send_err);
      return;
    }
    plist_free(mount_request_dict);

    printf("Waiting %dms for lockdownd...\n", timesl);
    usleep(timesl);
    printf("Switching DMG signatures...\n");
    assert(!afc_rename_path(afc_client, "PublicStaging/root.dimage",
                            "PublicStaging/staging.dimage"));

    printf("Reading response from lockdownd...\n");
    plist_t mount_result_dict = 0;
    property_list_service_error_t plist_recv_err =
        property_list_service_receive_plist(mim_client, &mount_result_dict);

    if (plist_recv_err != PROPERTY_LIST_SERVICE_E_SUCCESS) {
      printf("Failed reading mount request response: %d\n", plist_recv_err);
      return;
    }

    char *status = NULL;
    if (mount_result_dict) {
      plist_t node = plist_dict_get_item(mount_result_dict, "Status");
      if (node) {
        plist_get_string_val(node, &status);
        if (!status) {
          printf("Error: Seems like the status given wasn't a "
                 "string:\n");
          print_xml(mount_result_dict);
          exit(2);
        }
      } else {
        plist_t node = plist_dict_get_item(mount_result_dict, "Error");
        if (node) {
          char *error_returned = NULL;
          plist_get_string_val(node, &error_returned);
          if (!error_returned || !strcmp(error_returned, "ImageMountFailed")) {
            printf("Error: We somehow've already mounted our "
                   "image. Reboot your device and run again.\n");
            exit(3);
          }
        }
        printf("Error: Doesn't seem there was any status given.. check "
               "for an error. Returned:\n");
        print_xml(mount_result_dict);
        status = "";
      }
    } else {
      printf("Error: Doesn't seem we got any response whatsoever...\n");
      return;
    }

    // At this point, we know it was mounted succesfully.
    if (!strcmp(status, "Complete")) {
      lockdownd_service_descriptor_t helper_socket = 0;
      sleep(2);
      printf("Image mounted, running helper...\n");
      err = lockdownd_start_service(lockdown_client, "CopyIt", &helper_socket);
      if (err != LOCKDOWN_E_SUCCESS && err != LOCKDOWN_E_PLIST_ERROR) {
        printf("Failed to start helper service: %d\n", err);
        exit(4);
      }

      // We're all set!
      exit(0);
    } else {
      printf("Failed to inject image, trying again... (if it fails, try a "
             "different time). Delay: %dus\n",
             timesl);
      timesl += 1000;
      goto Retry;
    }
  }
}

int main(int argc, char **argv) {
  if (argc != 4) {
    fprintf(stderr,
            "Usage: %s DeveloperDiskImage.dmg DeveloperDiskImage.dmg.signature "
            "Root.dmg\n",
            argv[0]);
    return 1;
  }

  timesl = 209999;

  real_dmg = argv[1];
  real_dmg_signature = argv[2];
  root_dmg = argv[3];

  // Loop while performing
  assert(idevice_event_subscribe(cb, NULL) == IDEVICE_E_SUCCESS);
  while (1) {
    // Loop as we wait for our callback to be called.
  }
  return 0;
}
