#include <curses.h>
#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <pwd.h>
#include "defines.h"
#include "tcp.h"
#include "server.h"
#include "displayhandler.h"
#include "keyhandler.h"

extern	CDisplayHandler	*display;
extern	SERVERLIST	*global_server;
extern	bool		no_autologin, stealth_enabled;
extern	char		own_ip[256];

void FireupLocalFilesys(void);

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
CKeyHandler::CKeyHandler()
{
	this->want_quit = FALSE;
	this->bm_save = FALSE;
	this->want_local = FALSE;
}

CKeyHandler::~CKeyHandler()
{
}

bool CKeyHandler::Init(void)
{
	return(TRUE);
}

void CKeyHandler::PostToDisplay(int msg)
{
	display->PostMessage(msg);
}

void CKeyHandler::PostToDisplay(int msg, int extended)
{
	display->PostMessage(msg, extended);
}

void CKeyHandler::Loop(void)
{
	char	cwd[SERVER_WORKINGDIR_SIZE];
	int	keycode, slept = 0;

	getcwd(cwd, SERVER_WORKINGDIR_SIZE);
	
	display->PostStatusLine("checking environment...", FALSE);
	
	if(no_autologin)
		display->PostStatusLine("omitting auto-login", FALSE);
		
	//CheckIP();
	
	if(stealth_enabled)
		display->PostStatusLine("enabling stealth mode...", TRUE);
	else
		display->PostStatusLine("stealth mode disabled...", TRUE);
			
	if(display->ProbeBookmarkRC()) {
		display->PostStatusLine("found bookmarks", FALSE);
		PostToDisplay(MSG_DISPLAY_PASSWORD);
	}
	else
		PostToDisplay(MSG_DISPLAY_NOBOOKMRK);
		
	do {
		keycode = getch();
		
		switch (keycode) {
		case	KEY_LEFT:	PostToDisplay(MSG_KEY_LEFT);
					break;
		
		case	KEY_RIGHT:	PostToDisplay(MSG_KEY_RIGHT);
					break;
					
		case	KEY_UP:		PostToDisplay(MSG_KEY_UP);
					break;
		
		case	KEY_DOWN:	PostToDisplay(MSG_KEY_DOWN);
					break;
					
		case	KEY_NPAGE:	PostToDisplay(MSG_KEY_PGUP);
					break;
		
		case	KEY_PPAGE:	PostToDisplay(MSG_KEY_PGDN);
					break;
					
		case	13:		PostToDisplay(MSG_KEY_RETURN);
					break;
					
		case	9:		PostToDisplay(MSG_KEY_TAB);
					break;
				
		case	27:		PostToDisplay(MSG_KEY_ESC);
					break;

		case	KEY_BACKSPACE:	PostToDisplay(MSG_KEY_BACKSPACE);
					break;
					
		case	331:		PostToDisplay(MSG_KEY_STATUS_UP);
					break;
		
		case	127:
		case	330:		PostToDisplay(MSG_KEY_STATUS_DOWN);
					break;
					
		case	-1:		if(display->internal_state == DISPLAY_STATE_WELCOME) {
						slept++;
						if(slept > WELCOME_SLEEP)
							PostToDisplay(MSG_KEY_RETURN);
					}
					break;
		
		default:		PostToDisplay(MSG_KEY_EXTENDED, keycode);
					break;
		}

		// handle a few events
		if(this->want_local) {
			this->want_local = FALSE;
			FireupLocalFilesys();
		}
	} while(!this->want_quit);

	display->PostStatusLine("shutting down pftp II...", TRUE);
	
	if(this->bm_save)
		display->SaveBookmarks(cwd);

	this->ShutdownServers();
}

void CKeyHandler::ShutdownServers(void)
{
	SERVERLIST	*sv_temp = global_server, *sv_temp1;
	
	// first kill the threads
	while(sv_temp) {
		pthread_cancel(sv_temp->server->GetThread());
		sv_temp = sv_temp->next;
	}

	// now free memory and nuke object
	sv_temp = global_server;
	while(sv_temp) {
		sv_temp1 = sv_temp;
		sv_temp = sv_temp->next;
		delete(sv_temp1->server);
		delete(sv_temp1);
	}
}

