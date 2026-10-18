#ifndef ERROR_HANDLER_H
#define ERROR_HANDLER_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Initialisiert den globalen Error Handler
void error_handler_init(void);

// Zeigt eine Fehlermeldung an und versucht die App am Laufen zu halten
void show_error_message(const char *title, const char *message);

// Prüft ob ein kritischer Fehler aufgetreten ist
bool has_critical_error(void);

#ifdef __cplusplus
}
#endif

#endif // ERROR_HANDLER_H
