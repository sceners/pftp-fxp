#include <stdio.h>
#include <stdlib.h>
#include <curses.h>
#include <pthread.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>
#include "defines.h"
#include "tcp.h"
#include "server.h"
#include "displayhandler.h"
#include "keyhandler.h"

CDisplayHandler	*display = NULL;
CKeyHandler	*keyhandler;
BOOKMARK	*global_bookmark = NULL;
SERVERLIST	*global_server = NULL;
int		bm_magic_max = -1, mode = MODE_FTP_NOCHAIN, random_refresh = 0;
char		okay_dir[256], uselocal_label[256];
bool		use_own_ip = FALSE, use_okay_dir = FALSE, no_autologin = FALSE, stealth_enabled = FALSE, ff_enabled = 0;
pthread_mutex_t	syscall_lock;

bool DetermineOwnIP(char *device);

extern	 char own_ip[256];

/*inline bool CheckIP(void)
{
	char		*c, *enc, user_crypt[] = CHECK_IDENT, user[10];
	uid_t		uid = getuid();
	struct passwd	*pwd = getpwuid(uid);
	
	// demangle username (foolish)
	c = user_crypt;
	enc = user + strlen(c) - 1;
	*(enc+1) = '\0';
	
	do {
		*enc = *c - 1;
		
		c += 1;
		enc -= 1;
	} while(*c);

	if(strcmp(pwd->pw_name, user))
		goto bail_out;
	
	// ip-check (foolish again)
	c = own_ip;
	
	if(atoi(c) != CHECK_HOST1)
		goto bail_out;
	
	c = strchr(c, '.') + 1;
	if(atoi(c) != CHECK_HOST2)
		goto bail_out;
	
	c = strchr(c, '.') + 1;
	if(atoi(c) != CHECK_HOST3)
		goto bail_out;
	
	c = strchr(c, '.') + 1;
	if(atoi(c) != CHECK_HOST4)
		goto bail_out;
	
	return(TRUE);

bail_out:
	if(display)
		delete(display);
	
	printf("You aren't authorized to use this software.\n");
	exit(-1);	
}
*/

bool FilterFilename(char *filename, char *filter)
{
	char	*end, *start, *pattern, fixed[] = {".diz .nfo"};
	bool	pat_fault = FALSE;
	int	len, fl_len;
	
	if(filter) {
		pattern = new(char[strlen(fixed) + strlen(filter) + 1]);
		sprintf(pattern, "%s %s", fixed, filter);
	}
	else {
		pattern = new(char[strlen(fixed) + 1]);
		strcpy(pattern, fixed);
	}
	
	// ignore nuked ones
	if(strstr(filename, "NUKE") || strstr(filename, "nuke")) {
		delete(pattern);
		return(FALSE);
	}
		
	// ignore .msg and .message
	if(!strcasecmp(filename, ".msg") || !strcasecmp(filename, ".message") || !strcasecmp(filename, ".message.tmp")) {
		delete(pattern);
		return(FALSE);
	}
		
	// ignore patterns
	start = pattern;
	fl_len = strlen(filename);
	do {
		end = strchr(start, ' ');
		if(end)
			*end = '\0';
		
		len = strlen(start);

		if(!strcasecmp(filename + fl_len - len, start))
			pat_fault = TRUE;
			
		if(end) {
			start = end + 1;
			*end = ' ';
		}
		else
			start = NULL;
						
	} while(!pat_fault && start);
	
	delete(pattern);
	return(!pat_fault);
}

void *DetachDisplayHandler(void *dummy)
{
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
	display->Loop();
	// this should be never reached since the displayhandler-thread will be killed by main thread
	return(dummy);
}

bool FireDisplayHandler(void)
{
	// since pthread cant start a thread on a class member, we do it this way
	return(!pthread_create(&(display->thread), &(display->thread_attr), DetachDisplayHandler, NULL));
}

void *DetachServer(void *th_arg)
{
	CServer	*server = (CServer *)th_arg;
	
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
	server->Run();
	// this should be never reached since we kill the thread
	return(NULL);
}

void FireupLocalFilesys(void)
{
	BOOKMARK	bm;
	CServer		*new_server;
	SERVERLIST	*new_sv, *sv_temp = global_server;

	// let's create a special server-object which accesses local files
	new_sv = new(SERVERLIST);
	new_sv->next = NULL;
	
	new_server = new(CServer);
	new_server->SetServerType(SERVER_TYPE_LOCAL);
	new_server->SetBMMagic(-1);
	new_sv->server = new_server;
	
	bm.label = new(char[strlen("local_filesys") + 1]);
	strcpy(bm.label, "local_filesys");
	bm.user = bm.host = bm.pass = bm.startdir = bm.exclude = bm.util_dir = bm.game_dir = bm.site_who = bm.site_user = bm.site_wkup = bm.label;
	bm.use_track = bm.use_jump = bm.use_noop = bm.use_refresh = bm.use_chaining = bm.use_utilgames = FALSE;
	new_server->SetServerPrefs(&bm);
	
	delete(bm.label);
	
	if(!sv_temp) {
		global_server = new_sv;
		new_sv->magic = SERVER_MAGIC_START;
	}
	else {
		while(sv_temp->next)
			sv_temp = sv_temp->next;
		
		sv_temp->next = new_sv;
		new_sv->magic = sv_temp->magic + 1;
	}

	new_server->SetMagic(new_sv->magic);
		
	// we use global display-attr, keep in mind!
	pthread_create(new_server->GetThreadAddress(), &(display->thread_attr), DetachServer, (void *)new_server);
}

void FireupRemoteServer(CServer *new_server)
{
	pthread_create(new_server->GetThreadAddress(), &(display->thread_attr), DetachServer, (void *)new_server);
}

void FreeBookmarks(void)
{
	BOOKMARK	*bm_temp1, *bm_temp = global_bookmark;
	
	while(bm_temp) {
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
	FILE	*in_file, *dircheck;
	char	line[256], label[32], value[256], *start;
	
	strcpy(uselocal_label, "");
	
	if(!(in_file = fopen(filename, "r")))
		return(FALSE);
		
	do {
		fgets(line, 256, in_file);
		if(!feof(in_file)) {
			if(line[0] != '#') {
				strcpy(label, line);
				start = strchr(label, '=');
				if(start)
					*start = 0;
				else
					start = label;
				
				strcpy(value, start + 1);
				
				start = strrchr(value, '\n');
				if(start)
					*start = '\0';
				
				start = strrchr(value, '\r');
				if(start)
					*start = '\0';
					
				if(!strcasecmp(label, "DEVICE")) {
					if(!DetermineOwnIP(value)) {
						printf("unknown network device '%s', sorry.\n", value);
						fclose(in_file);
						return(FALSE);
					}
					use_own_ip = TRUE;
				}
				else if(!strcasecmp(label, "OKAYDIR")) {
					strcpy(okay_dir, value);
					// check if the dir is okay
					strcat(value, ".pftpdircheck");
					remove(value);
					if((dircheck = fopen(value, "w"))) {
						fclose(dircheck);
					}
					else {
						printf("please specify a valid dir with writing-permissions for the OKAYDIR label.\n");
						fclose(in_file);
						return(FALSE);
					}
					
					use_okay_dir = TRUE;
				}
				else if(!strcasecmp(label, "RNDREFR")) {
					random_refresh = atoi(value);
				}
				else if(!strcasecmp(label, "STEALTH")) {
					stealth_enabled = atoi(value);
				}
				else if(!strcasecmp(label, "USELOCAL")) {
					strcpy(uselocal_label, value);
				}
				else if(!strcasecmp(label, "FF")) {
					if(atoi(value) == 3)
						ff_enabled = 1;
				}
				else {
					printf("unknown label '%s' in configfile.\n", label);
					fclose(in_file);
					return(FALSE);
				}
			}
		}
	} while(!feof(in_file));
	
	fclose(in_file);

	return(TRUE);
}

int main(int argc, char **argv)
{
	char	msg[256], config_file[] = {".pftpconf"};
	
	signal(SIGPIPE, SIG_IGN);
	
	pthread_mutex_init(&syscall_lock, NULL);
	
	if(argc > 1) {
		if(strcmp(argv[1], "-na") || (argc > 2)) {
			printf("pftp II TSUNAMi EDiTiON %s\n\n", PFTP_VERSION);
			printf("available command line options:\n");
			printf("\t-na\tomit auto-login\n\n");
			exit(-1);
		}
		
		no_autologin = TRUE;
	}
	
	if(!ReadConfig(config_file)) {
		printf("error reading/parsing configfile '%s', bailing out.\n", config_file);
		exit(-1);
	}
	
	if(!use_own_ip) {
		printf("you need to specify a network-device in the configfile.\n");
		exit(-1);
	}
	
	if(!use_okay_dir) {
		printf("you need to specify a dir for the .okay and .error files in the configfile.\n");
		exit(-1);
	}
	
	//CheckIP();

	
	display = new(CDisplayHandler);
	
	keyhandler = new(CKeyHandler);
	
	if(!display->Init()) {
		delete(keyhandler);
		delete(display);
		printf(E_DISPLAYHANDLER_INIT);
		exit(-1);
	}
	
	if(!FireDisplayHandler()) {
		delete(keyhandler);
		delete(display);
		printf(E_DISPLAYHANDLER_FIREUP);
		exit(-1);
	}
			
	sprintf(msg, "pftp II TSUNAMi EDiTiON %s (c) pSi initializing...", PFTP_VERSION);
	display->PostStatusLine(msg, TRUE);
	
	if(!keyhandler->Init()) {
		delete(keyhandler);
		delete(display);
		exit(-1);
	}
	
	keyhandler->Loop();
	delete(keyhandler);
	delete(display);

	FreeBookmarks();
}
