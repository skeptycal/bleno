#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/prctl.h>
#include <unistd.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>

#define HCI_DEVICE_ID 0

int lastSignal = 0;

static void signalHandler(int signal) {
  lastSignal = signal;
}

int hci_le_set_advertising_data(int dd, uint8_t* data, uint8_t length, int to)
{
  struct hci_request rq;
  le_set_advertising_data_cp data_cp;
  uint8_t status;

  memset(&data_cp, 0, sizeof(data_cp));
  data_cp.length = length;
  memcpy(&data_cp.data, data, sizeof(data_cp.data));

  memset(&rq, 0, sizeof(rq));
  rq.ogf = OGF_LE_CTL;
  rq.ocf = OCF_LE_SET_ADVERTISING_DATA;
  rq.cparam = &data_cp;
  rq.clen = LE_SET_ADVERTISING_DATA_CP_SIZE;
  rq.rparam = &status;
  rq.rlen = 1;

  if (hci_send_req(dd, &rq, to) < 0)
    return -1;

  if (status) {
    errno = EIO;
    return -1;
  }

  return 0;
}

int main(int argc, const char* argv[])
{
  int hciSocket;
  struct hci_dev_info hciDevInfo;

  int previousAdapterState = -1;
  int currentAdapterState;
  const char* adapterState = NULL;
  
  fd_set rfds;
  struct timeval tv;
  int selectRetval;

  char stdinBuf[256 * 2 + 1];
  char eirBuf[256];
  int len;
  int i;

  memset(&hciDevInfo, 0x00, sizeof(hciDevInfo));

  // setup signal handlers
  signal(SIGINT, signalHandler);
  signal(SIGKILL, signalHandler);
  signal(SIGHUP, signalHandler);

  prctl(PR_SET_PDEATHSIG, SIGINT);

  // setup HCI socket
  hciSocket = hci_open_dev(HCI_DEVICE_ID);
  hciDevInfo.dev_id = HCI_DEVICE_ID;

  while(1) {
    FD_ZERO(&rfds);
    FD_SET(0, &rfds);
    FD_SET(hciSocket, &rfds);

    tv.tv_sec = 1;
    tv.tv_usec = 0;

    // get HCI dev info for adapter state
    ioctl(hciSocket, HCIGETDEVINFO, (void *)&hciDevInfo);
    currentAdapterState = hci_test_bit(HCI_UP, &hciDevInfo.flags);

    if (previousAdapterState != currentAdapterState) {
      previousAdapterState = currentAdapterState;

      if (!currentAdapterState) {
        adapterState = "poweredOff";
      } else {
        hci_le_set_advertise_enable(hciSocket, 1, 1000);
        if (hci_le_set_advertise_enable(hciSocket, 0, 1000) == -1) {
          if (EPERM == errno) {
            adapterState = "unauthorized";
          } else if (EIO == errno) {
            adapterState = "unsupported";
          } else {
            printf("%d\n", errno);
            adapterState = "unknown";
          }
        } else {
          adapterState = "poweredOn";
        }
      }

      printf("adapterState %s\n", adapterState);
    }

    selectRetval = select(hciSocket + 1, &rfds, NULL, NULL, &tv);

    if (-1 == selectRetval) {
      if (SIGINT == lastSignal || SIGKILL == lastSignal) {
        // done
        break;
      } else if (SIGHUP == lastSignal) {
        // stop advertising
        hci_le_set_advertise_enable(hciSocket, 0, 1000);
      } 
    } else if (selectRetval) {
      if (FD_ISSET(0, &rfds)) {
        len = read(0, stdinBuf, sizeof(stdinBuf));

        i = 0;
        while(stdinBuf[i] != '\n' && i < len) {
          sscanf(&stdinBuf[i], "%02x", (unsigned int*)&eirBuf[i / 2]);

          i += 2;
        }

        // stop advertising
        hci_le_set_advertise_enable(hciSocket, 0, 1000);

        // set advertisement data
        hci_le_set_advertising_data(hciSocket, (uint8_t*)&eirBuf, i / 2, 1000);

        // start advertising
        hci_le_set_advertise_enable(hciSocket, 1, 1000);
      }
    }
  }

  // stop advertising
  hci_le_set_advertise_enable(hciSocket, 0, 1000);

  close(hciSocket);

  return 0;
}