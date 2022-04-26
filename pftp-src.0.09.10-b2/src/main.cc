#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <curses.h>
#include <panel.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>
#include "defines.h"
#include "tcp.h"
#include "server.h"
#include "displayhandler.h"
#include "keyhandler.h"
#include <sys/stat.h>

#ifdef TLS
#include "tlsutil.h"
#endif

CDisplayHandler *display = NULL;
CKeyHandler *keyhandler;
BOOKMARK *global_bookmark = NULL;
SERVERLIST *global_server = NULL;
int passive_default = 1, displayinitdone;
int bm_magic_max = -1, mode = MODE_FXP_NOCHAIN, random_refresh = 0;
char okay_dir[1024], uselocal_label[256];
bool use_own_ip = FALSE, use_okay_dir = FALSE, no_autologin =
    FALSE, stealth_enabled = FALSE, ff_enabled = 0;
bool need_resize;
int filtermethod = 0;
pthread_mutex_t syscall_lock;
pthread_mutex_t sigwinch_lock;
char startcwd[SERVER_WORKINGDIR_SIZE];
char *sites2login = NULL;
char *section = NULL;
int debug=0,filter_method=0;

bool DetermineOwnIP(char *device);

extern char own_ip[256];
extern char *localwindowlog[LOG_LINES];
extern pthread_mutex_t localwindowloglock;
char defaultfilter[256];
char notlslist[1024];


/* uhm, stolen from epic 4 sources... */
//typedef RETSIGTYPE sigfunc (int);
#define RETSIGTYPE void
typedef RETSIGTYPE sigfunc(int);
#define SIGNAL_HANDLER(x) \
	RETSIGTYPE x (int sig)


sigfunc *my_signal(int sig_no, sigfunc * sig_handler)
{
    struct sigaction sa, osa;

    if (sig_no < 0)
        return NULL;            /* Signal not implemented */

    sa.sa_handler = sig_handler;
    sigemptyset(&sa.sa_mask);
    sigaddset(&sa.sa_mask, sig_no);

    sa.sa_flags = 0;
#if defined(SA_RESTART) || defined(SA_INTERRUPT)
    if (SIGALRM == sig_no || SIGINT == sig_no) {
# if defined(SA_INTERRUPT)
        sa.sa_flags |= SA_INTERRUPT;
# endif                         /* SA_INTERRUPT */
    } else {
# if defined(SA_RESTART)
        sa.sa_flags |= SA_RESTART;
# endif                         /* SA_RESTART */
    }
#endif                          /* SA_RESTART || SA_INTERRUPT */

    if (0 > sigaction(sig_no, &sa, &osa))
        return (SIG_ERR);

    return (osa.sa_handler);
}

//static int waiting=0;
// this one is based on example for ncurses :P
//static void
//adjust(int sig)
SIGNAL_HANDLER(adjust)
{
debuglog("signal sigwinch");
    pthread_mutex_lock(&sigwinch_lock);
//    display->sigwinch(sig);
    need_resize=TRUE;
    pthread_mutex_unlock(&sigwinch_lock);
     (void) signal(SIGWINCH, adjust);    /* some systems need this */
}

void unlinklog()
{
    char temppath[2048];
    snprintf(temppath, sizeof(temppath), "%spftp.debug.log", okay_dir);
    unlink(temppath);
}

void debuglog(char *fmt, ...)
{
    char linebuf[2048];
    char pidbuf[10];
    va_list argp;
    FILE *lf;
    char temppath[2048];
    time_t curtime = time(NULL);
//   uid_t   oldid = geteuid();
    if (debug) {
    pthread_mutex_lock(&syscall_lock);

    va_start(argp, fmt);
    vsnprintf(linebuf, sizeof(linebuf), fmt, argp);
    va_end(argp);
    snprintf(temppath, sizeof(temppath), "%spftp.debug.log", okay_dir);
    lf = fopen(temppath, "a+");
    if (lf != NULL) {
        sprintf(pidbuf, "%u", getpid());
        fprintf(lf, "%.24s [%-6.6s] %s\n", ctime(&curtime), pidbuf,
                linebuf);
        fclose(lf);
    }
    pthread_mutex_unlock(&syscall_lock);
    }
}

void StripANSI(char *line)
{
    char *m_pos, *c = line;
    // simply search for an ESC and then wait until we read the "m" (or end)

    while (*c) {
        if (*c == 27) {
            // found the possible start of an ESC code
            // now search for the "m"
            if (*(c + 1) != '[') {
                *c = '.';
                c++;
                continue;
            }
            m_pos = c + 2;
            while (*m_pos && (*m_pos != 'm')) {
                if (((*m_pos >= '0') && (*m_pos <= '9'))
                    || (*m_pos == ';'))
                    m_pos += 1;
                else {
                    c++;
                    continue;
                }
            }

            if (*m_pos) {
                // now copy the remaining string
                strcpy(c, m_pos + 1);
            } else {
                // else we reached '\0'
                c++;
                continue;
            }
        } else {
            if (filtermethod == 0) {
                if (((unsigned char) *c <= 31)
                    || ((unsigned char) *c >= 127))
                    *c = '.';
            } else {
                if (((unsigned char) *c <= 31)
                    || ((unsigned char) *c == 127)
                    || ((unsigned char) *c == 155))
                    *c = '.';
            }
            c += 1;
        }
    }
/*                for (m = strlen(this->log[index]), c = this->log[index];
                     m > 0; m--, c++) {
                }*/

}

bool trytls(char *site)
{
    char site1[50];
    //well shouldnt use strtok here so lets do it this way
    strcpy(site1, ",");
    strcat(site1, site);
    strcat(site1, ",");
    if (!strstr(notlslist, site1))
        return true;
    return false;
}

bool FilterFilename(char *filename, char *filter)
{
    char *end, *start, *pattern /*, fixed[] = {"*nuked* .*"} */ ;
    bool pat_fault = FALSE;
    int len, fl_len;

    if (filter) {
        pattern =
            new(char[strlen(defaultfilter) + strlen(filter) + 1 + 1]);
        sprintf(pattern, "%s %s", defaultfilter, filter);
    } else {
        pattern = new(char[strlen(defaultfilter) + 1]);
        strcpy(pattern, defaultfilter);
    }

    // ignore patterns
    start = pattern;
    fl_len = strlen(filename);
    do {
        end = strchr(start, ' ');
        if (end)
            *end = '\0';

        len = strlen(start);

        if (wild_match
            ((unsigned char *) start, (unsigned char *) filename))
            pat_fault = TRUE;

        if (end) {
            start = end + 1;
            *end = ' ';
        } else
            start = NULL;

    }
    while (!pat_fault && start);

    delete(pattern);
    return (!pat_fault);
}

void *DetachDisplayHandler(void *dummy)
{
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
    if (!display->Init()) {
        displayinitdone = 1;
        delete(display);
        printf(E_DISPLAYHANDLER_INIT);
        return (dummy);
    }
//    my_signal(SIGWINCH, adjust);
    displayinitdone = 2;
    display->Loop();
    // this should be never reached since the displayhandler-thread will be killed by main thread
    return (dummy);
}

bool FireDisplayHandler(void)
{
    // since pthread cant start a thread on a class member, we do it this way
    debuglog("creating display thread");
    sigset_t newmask,oldmask;
    sigemptyset(&newmask);
    sigaddset(&newmask,SIGCHLD);
    sigaddset(&newmask,SIGTERM);
    sigaddset(&newmask,SIGQUIT);
    sigaddset(&newmask,SIGINT);
    sigaddset(&newmask,SIGHUP);
    sigaddset(&newmask,SIGWINCH);
    pthread_sigmask(SIG_BLOCK,&newmask,&oldmask);        

    if (pthread_create(&(display->thread), &(display->thread_attr),
             DetachDisplayHandler, NULL)) {
     pthread_sigmask(SIG_SETMASK,&oldmask,NULL);        
     return FALSE;
    }
    pthread_sigmask(SIG_SETMASK,&oldmask,NULL);        
    return TRUE;
}

void *DetachServer(void *th_arg)
{
    CServer *server = (CServer *) th_arg;

//    my_signal(SIGWINCH, SIG_IGN);
    debuglog("hi from new new server");
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
    server->Run();
    // this should be never reached since we kill the thread
    return (NULL);
}

void FireupLocalFilesys(void)
{
    BOOKMARK bm;
    CServer *new_server;
    SERVERLIST *new_sv, *sv_temp = global_server;

    // let's create a special server-object which accesses local files
    new_sv = new(SERVERLIST);
    new_sv->next = NULL;

    new_server = new(CServer);
    new_server->SetServerType(SERVER_TYPE_LOCAL);
    new_server->SetBMMagic(-1);
    new_sv->server = new_server;

    bm.label = new(char[strlen("local_filesys") + 1]);
    strcpy(bm.label, "local_filesys");
    bm.user = bm.host = bm.pass = bm.startdir = bm.exclude = bm.util_dir =
        bm.game_dir = bm.site_who = bm.site_user = bm.site_wkup = bm.label;
    bm.use_track = bm.use_jump = bm.use_noop = bm.use_refresh =
        bm.use_chaining = bm.use_utilgames = FALSE;
    new_server->SetServerPrefs(&bm);

    delete(bm.label);

    if (!sv_temp) {
        global_server = new_sv;
        new_sv->magic = SERVER_MAGIC_START;
    } else {
        while (sv_temp->next)
            sv_temp = sv_temp->next;

        sv_temp->next = new_sv;
        new_sv->magic = sv_temp->magic + 1;
    }

    new_server->SetMagic(new_sv->magic);

    // we use global display-attr, keep in mind!
    debuglog("creating new localserver");
    sigset_t newmask,oldmask;
    sigemptyset(&newmask);
    sigaddset(&newmask,SIGCHLD);
    sigaddset(&newmask,SIGTERM);
    sigaddset(&newmask,SIGQUIT);
    sigaddset(&newmask,SIGINT);
    sigaddset(&newmask,SIGHUP);
    sigaddset(&newmask,SIGWINCH);
    pthread_sigmask(SIG_BLOCK,&newmask,&oldmask);        
    pthread_create(new_server->GetThreadAddress(), &(display->thread_attr),
                   DetachServer, (void *) new_server);
    pthread_sigmask(SIG_SETMASK,&oldmask,NULL);        
}

void FireupRemoteServer(CServer * new_server)
{
    debuglog("creating new remoteserver");
#ifndef CYGWIN
    sigset_t newmask,oldmask;
    sigemptyset(&newmask);
    sigaddset(&newmask,SIGCHLD);
    sigaddset(&newmask,SIGTERM);
    sigaddset(&newmask,SIGQUIT);
    sigaddset(&newmask,SIGINT);
    sigaddset(&newmask,SIGHUP);
    sigaddset(&newmask,SIGWINCH);
    pthread_sigmask(SIG_BLOCK,&newmask,&oldmask);        
#endif
    pthread_create(new_server->GetThreadAddress(), &(display->thread_attr),
                   DetachServer, (void *) new_server);
#ifndef CYGWIN
    pthread_sigmask(SIG_SETMASK,&oldmask,NULL);        
#endif
}

void FreeBookmarks(void)
{
    BOOKMARK *bm_temp1, *bm_temp = global_bookmark;

    while (bm_temp) {
        bm_temp1 = bm_temp;
        bm_temp = bm_temp->next;

        delete(bm_temp1->label);
        delete(bm_temp1->host);
        delete(bm_temp1->user);
        delete(bm_temp1->pass);
        delete(bm_temp1->startdir);
        delete(bm_temp1->exclude);

        delete(bm_temp1);
    }

    global_bookmark = NULL;
}

bool ReadConfig(char *filename)
{
    FILE *in_file, *dircheck;
    char line[1024], label[32], value[1024], *start;

    strcpy(uselocal_label, "");
    strcpy(notlslist, "");
    strcpy(defaultfilter, "");
    strcpy(own_ip,"");
    strcpy(okay_dir,"");

    if (!(in_file = fopen(filename, "r")))
        return (FALSE);

    do {
        fgets(line, 1024, in_file);
        if (!feof(in_file)) {
            if (line[0] != '#') {
                strcpy(label, line);

                start = strrchr(label, '\n');
                if (start)
                    *start = '\0';

                start = strrchr(label, '\r');
                if (start)
                    *start = '\0';

                start = strchr(label, '=');
                if (start)
                    *start = '\0';
                else
                    start = label;

                strcpy(value, start + 1);

                if (*label != '\0')
                    if (!strcasecmp(label, "DEVICE")) {
                        if (!DetermineOwnIP(value)) {
                            printf("unknown network device '%s', sorry.\n",
                                   value);
                            fclose(in_file);
                            return (FALSE);
                        }
                        use_own_ip = TRUE;
                    } else if (!strcasecmp(label, "VIEWFILTER")) {
                        filtermethod = atoi(value);
                    } else if (!strcasecmp(label, "DEBUG")) {
                        debug = atoi(value);
                    } else if (!strcasecmp(label, "LOCALIP")) {
                        strncpy(own_ip, value, 254);
                        own_ip[255] = '\0';
                        use_own_ip = TRUE;
                    } else if (!strcasecmp(label, "DEFAULTFILTER")) {
                        strncpy(defaultfilter, value, 254);
                        defaultfilter[255] = '\0';
                    } else if (!strcasecmp(label, "NOTLS")) {
                        notlslist[0] = ',';
                        strncpy(notlslist + 1, value, 1020);
                        notlslist[1020] = '\0';
                        strcat(notlslist, ",");
                    } else if (!strcasecmp(label, "OKAYDIR")) {
                        struct stat s;
                        strcpy(okay_dir, value);
                        strcat(okay_dir, "/");
                        if (stat(okay_dir, &s) < 0) {
                            printf
                                ("please specify a valid dir for the OKAYDIR label.\n");
                            fclose(in_file);
                            return (FALSE);
                        }
                        if (!S_ISDIR(s.st_mode)) {
                            printf
                                ("please specify a valid dir for the OKAYDIR label.\n");
                            fclose(in_file);
                            return (FALSE);
                        }
                        // check if the dir is okay
                        strcat(value, ".pftpdircheck");
                        remove(value);
                        if ((dircheck = fopen(value, "w"))) {
                            fclose(dircheck);
                        } else {
                            printf
                                ("please specify a valid dir with writing-permissions for the OKAYDIR label.\n");
                            fclose(in_file);
                            return (FALSE);
                        }
                        remove(value);
                        use_okay_dir = TRUE;
                    } else if (!strcasecmp(label, "LOCALDIR")) {
                        if (chdir(value) != 0) {
                            printf
                                ("please specify a valid dir for the LOCALDIR label or comment it out\n");
                            return (FALSE);
                        }
                    } else if (!strcasecmp(label, "RNDREFR")) {
                        random_refresh = atoi(value);
                    } else if (!strcasecmp(label, "PASSIVE")) {
                        passive_default = atoi(value);
                    } else if (!strcasecmp(label, "STEALTH")) {
                        stealth_enabled = atoi(value);
                    } else if (!strcasecmp(label, "USELOCAL")) {
                        strcpy(uselocal_label, value);
                    } else if (!strcasecmp(label, "FF")) {
                        if (atoi(value) == 3)
                            ff_enabled = 1;
                    } else {
                        printf("unknown label '%s' in configfile.\n",
                               label);
                        fclose(in_file);
                        return (FALSE);
                    }
            }
        }
    }
    while (!feof(in_file));

    fclose(in_file);

    return (TRUE);
}


int main(int argc, char **argv)
{
    char msg[256], config_file[] = { ".pftpconf" };
    int n;

    pthread_mutex_init(&syscall_lock, NULL);
    pthread_mutex_init(&sigwinch_lock, NULL);

    my_signal(SIGINT, SIG_IGN);
    my_signal(SIGPIPE, SIG_IGN);
    my_signal(SIGWINCH, adjust);
    need_resize=FALSE;
    if (argc > 1) {
        for (n = 1; n < argc; n++)
            if (!strcmp(argv[n], "-na"))
                no_autologin = TRUE;
            else if (!strncmp(argv[n], "-connect=", 9)) {
                sites2login = (char *) malloc(strlen(argv[n]) - 9 + 2);
                strcpy(sites2login, argv[n] + 9);
                strcat(sites2login, ",");
            }
            else if (!strncmp(argv[n], "-section=", 9)) {
                section = (char *) malloc(strlen(argv[n]) - 9 + 1);
                strcpy(section, argv[n] + 9);
            } else {
                printf("pftp BuSTeD EDiTiON %s\n\n", PFTP_VERSION);
                printf("available command line options:\n");
                printf("\t-na\tomit autologin\n");
                printf
                    ("\t-connect=site1,site2,...\tautologin to specified sites\n\n");
                exit(-1);
            }
    }
    getcwd(startcwd, SERVER_WORKINGDIR_SIZE);

    if (!ReadConfig(config_file)) {
        printf("error reading/parsing configfile '%s', bailing out.\n",
               config_file);
        exit(-1);
    }

    if (display->ProbeBookmarkRC() == 1) {
        printf("unknown or invalid bookmark file found, delete it\n");
        exit(-1);
    }

    if (!use_own_ip) {
        printf
            ("you need to specify a network-device in the configfile.\n");
        exit(-1);
    }

    if (!use_okay_dir) {
        printf
            ("you need to specify a dir for the .okay and .error files in the configfile.\n");
        exit(-1);
    }

    unlinklog();
    debuglog("pftp start");

    //CheckIP();
#ifdef TLS
    if (tls_init())
        exit(1);                /* exit on TLS init error */
#endif                          /* TLS */

    pthread_mutex_init(&(localwindowloglock), NULL);
    for (n = 0; n < LOG_LINES; n++) {
        localwindowlog[n] = NULL;
    }
    display = new(CDisplayHandler);

    keyhandler = new(CKeyHandler);

    displayinitdone = 0;
    if (!FireDisplayHandler()) {
        delete(keyhandler);
        delete(display);
        printf(E_DISPLAYHANDLER_FIREUP);
        exit(-1);
    }
    //we will wait for display to init
    n = 0;
    while ((displayinitdone == 0) && (n < 1000)) {
        n++;
        usleep(10000);
    }
    if ((displayinitdone == 1) || (displayinitdone == 0)) {
        delete(keyhandler);
        puts("Display thread was Unable to init...");
        exit(-1);
    }
    sprintf(msg, "pftp BuSTeD EDiTiON %s (c) pSi&HoE initializing...",
            PFTP_VERSION);
    display->PostStatusLine(msg, TRUE);

    if (!keyhandler->Init()) {
        //cannot happen :)
        delete(keyhandler);
        delete(display);
        exit(-1);
    }

    keyhandler->Loop();
    delete(keyhandler);
    delete(display);

    FreeBookmarks();
#ifdef TLS
    tls_cleanup();
#endif                          /* TLS */
}
