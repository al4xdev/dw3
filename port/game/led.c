/* led.c: the game's side of the handheld's LED bar (led.h; docs/HANDHELD.md, "The LED bar is on a UART, not in sysfs").
 *
 * The bar is an MCU on /dev/ttyS2 and there is no sysfs node for it: /sys/class/leds is empty. This mod does not touch
 * the tty either. It talks to the package's daemon over a Unix socket in /tmp (the card's filesystem refuses Unix
 * sockets), and the daemon is the only process that opens the line - a second writer is the thing a lock cannot fix.
 *
 * What a reading costs: one connection, one line, one reply, and only when the level changes. Measured on the console
 * at ~2 ms (the round trip, the daemon's floor between writes included): 0.1 % of a battle frame, a handful of times a
 * fight. A frame-by-frame animation is impossible on this path and is not attempted - the MCU animates the breathing
 * modes itself and one byte is enough.
 *
 * Off the handheld this is a no-op: no socket, no ledd.py, one log line and it stops looking. A Windows build has no
 * Unix socket, so the whole mod compiles to nothing there. */
#include <stdio.h>
#include <string.h>

#include "fightstg.h"
#include "led.h"
#include "port_harness.h"
#include "port_runtime.h"

#ifdef _WIN32

void led_start(void) {}
void led_frame(void) {}
const char *led_status(void) { return NULL; }
void led_stop(void) {}

#else /* the handheld: a Unix socket to the daemon */

#include <fcntl.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#define LEDD_SOCK "/tmp/ledd.sock"
#define LEDD_FILE "ledd.py" /* in the package's directory, which is the game's working directory (handheld/dw3.sh) */
#define LED_REPLY 96

static int led_ready;      /* the daemon answered once: send changes from here on */
static int led_gave_up;    /* no daemon and no ledd.py: logged once, never looked again */
static int led_level = -1; /* the last level sent, -1 before the first */
static long led_next_try;  /* port_frames at which to look for the daemon again after it went away */

/* One request, one reply. The reply is read so the daemon is never left writing into a socket this side closed. */
static int led_ask(const char *request) {
    struct sockaddr_un addr;
    char reply[LED_REPLY];
    ssize_t sent;
    int fd;

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        return 0;
    }
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, LEDD_SOCK, sizeof(addr.sun_path) - 1);
    if (connect(fd, (struct sockaddr *)&addr, (socklen_t)sizeof(addr)) != 0) {
        close(fd);
        return 0;
    }
    sent = send(fd, request, strlen(request), 0);
    if (sent != (ssize_t)strlen(request)) {
        close(fd);
        return 0;
    }
    (void)recv(fd, reply, sizeof(reply) - 1, 0);
    close(fd);
    return 1;
}

/* The daemon, started once if it is not already there: no shell, and the child's own double fork is what keeps it
 * alive after this one exits. */
static void led_wake_daemon(void) {
    struct timespec pause;
    pid_t pid;

    if (access("./" LEDD_FILE, R_OK) != 0) {
        return; /* not the handheld's package: nothing to start, and no python3 to start it with */
    }
    pid = fork();
    if (pid == 0) {
        int null = open("/dev/null", O_RDWR);

        if (null >= 0) {
            dup2(null, 0);
            dup2(null, 1);
            dup2(null, 2);
        }
        execlp("python3", "python3", "./" LEDD_FILE, "--detach", (char *)NULL);
        _exit(127); /* no python3 on this machine: the parent's retry decides what to say */
    }
    pause.tv_sec = 0;
    pause.tv_nsec = 300000000L; /* 300 ms: its fork, socket bind and flock */
    nanosleep(&pause, NULL);
}

static void led_apply(int level) {
    char request[48];

    if (!led_ready || level == led_level) {
        return;
    }
    if (level == LED_NONE) {
        /* Outside a battle the bar goes dark and the regulator drops: nothing is being watched there, and the rail
         * only belongs up while a reading is on it. LED_OUT is a reading (the bar's rotating mode), so it is sent as
         * the daemon's level 6 like any other. */
        snprintf(request, sizeof(request), "off\n");
    } else {
        snprintf(request, sizeof(request), "level %d %d\n", level, (int)getpid());
    }
    if (!led_ask(request)) {
        led_ready = 0; /* it died under us: led_frame looks for it again, and led_start logs nothing more */
    }
    led_level = level;
    port_log("led bar: frame %ld: level %d", port_frames, level);
}

/* The party's health as one of the six readings: the weakest member that is in this battle, because a party is as
 * healthy as its worst member and that is the one worth noticing. A member with no max HP is a slot WFIGHTMN left
 * empty, not a knocked-out Digimon. */
static int led_read_level(void) {
    const FightstgMember *party = fightstg_battle.state.members[0];
    s32 worst = -1;
    int i;

    for (i = 0; i < 3; i++) {
        s32 pct;

        if (party[i].max_hp <= 0) {
            continue;
        }
        pct = (s32)party[i].hp * 100 / (s32)party[i].max_hp;
        if (worst < 0 || pct < worst) {
            worst = pct;
        }
    }
    if (worst < 0) {
        return LED_NONE; /* the state is not filled yet: the battle has not started */
    }
    if (worst <= 0) {
        return LED_OUT;
    }
    if (worst <= 20) {
        return LED_DYING;
    }
    if (worst <= 40) {
        return LED_CRITICAL;
    }
    if (worst <= 60) {
        return LED_BAD;
    }
    if (worst <= 80) {
        return LED_HURT;
    }
    return LED_HEALTHY;
}

void led_start(void) {
    if (led_ask("status\n")) {
        led_ready = 1;
        port_log("led bar: the daemon answers at %s", LEDD_SOCK);
        return;
    }
    if (access("./" LEDD_FILE, R_OK) != 0) {
        /* Not the handheld's package: nothing to start, and nothing will ever appear, so stop looking. */
        if (led_gave_up) {
            return;
        }
        led_gave_up = 1;
        port_log("led bar: no daemon at %s and no %s beside the game: the bar is left alone", LEDD_SOCK, LEDD_FILE);
        return;
    }
    led_wake_daemon();
    if (led_ask("status\n")) {
        led_ready = 1;
        port_log("led bar: the daemon started from ./%s", LEDD_FILE);
    }
}

void led_frame(void) {
    const PortOverlay *battle;

    if (!led_ready) {
        /* It may have died under us mid-session: look again every ~5 s at 60 Hz, unless this machine has no daemon at
         * all (led_gave_up), in which case there is nothing to look for. */
        if (led_gave_up || port_frames < led_next_try) {
            return;
        }
        led_start();
        led_next_try = port_frames + 300;
        if (!led_ready) {
            return;
        }
        led_level = -1; /* whatever the bar was showing is not ours any more */
    }
    battle = port_overlay_current(1);
    if (battle == NULL || strcmp(battle->name, "FIGHTSTG") != 0) {
        led_apply(LED_NONE);
        return;
    }
    led_apply(led_read_level());
}

const char *led_status(void) {
    static const char *const names[] = { "led", "led 1", "led 2", "led 3", "led 4", "led 5", "led out" };

    if (led_level < 0 || led_level > LED_OUT) {
        return NULL;
    }
    return names[led_level];
}

#endif /* _WIN32 */
