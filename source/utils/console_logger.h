#ifndef ETERNAL_CONSOLE_LOGGER_H
#define ETERNAL_CONSOLE_LOGGER_H
/* Bounded console-only worker. Producers never wait for console output, and
 * overflow is counted rather than turning a game frame into an output wait. */
int console_logger_start(void);
void console_logger_poll(int force);
void console_logger_shutdown(void);
/* Original game's debug stdout only; file streams and formatting APIs retain
 * their libc implementations. Return the full formatted length as printf does. */
int console_game_printf(const char *format, ...);
int console_game_puts(const char *text);
#endif
