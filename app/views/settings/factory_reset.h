/* Resetting the panel (Settings > Home Assistant): a question first, then
 * the logo loader with the steps (sign out of Home Assistant, delete the
 * settings) and a restart into the setup. UI thread. */
#ifndef FACTORY_RESET_H
#define FACTORY_RESET_H

#ifdef __cplusplus
extern "C" {
#endif

/* Asks; "Reset" starts it. */
void factory_reset_ask(void);

#ifdef __cplusplus
}
#endif

#endif /* FACTORY_RESET_H */
