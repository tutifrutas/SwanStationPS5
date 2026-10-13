/*
 * SwanStationPS5 disc daemon - a payload that stays resident (send it once to the console's ELF loader, port 9021).
 * A PS1 disc put in the USB CD drive is installed to /data/SwanStationPS5/games without the app being open, with the
 * game's name and the percentage as the console's own notifications; then SwanStationPS5 is started, which finds the
 * disc and the game and starts it.
 *
 * The disc code is the app's own (src/discimport.c, src/cdscsi.c, src/disc.c), compiled in with a small SDL
 * replacement (payload/shim). The way it reaches the drive, installs the disc and starts the app follows the disc
 * daemon of PS5SX2 (ps5/disclaunch/disc_launch.c by swordpdf, GPL-3.0-or-later) and ps-disc-detect (Al-Azif).
 * While the app is open it looks after the drive itself, and the daemon does not touch it.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "SwanStationPS5.h"
#include "cdscsi.h"
#include "discimport.h"

#include <dlfcn.h>
#include <errno.h>
#include <math.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#include <sys/user.h>
#include <time.h>
#include <unistd.h>

#include <ps5/kernel.h>
#include <ps5/klog.h>

#define TITLE_ID "PPSA98510"
#define ROOT "/data/SwanStationPS5"
#define GAMES ROOT "/games"
#define CONFIG ROOT "/SwanStationPS5.ini"
#define APP_ALIVE ROOT "/app-alive" /* the app writes the time in here every couple of seconds */
#define LAUNCH_MARK ROOT "/daemon-launch" /* tells the app that this daemon opened it, so it keeps quiet at first */
#define BUSY ROOT "/disc-busy"       /* this daemon writes the time in here while it installs a disc */
#define PID_FILE ROOT "/disc-daemon.pid"
#define LOG_FILE ROOT "/logs/disc-daemon.log"

enum
{
    kPollMs = 500,
    kSettleSec = 2,     /* a disc just put in: the system's own screen is on for a moment */
    kAliveWithin = 8,   /* seconds since the app's last sign of life */
    kProgressStep = 5,  /* a notification every this many percent */
    kAfterCloseSec = 5  /* after the app closes, the disc that is in is not news */
};

/* ------------------------------------------------------------------ texts: the app's five languages */

enum Msg
{
    M_READY,
    M_INSTALLING,
    M_PROGRESS,
    M_PROGRESS_ETA,
    M_INSTALLED,
    M_INSTALLED_BAD,
    M_ALREADY,
    M_NO_DRIVE,
    M_NO_DISC,
    M_NOT_PS1,
    M_UNREADABLE,
    M_REMOVED,
    M_WRITE_FAILED,
    M_STALLED,
    /* the same four, for when the disc is known by name */
    M_UNREADABLE_N,
    M_REMOVED_N,
    M_WRITE_FAILED_N,
    M_STALLED_N,
    M_COUNT
};

/* en, fr, pt, es, ja - the same order as the app's language setting (src/i18n.h) */
static const char *const TEXT[M_COUNT][5] = {
    [M_READY] = {"SSPS5 ready...", "SSPS5 prêt...", "SSPS5 pronto...", "SSPS5 listo...", "SSPS5 準備完了..."},
    [M_INSTALLING] = {"Installing %s...", "Installation de %s...", "A instalar %s...", "Instalando %s...",
                      "%s をインストール中..."},
    [M_PROGRESS] = {"Installing %s: %d%%", "Installation de %s : %d%%", "A instalar %s: %d%%",
                    "Instalando %s: %d%%", "%s をインストール中: %d%%"},
    /* title, percent, time left ("5 min 30 s") */
    [M_PROGRESS_ETA] = {"Installing %s: %d%% - %s left...", "Installation de %s : %d%% - %s restant...",
                        "A instalar %s: %d%% - %s restantes...", "Instalando %s: %d%% - %s restantes...",
                        "%s をインストール中: %d%% - 残り %s..."},
    [M_INSTALLED] = {"%s is installed.", "%s est installé.", "%s está instalado.", "%s está instalado.",
                     "%s のインストールが完了しました。"},
    [M_INSTALLED_BAD] = {"%s is installed with %d unreadable sectors.",
                         "%s est installé avec %d secteurs illisibles.",
                         "%s está instalado com %d setores ilegíveis.",
                         "%s está instalado con %d sectores ilegibles.",
                         "%s を読めないセクタ %d 個でインストールしました。"},
    [M_ALREADY] = {"Loading %s...", "Chargement de %s...", "A carregar %s...", "Cargando %s...",
                   "%s を読み込み中..."},
    [M_NO_DRIVE] = {"No CD drive was found.", "Aucun lecteur de CD trouvé.", "Nenhum leitor de CD encontrado.",
                    "No se encontró ningún lector de CD.", "CD ドライブが見つかりません。"},
    [M_NO_DISC] = {"The CD drive has no disc ready.", "Le lecteur de CD n'a pas de disque prêt.",
                   "O leitor de CD não tem disco pronto.", "El lector de CD no tiene un disco listo.",
                   "CD ドライブに使えるディスクがありません。"},
    [M_NOT_PS1] = {"This is not a PS1 game disc. Nothing was installed.",
                   "Ce n'est pas un disque de jeu PS1. Rien n'a été installé.",
                   "Este não é um disco de jogo de PS1. Nada foi instalado.",
                   "Este no es un disco de juego de PS1. No se instaló nada.",
                   "PS1 のゲームディスクではありません。何もインストールされていません。"},
    [M_UNREADABLE] = {"The disc could not be read: too many bad sectors. What was copied was deleted.",
                      "Le disque est illisible : trop de secteurs défectueux. Ce qui avait été copié a été supprimé.",
                      "Não foi possível ler o disco: demasiados setores com erro. O que foi copiado foi apagado.",
                      "No se pudo leer el disco: demasiados sectores con error. Lo copiado se borró.",
                      "ディスクを読めません: 不良セクタが多すぎます。コピー済みのデータは削除しました。"},
    [M_REMOVED] = {"The disc was removed: installation cancelled. What was copied was deleted.",
                   "Le disque a été retiré : installation annulée. Ce qui avait été copié a été supprimé.",
                   "O disco foi removido: instalação cancelada. O que foi copiado foi apagado.",
                   "Se quitó el disco: instalación cancelada. Lo copiado se borró.",
                   "ディスクが取り出されました: インストールをキャンセルしました。コピー済みのデータは削除しました。"},
    [M_WRITE_FAILED] = {"The game could not be written to the games folder.",
                        "Impossible d'écrire le jeu dans le dossier des jeux.",
                        "Não foi possível gravar o jogo na pasta de jogos.",
                        "No se pudo escribir el juego en la carpeta de juegos.",
                        "ゲームフォルダに書き込めませんでした。"},
    [M_STALLED] = {"The CD drive did not read anything for 30 seconds: installation stopped.",
                   "Le lecteur de CD n'a rien lu pendant 30 secondes : installation arrêtée.",
                   "O leitor de CD não leu nada durante 30 segundos: instalação parada.",
                   "El lector de CD no leyó nada durante 30 segundos: instalación detenida.",
                   "CD ドライブが 30 秒間何も読めませんでした: インストールを中止しました。"},
    [M_UNREADABLE_N] = {"%s could not be read: too many bad sectors. What was copied was deleted.",
                        "%s est illisible : trop de secteurs défectueux. Ce qui avait été copié a été supprimé.",
                        "Não foi possível ler %s: demasiados setores com erro. O que foi copiado foi apagado.",
                        "No se pudo leer %s: demasiados sectores con error. Lo copiado se borró.",
                        "%s を読めません: 不良セクタが多すぎます。コピー済みのデータは削除しました。"},
    [M_REMOVED_N] = {"The disc of %s was removed: installation cancelled. What was copied was deleted.",
                     "Le disque de %s a été retiré : installation annulée. Ce qui avait été copié a été supprimé.",
                     "O disco de %s foi removido: instalação cancelada. O que foi copiado foi apagado.",
                     "Se quitó el disco de %s: instalación cancelada. Lo copiado se borró.",
                     "%s のディスクが取り出されました: インストールをキャンセルしました。コピー済みのデータは削除しました。"},
    [M_WRITE_FAILED_N] = {"%s could not be written to the games folder.",
                          "Impossible d'écrire %s dans le dossier des jeux.",
                          "Não foi possível gravar %s na pasta de jogos.",
                          "No se pudo escribir %s en la carpeta de juegos.",
                          "%s をゲームフォルダに書き込めませんでした。"},
    [M_STALLED_N] = {"The CD drive did not read anything for 30 seconds: installation of %s stopped.",
                     "Le lecteur de CD n'a rien lu pendant 30 secondes : installation de %s arrêtée.",
                     "O leitor de CD não leu nada durante 30 segundos: instalação de %s parada.",
                     "El lector de CD no leyó nada durante 30 segundos: instalación de %s detenida.",
                     "CD ドライブが 30 秒間何も読めませんでした: %s のインストールを中止しました。"},
};

static int language; /* the app's setting, read from its ini */

static void load_language(void)
{
    FILE *f = fopen(CONFIG, "r");
    char line[256];
    language = 0;
    while (f && fgets(line, sizeof(line), f))
        if (strncmp(line, "language=", 9) == 0)
        {
            int v = atoi(line + 9);
            language = v >= 0 && v < 5 ? v : 0;
        }
    if (f)
        fclose(f);
}

/* "Install discs only" (Settings > System > SwanStationPS5), read for every disc: the game is not started, so the app is
 * not opened, and a disc whose game is installed already is left alone */
static bool install_only(void)
{
    FILE *f = fopen(CONFIG, "r");
    char line[256];
    bool on = false;
    while (f && fgets(line, sizeof(line), f))
        if (strncmp(line, "disc_install_only=", 18) == 0)
            on = atoi(line + 18) != 0;
    if (f)
        fclose(f);
    return on;
}

/* ------------------------------------------------------------------ notifications and log */

typedef struct
{
    char unused[45];
    char message[3075];
} notify_req_t;
int sceKernelSendNotificationRequest(int32_t device, void *req, size_t size, int32_t blocking);

static void say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void say(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    klog_printf("[SwanStationPS5 disc] %s\n", buf);
    SwanStationPS5_log("daemon: %s", buf);
}

static void notify(enum Msg m, ...)
{
    static notify_req_t req;
    memset(&req, 0, sizeof(req));
    va_list ap;
    va_start(ap, m);
    vsnprintf(req.message, sizeof(req.message), TEXT[m][language], ap);
    va_end(ap);
    int rc = sceKernelSendNotificationRequest(0, &req, sizeof(req), 0);
    say("notification (rc %d): %s", rc, req.message);
}

/* "5 min 30 s" (Japanese: "5分30秒"), rounded to 5 seconds */
static void format_left(char *out, size_t size, long seconds)
{
    seconds = (seconds + 2) / 5 * 5;
    if (seconds < 5)
        seconds = 5;
    long m = seconds / 60, sec = seconds % 60;
    if (language == 4)
    {
        if (m)
            snprintf(out, size, "%ld分%ld秒", m, sec);
        else
            snprintf(out, size, "%ld秒", sec);
    }
    else if (m)
        snprintf(out, size, "%ld min %ld s", m, sec);
    else
        snprintf(out, size, "%ld s", sec);
}

/* The time left, from the speed just measured and the shape of a CD. A CD spins at fixed revolutions and its sectors
 * run in a spiral of constant density, so the read speed at any point goes with the radius there, and the radius
 * with how many sectors came before it: r(f)^2 = rin^2 + K f, f the fraction done. With the speed measured over the
 * last stretch (at the stretch's middle), the rest takes (N r_mid / v) * (2 / K) * (r(1) - r(now)).
 * A drive that does not speed up towards the edge gets the plain "same speed from here" estimate instead. */
#define CD_R_IN 24.0f    /* mm, where the data starts */
#define CD_R_OUT 58.0f   /* mm, the largest it gets */
#define CD_CAPACITY 333000.0 /* sectors of a 74 minute disc, at that largest radius */

typedef struct
{
    double prev_f;      /* where the last notification was (fraction), and when */
    double prev_time;
    double prev_rate;   /* the speed of that stretch, sectors/s, and the radius at its middle */
    double prev_rmid;
} Eta;

static double cd_radius(double k, double f)
{
    return sqrt((double)CD_R_IN * CD_R_IN + k * f);
}

/* seconds left, or < 0 when there is nothing to base it on */
static long eta_left(Eta *e, double f_now, double now, int total)
{
    long left = -1;
    if (total > 0 && f_now > e->prev_f && now > e->prev_time && f_now < 1.0)
    {
        double dt = now - e->prev_time, rate = total * (f_now - e->prev_f) / dt;
        double plain = (1.0 - f_now) * dt / (f_now - e->prev_f); /* the same speed for the rest */
        double k = (total / CD_CAPACITY) * ((double)CD_R_OUT * CD_R_OUT - (double)CD_R_IN * CD_R_IN);
        double rmid = cd_radius(k, (e->prev_f + f_now) * 0.5);
        double modelled = (total * rmid / rate) * (2.0 / k) * (cd_radius(k, 1.0) - cd_radius(k, f_now));
        /* does this drive speed up the way the model says? at least half of the speed-up the model expects between
         * the two stretches (the noise of one stretch is a percent or two, the model's step is a few) */
        bool speeds_up = true;
        if (e->prev_rate > 0.0 && e->prev_rmid > 0.0)
            speeds_up = rate / e->prev_rate >= 1.0 + 0.5 * (rmid / e->prev_rmid - 1.0);
        double best = speeds_up && modelled > 0.0 && modelled < 3.0 * plain ? modelled : plain;
        left = (long)best;
        e->prev_rate = rate;
        e->prev_rmid = rmid;
    }
    e->prev_f = f_now;
    e->prev_time = now;
    return left;
}

static double now_seconds(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

/* ------------------------------------------------------------------ the app: alive, and started */

static bool stamp_fresh(const char *path, int within)
{
    FILE *f = fopen(path, "r");
    long long t = 0;
    if (!f)
        return false;
    int got = fscanf(f, "%lld", &t);
    fclose(f);
    long long now = (long long)time(NULL);
    return got == 1 && now >= t && now - t <= within;
}

static void stamp(const char *path)
{
    FILE *f = fopen(path, "w");
    if (f)
    {
        fprintf(f, "%lld\n", (long long)time(NULL));
        fclose(f);
    }
}

typedef struct
{
    uint32_t size;
    int user_id;
    uint32_t app_opt;
    uint64_t crash_report;
    uint64_t check_flag;
} lnc_app_param_t;
typedef int (*LaunchApp_t)(const char *, const char **, lnc_app_param_t *);
typedef int (*UsrInit_t)(void *);
typedef int (*UsrFg_t)(int *);

static LaunchApp_t p_launch;
static UsrFg_t p_fguser;

static void load_launcher(void)
{
    void *h = dlopen("libSceSystemService.sprx", RTLD_NOW | RTLD_GLOBAL);
    if (h)
        p_launch = (LaunchApp_t)dlsym(h, "sceLncUtilLaunchApp");
    void *u = dlopen("libSceUserService.sprx", RTLD_NOW | RTLD_GLOBAL);
    if (u)
    {
        UsrInit_t init = (UsrInit_t)dlsym(u, "sceUserServiceInitialize");
        p_fguser = (UsrFg_t)dlsym(u, "sceUserServiceGetForegroundUser");
        if (init)
            say("sceUserServiceInitialize = 0x%08x", (unsigned)init(NULL));
    }
    say("launcher: %s", p_launch ? "sceLncUtilLaunchApp found" : "sceLncUtilLaunchApp NOT found");
}

/* Starts the app the way its tile does. Never fork + LoadExec: it panicked the kernel for PS5SX2. */
static void launch_app(void)
{
    if (!p_launch)
        return;
    int user = -1;
    if (p_fguser)
        p_fguser(&user);
    lnc_app_param_t prm;
    memset(&prm, 0, sizeof(prm));
    prm.size = sizeof(prm);
    prm.user_id = user;
    stamp(LAUNCH_MARK);
    int rc = p_launch(TITLE_ID, NULL, &prm);
    say("sceLncUtilLaunchApp(" TITLE_ID ", user %d) = 0x%08x", user, (unsigned)rc);
}

/* ------------------------------------------------------------------ one daemon at a time */

static void take_over(void)
{
    FILE *f = fopen(PID_FILE, "r");
    int old = 0;
    if (f)
    {
        if (fscanf(f, "%d", &old) == 1 && old > 0 && old != (int)getpid())
        {
            /* only a process that is still this daemon: the comm of the process is the loader's */
            int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, old};
            static unsigned char buf[8192];
            size_t len = sizeof(buf);
            memset(buf, 0, sizeof(buf));
            if (sysctl(mib, 4, buf, &len, NULL, 0) == 0 && len > 0)
            {
                const struct kinfo_proc *kp = (const struct kinfo_proc *)buf;
                if (kp->ki_pid == old && (strstr(kp->ki_comm, "payload") || strstr(kp->ki_comm, "disc") ||
                                          strstr(kp->ki_comm, "Disc")))
                {
                    say("the previous daemon (pid %d, %s) is stopped", old, kp->ki_comm);
                    kill((pid_t)old, SIGKILL);
                }
            }
        }
        fclose(f);
    }
    f = fopen(PID_FILE, "w");
    if (f)
    {
        fprintf(f, "%d\n", (int)getpid());
        fclose(f);
    }
}

static bool superseded(void)
{
    FILE *f = fopen(PID_FILE, "r");
    int p = 0;
    if (!f)
        return false;
    int got = fscanf(f, "%d", &p);
    fclose(f);
    return got == 1 && p > 0 && p != (int)getpid();
}

/* ------------------------------------------------------------------ a disc */

static void handle_disc(const DiscId *id)
{
    say("a disc: '%s' (%s)", id->title, id->key);
    sleep(kSettleSec); /* the system's own screen for a disc it does not know, before anything of ours */
    const bool only = install_only();
    stamp(BUSY);
    if (!discimport_start(GAMES))
    {
        say("an installation is already running");
        remove(BUSY);
        return;
    }
    bool announced = false;
    int next_step = kProgressStep;
    Eta eta = {0};
    char title[96] = "";
    while (discimport_state() == DI_RUNNING)
    {
        sleep(1);
        stamp(BUSY);
        if (discimport_check_stall())
            break;
        if (discimport_dumping() && discimport_title()[0] && !announced) /* not for a game that was already there */
        {
            announced = true;
            str_copy(title, sizeof(title), discimport_title());
            eta.prev_time = now_seconds();
            notify(M_INSTALLING, title);
        }
        int pct = discimport_permille() / 10;
        if (announced && pct >= next_step)
        {
            long left = eta_left(&eta, discimport_permille() / 1000.0, now_seconds(), discimport_total_sectors());
            if (left >= 0)
            {
                char when[40];
                format_left(when, sizeof(when), left);
                notify(M_PROGRESS_ETA, title, pct, when);
            }
            else
                notify(M_PROGRESS, title, pct);
            while (next_step <= pct)
                next_step += kProgressStep;
        }
    }
    DiscImportState result = discimport_state();
    if (discimport_title()[0])
        str_copy(title, sizeof(title), discimport_title());
    int bad = discimport_bad();
    discimport_ack();
    remove(BUSY);
    switch (result)
    {
    case DI_OK: notify(M_INSTALLED, title); break;
    case DI_OK_INCOMPLETE: notify(M_INSTALLED_BAD, title, bad); break;
    case DI_ALREADY:
        if (!only) /* with install only the game is there already: nothing to say and nothing to open */
            notify(M_ALREADY, title);
        break;
    case DI_NO_DRIVE: notify(M_NO_DRIVE); break;
    case DI_NO_DISC: notify(M_NO_DISC); break;
    case DI_NOT_PS1: notify(M_NOT_PS1); break;
    /* by the game's name when the disc was identified, else in general */
    case DI_UNREADABLE: title[0] ? notify(M_UNREADABLE_N, title) : notify(M_UNREADABLE); break;
    case DI_DRIVE_LOST: title[0] ? notify(M_REMOVED_N, title) : notify(M_REMOVED); break;
    case DI_WRITE_FAILED: title[0] ? notify(M_WRITE_FAILED_N, title) : notify(M_WRITE_FAILED); break;
    case DI_STALLED: title[0] ? notify(M_STALLED_N, title) : notify(M_STALLED); break;
    default: break;
    }
    if (only)
        say("install only: the app is not opened");
    else if (result == DI_OK || result == DI_OK_INCOMPLETE || result == DI_ALREADY)
        launch_app(); /* it finds the disc in the drive and starts the game */
}

int main(void)
{
    /* /dev/cd* and notifications, whatever the loader gave us */
    uint8_t caps[16];
    memset(caps, 0xff, sizeof(caps));
    kernel_set_ucred_authid(getpid(), 0x4800000000010003ull);
    kernel_set_ucred_caps(getpid(), caps);
    signal(SIGCHLD, SIG_IGN);
    make_dirs(ROOT "/logs");
    SwanStationPS5_log_open(LOG_FILE);
    say("disc daemon, pid %d", (int)getpid());
    take_over();
    load_language();
    load_launcher();
    notify(M_READY);

    discwatch_set_active(true);
    bool was_open = false;
    time_t closed_at = 0;
    for (;;)
    {
        usleep(kPollMs * 1000);
        if (superseded())
        {
            say("another daemon took over: stopping");
            break;
        }
        const bool app_open = stamp_fresh(APP_ALIVE, kAliveWithin);
        discwatch_pause(app_open); /* the app watches the drive itself */
        DiscId id;
        if (app_open)
        {
            /* whatever was seen just before the app opened is the app's business: if it were kept, it would be acted
             * on the moment the app is closed, and the app would open again by itself */
            discwatch_take(&id);
            was_open = true;
            continue;
        }
        if (was_open)
        {
            was_open = false;
            closed_at = time(NULL);
        }
        /* a disc already in when the app is closed is not news: only one put in, or changed, after that is. The first
         * few seconds after the app closes, what the watcher reports (it looks at the drive again after a pause) is
         * the same disc. */
        if (closed_at && time(NULL) - closed_at < kAfterCloseSec)
        {
            discwatch_take(&id);
            continue;
        }
        if (discwatch_take(&id))
            handle_disc(&id);
    }
    return 0;
}
