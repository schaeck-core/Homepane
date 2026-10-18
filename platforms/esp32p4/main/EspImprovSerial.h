/*
 * Improv Wi-Fi over the console UART (the board's USB port): the web
 * flasher asks for the Wi-Fi credentials right after flashing and sends
 * them over the same cable. Packet format in app/cpp/net/ImprovSerial.h;
 * connecting goes through the WifiController (app_cpp_wifi_connect()), so
 * the credentials are saved exactly as if they were entered on the panel.
 */
#ifndef ESP_IMPROV_SERIAL_H
#define ESP_IMPROV_SERIAL_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Take over reading the console UART and answer Improv requests. Call early
 * in app_main (after esp_netif_init()): the browser asks about one second
 * after the board was reset. Log output keeps going to the same UART.
 */
void improv_serial_start(void);

#ifdef __cplusplus
}
#endif

#endif /* ESP_IMPROV_SERIAL_H */
