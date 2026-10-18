#ifndef APP_CPP_UPDATE_UPDATESOURCE_H
#define APP_CPP_UPDATE_UPDATESOURCE_H

// Where devices look for firmware updates: the signed manifest that a
// release publishes next to the web flasher (GitHub Pages).
// Fixed at build time on purpose: not a user setting and not in config.ini.
// Another update source (a fork, a private CI): set the environment
// variable HOMEPANE_OTA_MANIFEST_URL when configuring the firmware build.
#ifndef HOMEPANE_OTA_MANIFEST_URL
#define HOMEPANE_OTA_MANIFEST_URL "https://schaeck-core.github.io/Homepane/firmware-manifest.json"
#endif

#endif // APP_CPP_UPDATE_UPDATESOURCE_H
