// Bluetooth link: Nordic UART service on the ST BLE TinyShield. Pair A owns this file.
// Based on UART.ino from the STBLE library's UARTPassThrough example by Ben Rose, TinyCircuits
// (https://github.com/TinyCircuits/TinyCircuits-STBLE-Library).
// Lines travel in packets of at most 20 bytes; the newline marks the end of a line.

char BLE_NAME[21];
const uint8_t BLE_PACKET = 20;

bool blePresent = true;  // false when the shield did not answer at startup; USB keeps working
bool bleConnected = false;
uint16_t bleConnHandle;
bool bleAdvertise = true;  // start advertising on the next bleLoop()
uint16_t UARTServHandle, UARTTXCharHandle, UARTRXCharHandle;

// Received bytes wait here until loop() turns them into lines, so handlers never run inside
// HCI_Process() (sendLine() calls HCI_Process() itself while the radio is busy).
char bleRx[256];
uint8_t bleRxHead = 0, bleRxTail = 0;  // uint8_t wraps at 256, the buffer size

#define COPY_UUID_128(uuid_struct, uuid_15, uuid_14, uuid_13, uuid_12, uuid_11, uuid_10, uuid_9, uuid_8, uuid_7, uuid_6, uuid_5, uuid_4, uuid_3, uuid_2, uuid_1, uuid_0) \
  do {\
    uuid_struct[0] = uuid_0; uuid_struct[1] = uuid_1; uuid_struct[2] = uuid_2; uuid_struct[3] = uuid_3; \
    uuid_struct[4] = uuid_4; uuid_struct[5] = uuid_5; uuid_struct[6] = uuid_6; uuid_struct[7] = uuid_7; \
    uuid_struct[8] = uuid_8; uuid_struct[9] = uuid_9; uuid_struct[10] = uuid_10; uuid_struct[11] = uuid_11; \
    uuid_struct[12] = uuid_12; uuid_struct[13] = uuid_13; uuid_struct[14] = uuid_14; uuid_struct[15] = uuid_15; \
  }while(0)

#define COPY_UART_SERVICE_UUID(uuid_struct)  COPY_UUID_128(uuid_struct,0x6E, 0x40, 0x00, 0x01, 0xB5, 0xA3, 0xF3, 0x93, 0xE0, 0xA9, 0xE5, 0x0E, 0x24, 0xDC, 0xCA, 0x9E)
#define COPY_UART_TX_CHAR_UUID(uuid_struct)  COPY_UUID_128(uuid_struct,0x6E, 0x40, 0x00, 0x02, 0xB5, 0xA3, 0xF3, 0x93, 0xE0, 0xA9, 0xE5, 0x0E, 0x24, 0xDC, 0xCA, 0x9E)
#define COPY_UART_RX_CHAR_UUID(uuid_struct)  COPY_UUID_128(uuid_struct,0x6E, 0x40, 0x00, 0x03, 0xB5, 0xA3, 0xF3, 0x93, 0xE0, 0xA9, 0xE5, 0x0E, 0x24, 0xDC, 0xCA, 0x9E)

void bleSetup() {
  HCI_Init();
  BNRG_SPI_Init();
  BlueNRG_RST();

  const uint32_t *uid = (const uint32_t *)0x0080A00C;
  uint32_t words[] = {uid[0], uid[13], uid[14], uid[15]};
  char identity[33];
  snprintf(identity, sizeof(identity), "%08lx%08lx%08lx%08lx",
           (unsigned long)words[0], (unsigned long)words[1], (unsigned long)words[2], (unsigned long)words[3]);
  snprintf(BLE_NAME, sizeof(BLE_NAME), "TinyKey-%.12s", identity);
  uint8_t bdaddr[6] = {0};
  for (uint8_t i = 0; i < 6; i++)
    for (uint8_t w = 0; w < 4; w++) bdaddr[i] ^= (uint8_t)(words[w] >> (((i + w) % 4) * 8));
  bdaddr[5] = (bdaddr[5] & 0x3F) | 0xC0;  // static random address
  if (hci_le_set_random_address(bdaddr) != BLE_STATUS_SUCCESS) {
    // no BLE shield answering: a floating IRQ pin would keep firing HCI_Isr and stall the watch
    detachInterrupt(BNRG_SPI_EXTI_PIN);
    bleAdvertise = false;
    blePresent = false;
    return;
  }
  aci_gatt_init();

  uint16_t service_handle, dev_name_char_handle, appearance_char_handle;
  aci_gap_init_IDB05A1(GAP_PERIPHERAL_ROLE_IDB05A1, 0, sizeof(BLE_NAME) - 1, &service_handle, &dev_name_char_handle, &appearance_char_handle);
  aci_gatt_update_char_value(service_handle, dev_name_char_handle, 0, strlen(BLE_NAME), (uint8_t *)BLE_NAME);

  uint8_t uuid[16];
  COPY_UART_SERVICE_UUID(uuid);
  aci_gatt_add_serv(UUID_TYPE_128, uuid, PRIMARY_SERVICE, 7, &UARTServHandle);
  COPY_UART_TX_CHAR_UUID(uuid);  // laptop writes here
  aci_gatt_add_char(UARTServHandle, UUID_TYPE_128, uuid, BLE_PACKET, CHAR_PROP_WRITE_WITHOUT_RESP, ATTR_PERMISSION_NONE,
                    GATT_NOTIFY_ATTRIBUTE_WRITE, 16, 1, &UARTTXCharHandle);
  COPY_UART_RX_CHAR_UUID(uuid);  // watch notifies here
  aci_gatt_add_char(UARTServHandle, UUID_TYPE_128, uuid, BLE_PACKET, CHAR_PROP_NOTIFY, ATTR_PERMISSION_NONE,
                    0, 16, 1, &UARTRXCharHandle);

  aci_hal_set_tx_power_level(1, 3);  // +4 dBm
}

void bleLoop() {
  if (!blePresent) return;
  HCI_Process();
  static unsigned long lastTry = 0;
  if (bleAdvertise && millis() - lastTry >= 100) {
    lastTry = millis();
    char adv[sizeof(BLE_NAME)];  // AD type byte + complete name
    adv[0] = AD_TYPE_COMPLETE_LOCAL_NAME;
    memcpy(adv + 1, BLE_NAME, sizeof(BLE_NAME) - 1);
    hci_le_set_scan_resp_data(0, NULL);
    tBleStatus ret = aci_gap_set_discoverable(ADV_IND, (50 * 1000) / 625, (100 * 1000) / 625, STATIC_RANDOM_ADDR,
                                              NO_WHITE_LIST_USE, sizeof(adv), adv, 0, NULL, 0, 0);
    bleAdvertise = ret != BLE_STATUS_SUCCESS;  // the chip can refuse right after a disconnect: retry until it takes
  }
  while (bleRxTail != bleRxHead) feedByte(bleIn, bleRx[bleRxTail++], true);
}

// Sends one notification, retrying while the radio's buffers are full.
bool bleNotify(const char *data, uint8_t len) {
  unsigned long start = millis();
  while (bleConnected) {
    tBleStatus ret = aci_gatt_update_char_value(UARTServHandle, UARTRXCharHandle, 0, len, (uint8_t *)data);
    if (ret == BLE_STATUS_SUCCESS) return true;
    if (ret != BLE_STATUS_INSUFFICIENT_RESOURCES || millis() - start > 500) return false;
    HCI_Process();  // keeps events (like a disconnect) flowing while we wait
  }
  return false;
}

// Sends text plus a newline in packets of at most 20 bytes.
void bleSend(const char *text) {
  size_t n = strlen(text);
  char packet[BLE_PACKET];
  size_t pos = 0;
  while (pos <= n) {  // pos == n is the newline
    uint8_t len = 0;
    while (len < BLE_PACKET && pos <= n) {
      packet[len++] = pos < n ? text[pos] : '\n';
      pos++;
    }
    // shortcut: a line cut off here leaves a fragment the laptop glues to the next line, which then fails to parse; fine at our message rate
    if (!bleNotify(packet, len)) return;
  }
}

// Ends the Bluetooth link from the watch side, like walking out of range; the reconnect test uses it.
void bleDrop() {
  if (bleConnected) aci_gap_terminate(bleConnHandle, 0x13);  // 0x13: remote user terminated connection
}

void Attribute_Modified_CB(uint16_t handle, uint8_t data_length, uint8_t *att_data) {
  if (handle != UARTTXCharHandle + 1) return;
  for (int i = 0; i < data_length; i++) {
    if ((uint8_t)(bleRxHead + 1) == bleRxTail) return;  // full: drop the rest; laptop never sends this much at once
    bleRx[bleRxHead++] = att_data[i];
  }
}

void HCI_Event_CB(void *pckt) {
  hci_uart_pckt *hci_pckt = (hci_uart_pckt *)pckt;
  hci_event_pckt *event_pckt = (hci_event_pckt *)hci_pckt->data;
  if (hci_pckt->type != HCI_EVENT_PKT) return;

  switch (event_pckt->evt) {
    case EVT_DISCONN_COMPLETE:
      bleConnected = false;
      bleAdvertise = true;  // be findable again so the laptop can reconnect
      bleRxHead = bleRxTail = 0;
      resetReader(bleIn);
      break;

    case EVT_LE_META_EVENT: {
      evt_le_meta_event *evt = (evt_le_meta_event *)event_pckt->data;
      if (evt->subevent == EVT_LE_CONN_COMPLETE) {
        bleConnected = true;
        bleConnHandle = ((evt_le_connection_complete *)evt->data)->handle;
      }
      break;
    }

    case EVT_VENDOR: {
      evt_blue_aci *blue_evt = (evt_blue_aci *)event_pckt->data;
      if (blue_evt->ecode == EVT_BLUE_GATT_ATTRIBUTE_MODIFIED) {
        evt_gatt_attr_modified_IDB05A1 *evt = (evt_gatt_attr_modified_IDB05A1 *)blue_evt->data;
        Attribute_Modified_CB(evt->attr_handle, evt->data_length, evt->att_data);
      }
      break;
    }
  }
}
