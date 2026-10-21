/* The LED bar of the RK3326 handhelds (docs/HANDHELD.md, "The LED bar is on a UART, not in sysfs"): the mod that
 * shows the party's health on it, through the daemon of the package (handheld/ledd.py; `ledd` in the settings
 * directory once the handheld menu has installed it).
 *
 * Why a daemon and not the game writing the bar: what costs in that path is not the byte (1 ms on the wire at 9600
 * 8N1) but paying the setup every time. Measured on the console: the vendor script 66 ms a change, the mcu_led binary
 * 18 ms, opening and closing the tty with no fork at all ~14 ms, the same fd held open 0.15-0.9 ms. One process owns
 * the tty and its lock; the game sends one short message when the reading changes.
 *
 * The reading itself: five steps of colour and rhythm plus the bar's own rotating mode for a knocked-out party. The
 * palette is the measured one (docs/HANDHELD.md), not the names of the vendor script. */
#ifndef LED_H
#define LED_H

/* The six readings the daemon knows, plus "nothing to show" (outside a battle, the bar goes dark and the regulator
 * drops: nobody is watching it there). */
enum { LED_NONE, LED_HEALTHY, LED_HURT, LED_BAD, LED_CRITICAL, LED_DYING, LED_OUT };

void led_start(void);         /* once, when the mod is enabled: find or start the daemon */
void led_frame(void);         /* every vsync: read the party, send what changed */
const char *led_status(void); /* the window title's text while the mod is on, or NULL */

/* There is no led_stop: the runtime has no "mod disabled" callback, and it is not needed. The daemon knows the game's
 * pid (every level it is sent carries it) and turns the bar off within a second of that process going away - which
 * covers a clean exit, a crash and a kill alike. */

#endif
