#include <curses.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <pwd.h>
#include <ctype.h>

#include "defines.h"
#include "tcp.h"
#include "server.h"
#include "displayhandler.h"
#include "keyhandler.h"

extern	CDisplayHandler	*display;
extern	CKeyHandler	*keyhandler;
extern	SERVERLIST	*global_server;
extern	BOOKMARK	*global_bookmark;
extern	bool		no_autologin;
extern	int		mode;
extern	char		own_ip[256];

int old_ftp_mode = MODE_FTP_NOCHAIN, old_fxp_mode = MODE_FXP_NOCHAIN;
bool FilterFilename(char *filename, char *filter);
void FireupRemoteServer(CServer *server);

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

CDisplayHandler::CDisplayHandler()
{
	int	n;
	
	this->speed_left = this->speed_right = this->speed_left_old = this->speed_right_old = 0.0;
		
	for(n = 0; n < STATUS_LOG; n++)
		this->statuslog[n] = NULL;
	
	for(n = 0; n < LOG_LINES; n++)
		this->log[n] = NULL;
		
	for(n = 0; n < JUMP_STACK; n++) {
		this->jump_stack_left[n] = -1;
		this->jump_stack_right[n] = -1;
	}
	this->jump_stack_left_pos = 0;
	this->jump_stack_right_pos = 0;
	
	this->leftwindow_busy = this->rightwindow_busy = NULL;
	
	this->filelist_left_format = this->filelist_right_format = FALSE;
	
	strcpy(this->window_left_cwd, "");
	strcpy(this->window_right_cwd, "");

	this->status_line = 0;	
	this->thread_attr_init = FALSE;
	this->thread_running = FALSE;
	this->default_windows = FALSE;
	this->status_win_size = 5;
	this->message_stack = NULL;	
	this->server_message_stack = NULL;
	this->status_msg_stack = NULL;
	this->internal_state = DISPLAY_STATE_NORMAL;
	this->window_dialog = this->window_notice = this->window_input = NULL;
	this->bm_save = FALSE;
	this->leftwindow_magic = this->rightwindow_magic = -1;
	this->filelist_left = this->filelist_right = NULL;
	this->window_tab = this->window_prefs = NULL;
		
	pthread_mutex_init(&(this->message_lock), NULL);
	pthread_mutex_init(&(this->server_message_lock), NULL);
	pthread_mutex_init(&(this->status_lock), NULL);
	
	// init curses
	initscr();
	intrflush(stdscr, TRUE);
	keypad(stdscr, TRUE);
	nonl();
	noecho();
	cbreak();
	halfdelay(KEYBOARD_DELAY);
}

CDisplayHandler::~CDisplayHandler()
{
	MSG_LIST	*msg_temp, *msg_temp1;
	SERVER_MSG_LIST	*server_msg_temp, *server_msg_temp1;
	STATUS_MSG_LIST	*status_msg_temp, *status_msg_temp1;
	
	// following will never be execute by the displayhandler thread itself
	if(this->thread_running) {
		this->thread_running = FALSE;
		pthread_cancel(this->thread);
	}
		
	if(this->thread_attr_init) {
		this->thread_attr_init = FALSE;
		pthread_attr_destroy(&(this->thread_attr));
	}
	
	if(this->default_windows) {
		this->default_windows = FALSE;

		wattrset(this->window_command, COLOR_PAIR(STYLE_WHITE) | A_NORMAL);
		wbkgdset(this->window_command, ' ' | COLOR_PAIR(STYLE_WHITE));
		werase(this->window_command);
		wbkgdset(this->window_command, ' ');

		wattrset(this->window_status, COLOR_PAIR(STYLE_WHITE) | A_NORMAL);
		wbkgdset(this->window_status, ' ' | COLOR_PAIR(STYLE_WHITE));
		werase(this->window_status);
		wbkgdset(this->window_status, ' ');

		wattrset(this->window_left, COLOR_PAIR(STYLE_WHITE) | A_NORMAL);
		wbkgdset(this->window_left, ' ' | COLOR_PAIR(STYLE_WHITE));
		werase(this->window_left);
		wbkgdset(this->window_left, ' ');

		wattrset(this->window_right, COLOR_PAIR(STYLE_WHITE) | A_NORMAL);
		wbkgdset(this->window_right, ' ' | COLOR_PAIR(STYLE_WHITE));
		werase(this->window_right);
		wbkgdset(this->window_right, ' ');

		wnoutrefresh(this->window_command);
		wnoutrefresh(this->window_status);
		wnoutrefresh(this->window_left);
		wnoutrefresh(this->window_right);
		doupdate();
		
		delwin(this->window_command);
		delwin(this->window_status);
		delwin(this->window_left);
		delwin(this->window_right);
	}
	
	// shutdown curses
	nocbreak();
	echo();
	nl();
	keypad(stdscr, FALSE);	
	endwin();

	pthread_mutex_lock(&(this->message_lock));
	msg_temp = this->message_stack;
	while(msg_temp) {
		msg_temp1 = msg_temp;
		msg_temp = msg_temp->next;
		delete(msg_temp1);
	}
	pthread_mutex_unlock(&(this->message_lock));

	pthread_mutex_lock(&(this->server_message_lock));
	server_msg_temp = this->server_message_stack;
	while(server_msg_temp) {
		server_msg_temp1 = server_msg_temp;
		server_msg_temp = server_msg_temp->next;
		delete(server_msg_temp1);
	}
	pthread_mutex_unlock(&(this->server_message_lock));

	pthread_mutex_lock(&(this->status_lock));
	status_msg_temp = this->status_msg_stack;
	while(status_msg_temp) {
		status_msg_temp1 = status_msg_temp;
		status_msg_temp = status_msg_temp->next;
		delete(status_msg_temp1);
	}
	pthread_mutex_unlock(&(this->status_lock));
}

bool CDisplayHandler::Init(void)
{
	if(pthread_attr_init(&(this->thread_attr)))
		return(FALSE);
	else
		this->thread_attr_init = TRUE;

	if(pthread_attr_setdetachstate(&(this->thread_attr), PTHREAD_CREATE_DETACHED))
		return(FALSE);

	this->InitColors();
	this->DetermineScreenSize();
	this->OpenDefaultWindows();
	this->default_windows = TRUE;
	this->RebuildScreen();
	return(TRUE);
}

void CDisplayHandler::HandleMessage(int msg, int extended)
{
	CServer		*server;
	FILELIST	*filelist;
	int		temp = 0, magic, ret;
	bool		flag;
	
	flag = FALSE;
	
	switch(this->internal_state) {
	case	DISPLAY_STATE_NORMAL:	switch(msg) {
					case	MSG_KEY_UP:		this->UpdateFilelistScroll(FALSE, FALSE);
									this->ResetTimer();
									break;
					
					case	MSG_KEY_DOWN:		this->UpdateFilelistScroll(TRUE, FALSE);
									this->ResetTimer();
									break;
					
					case	MSG_KEY_PGUP:		this->UpdateFilelistPageMove(TRUE);
									this->ResetTimer();
									break;
					
					case	MSG_KEY_PGDN:		this->UpdateFilelistPageMove(FALSE);
									this->ResetTimer();
									break;
					
					case	MSG_KEY_LEFT:		this->ChangeDir(FOR_SERVER_MSG_CWD_UP);
									this->ResetTimer();
									break;
					
					case	MSG_KEY_RIGHT:
					case	MSG_KEY_RETURN:		this->ChangeDir(FOR_SERVER_MSG_CWD);
									this->ResetTimer();
									break;
										
					case	MSG_KEY_TAB:		if(this->window_tab == this->window_left) {
										if(this->filelist_right)
											this->window_tab = this->window_right;
									}
									else {
										if(this->filelist_left)
											this->window_tab = this->window_left;
									}
									this->UpdateTabBar(FALSE);
									break;
									
					case	MSG_KEY_STATUS_UP:	this->ScrollStatusUp();
									break;
					
					case	MSG_KEY_STATUS_DOWN:	this->ScrollStatusDown();
									break;
									
					case	MSG_DISPLAY_PASSWORD:	this->internal_state = DISPLAY_STATE_PASSWORD;
									this->OpenPasswordInput(TRUE);
									break;
					
					case	MSG_DISPLAY_NOBOOKMRK:	this->internal_state = DISPLAY_STATE_NOBKMRK;
									this->bm_save = TRUE;
									this->NoticeNoBookmark();
									break;
									
					case	MSG_KEY_EXTENDED:	switch((char)extended) {
									case	32:	this->FileToggleMark();
											this->ResetTimer();
											break;
									
									case	'K':
									case	'k':	flag = TRUE;
									case	'T':
									case	't':	//CheckIP();
											this->TransferFile(flag);
											this->ResetTimer();
											break;
													
									case	'D':
									case	'd':	this->DeleteFile();
											this->ResetTimer();
											break;
											
									case	'O':
									case	'o':	//CheckIP();
											this->internal_state = DISPLAY_STATE_OPENSITE;
											this->OpenSiteDialog();
											break;

									case	'P':	flag = TRUE;
									case	'p':	if(flag)
												this->PrepDir(FOR_SERVER_MSG_PREP_IN);
											else
												this->PrepDir(FOR_SERVER_MSG_PREP);
											
											this->ResetTimer();
											break;

									case	'I':
									case	'i':	this->WipePrep();
											this->ResetTimer();
											break;
											
									case	'C':
									case	'c':	this->CloseSite();
											break;
											
									case	'S':
									case	's':	this->internal_state = DISPLAY_STATE_SWITCH;
											this->OpenSwitchWindow();
											break;
											
									case	'F':
									case	'f':	if(this->window_tab == this->window_left) {
												this->filelist_left_format = !this->filelist_left_format;
											}
											else {
												this->filelist_right_format = !this->filelist_right_format;
											}
											this->UpdateFilelistNewPosition(this->window_tab);
											this->UpdateTabBar(FALSE);
											this->ResetTimer();
											break;
											
									case	'V':
									case	'v':	this->ViewFile();
											break;
											
									case	'L':
									case	'l':	if((server = TryOpenLog())) {
												this->internal_state = DISPLAY_STATE_LOG;
												this->OpenLog(server);
											}
											else {
												this->internal_state = DISPLAY_STATE_NOTICE;
												this->DialogNotice(NOTICE_NO_LOG, DEFAULT_OKAY);
											}
											break;
																				
									case	'Y':	flag = TRUE;
									case	'y':	if(this->window_tab == this->window_left) {
												if(!flag)
													this->jump_stack_left_pos++;
												else
													this->jump_stack_left_pos = 0;
													
												if(this->jump_stack_left_pos >= JUMP_STACK)
													this->jump_stack_left_pos = 0;
												
												if(this->jump_stack_left[this->jump_stack_left_pos] == -1)
													this->jump_stack_left_pos = 0;
												else
													this->filelist_left_magic = this->jump_stack_left[this->jump_stack_left_pos];
											}
											else {
												if(!flag)
													this->jump_stack_right_pos++;
												else
													this->jump_stack_right_pos = 0;
													
												if(this->jump_stack_right_pos >= JUMP_STACK)
													this->jump_stack_right_pos = 0;
												
												if(this->jump_stack_right[this->jump_stack_right_pos] == -1)
													this->jump_stack_right_pos = 0;
												else
													this->filelist_right_magic = this->jump_stack_right[this->jump_stack_right_pos];
											}
											
											this->UpdateFilelistNewPosition(this->window_tab);
											this->UpdateTabBar(FALSE);
											this->ResetTimer();
											break;
											
									case	'M':
									case	'm':	strcpy(this->input_temp, "");
											this->internal_state_previous = INPUT_DO_MKD;
											this->internal_state = DISPLAY_STATE_INPUT;
											this->DialogInput(DIALOG_ENTERMKD, this->input_temp, INPUT_TEMP_MAX, FALSE);
											break;
											
									case	'W':
									case	'w':	if(this->window_tab == this->window_left)
												strcpy(this->input_temp, this->window_left_cwd);
											else
												strcpy(this->input_temp, this->window_right_cwd);
											
											this->internal_state_previous = INPUT_DO_CWD;
											this->internal_state = DISPLAY_STATE_INPUT;
											this->DialogInput(DIALOG_ENTERCWD, this->input_temp, INPUT_TEMP_MAX, FALSE);
											break;
											
									case	'N':
									case	'n':	if(this->window_tab == this->window_left) {
												filelist = this->filelist_left;
												magic = this->filelist_left_magic;
											}
											else {
												filelist = this->filelist_right;
												magic = this->filelist_right_magic;
											}
											
											filelist = this->GetFilelistEntry(filelist, magic);
											strcpy(this->old_input_temp, filelist->name);
											strcpy(this->input_temp, filelist->name);
											this->internal_state_previous = INPUT_DO_RENAME;
											this->internal_state = DISPLAY_STATE_INPUT;
											this->DialogInput(DIALOG_ENTERRENAME, this->input_temp, INPUT_TEMP_MAX, FALSE);
											break;
											
									case	'E':
									case	'e':	if((server = TryPrefs(&(this->siteopen_bm_realmagic)))) {
												this->internal_state = DISPLAY_STATE_PREFS;
												this->internal_state_previous = DISPLAY_STATE_NORMAL;
												this->FillInfoForPrefs();
												this->OpenPrefsDialog();
												this->siteprefs_server = server;
											}
											else {
												this->internal_state = DISPLAY_STATE_NOTICE;
												this->DialogNotice(NOTICE_NO_PREFS, DEFAULT_OKAY);
											}
											break;

									case	'X':
									case	'x':	if((ret = TrySite(this->window_tab)) == 2) {
												this->internal_state = DISPLAY_STATE_XSITE;
												this->DialogXSite();
											}
											else if(ret == 0) {
												this->internal_state = DISPLAY_STATE_NOTICE;
												this->DialogNotice(NOTICE_NO_XSITE, DEFAULT_OKAY);
											}
											break;
									
									case	'A':
									case	'a':	this->CompareFiles();
											this->ResetTimer();
											break;
											
									case	'G':
									case	'g':	flag = TRUE;
									case	'U':
									case	'u':	this->UtilGame(flag);
											this->ResetTimer();
											break;
											
									case	'R':	flag = TRUE;
									case	'r':	this->RefreshSite(flag);
											this->ResetTimer();
											break;
									
									case	'+':	flag = TRUE;
									case	'-':	this->MarkFiles(flag);
											this->ResetTimer();
											break;
											
									case	'Z':	if((mode == MODE_FTP_CHAIN) || (mode == MODE_FTP_NOCHAIN)) {
												old_ftp_mode = mode;
												mode = old_fxp_mode;
											}
											else {
												old_fxp_mode = mode;
												mode = old_ftp_mode;
											}
											this->UpdateMode();
											break;
											
									case	'z':	if(mode == MODE_FTP_CHAIN)
												mode = MODE_FTP_NOCHAIN;
											else if(mode == MODE_FTP_NOCHAIN)
												mode = MODE_FTP_CHAIN;
											else if(mode == MODE_FXP_CHAIN)
												mode = MODE_FXP_NOCHAIN;
											else
												mode = MODE_FXP_CHAIN;
											
											this->UpdateMode();
											break;
											
									case	'B':
									case	'b':	this->ChangeSorting();
											break;
											
									case	'Q':
									case	'q':	keyhandler->WantQuit(this->bm_save);
											break;
									}
									break;
					}
					break;

	case	DISPLAY_STATE_INPUT:	switch(msg) {
					case	MSG_KEY_EXTENDED:	this->InputAppendChar((char)extended);
									break;
									
					case	MSG_KEY_BACKSPACE:	this->InputBackspace();
									break;
									
					case	MSG_KEY_RETURN:		this->internal_state = DISPLAY_STATE_NORMAL;
									this->CloseInput();
									
									if(strlen(this->input_temp) > 0) {
										switch(this->internal_state_previous) {
										case	INPUT_DO_MKD:		this->PostToServer(FOR_SERVER_MSG_MKD, TRUE, MSG_TYPE_INPUT);
														break;

										case	INPUT_DO_CWD:		this->PostToServer(FOR_SERVER_MSG_CWD, TRUE, MSG_TYPE_INPUT);
														break;

										case	INPUT_DO_RENAME:	this->PostToServer(FOR_SERVER_MSG_RENFROM, TRUE, MSG_TYPE_OLDINPUT);
														this->PostToServer(FOR_SERVER_MSG_RENTO, TRUE, MSG_TYPE_INPUT);
														break;
										}
									}
									break;
					
					case	MSG_KEY_ESC:		this->internal_state = DISPLAY_STATE_NORMAL;
									this->CloseInput();
									break;
					
					}
					break;
							
	case	DISPLAY_STATE_XSITE:	switch(msg) {
					case	MSG_KEY_RIGHT:
					case	MSG_KEY_TAB:
					case	MSG_KEY_DOWN:		this->xsite_buttonstate++;
									if(this->xsite_buttonstate > 3)
										this->xsite_buttonstate = 0;
									this->UpdateXSiteButtons();
									break;

					case	MSG_KEY_LEFT:
					case	MSG_KEY_UP:		this->xsite_buttonstate--;
									if(this->xsite_buttonstate < 0)
										this->xsite_buttonstate = 3;
									this->UpdateXSiteButtons();
									break;

					case	MSG_KEY_EXTENDED:	this->InputAppendChar((char)extended);
									break;
									
					case	MSG_KEY_BACKSPACE:	this->InputBackspace();
									break;
									
					case	MSG_KEY_RETURN:		this->internal_state = DISPLAY_STATE_NORMAL;
									this->CloseSiteInput();
									
									switch(this->xsite_buttonstate) {
									case	0:	if(strlen(this->input_temp) > 0) {
												this->PostToServer(FOR_SERVER_MSG_SITE, TRUE, MSG_TYPE_INPUT);
											}
											break;
									
									default:	this->GetSiteAlias(this->xsite_buttonstate);
											this->PostToServer(FOR_SERVER_MSG_SITE, TRUE, MSG_TYPE_INPUT);
									}
									break;
					
					case	MSG_KEY_ESC:		this->internal_state = DISPLAY_STATE_NORMAL;
									this->CloseSiteInput();
									break;
					
					}
					break;
					
	case	DISPLAY_STATE_VIEW:	switch(msg) {
					case	MSG_KEY_UP:		this->ScrollView(FALSE);
									break;
					
					case	MSG_KEY_DOWN:		this->ScrollView(TRUE);
									break;
					
					case	MSG_KEY_PGDN:		this->PageMoveView(FALSE);
									break;
					
					case	MSG_KEY_PGUP:		this->PageMoveView(TRUE);
									break;
									
					case	MSG_KEY_ESC:		this->internal_state = DISPLAY_STATE_NORMAL;
									this->CloseView();
									break;
					}
					break;
					
	case	DISPLAY_STATE_LOG:	switch(msg) {
					case	MSG_KEY_UP:		this->ScrollLog(TRUE);
									break;
					
					case	MSG_KEY_DOWN:		this->ScrollLog(FALSE);
									break;
					
					case	MSG_KEY_PGDN:		this->PageMoveLog(TRUE);
									break;
					
					case	MSG_KEY_PGUP:		this->PageMoveLog(FALSE);
									break;
									
					case	MSG_KEY_ESC:		this->internal_state = DISPLAY_STATE_NORMAL;
									this->CloseLog();
									break;
					}
					break;
								
	case	DISPLAY_STATE_SWITCH:	switch(msg) {
					case	MSG_KEY_UP:		this->ScrollSwitch(TRUE);
									break;
					
					case	MSG_KEY_DOWN:		this->ScrollSwitch(FALSE);
									break;
					
					case	MSG_KEY_PGUP:		this->PageMoveSwitch(TRUE);
									break;
					
					case	MSG_KEY_PGDN:		this->PageMoveSwitch(FALSE);
									break;
									
					case	MSG_KEY_RETURN:		this->internal_state = DISPLAY_STATE_NORMAL;
									this->CloseSwitchWindow();
									this->PerformSwitch();
									break;
									
					case	MSG_KEY_ESC:		this->internal_state = DISPLAY_STATE_NORMAL;
									this->CloseSwitchWindow();
									break;
					}
					break;
					
	case	DISPLAY_STATE_NOBKMRK:	switch(msg) {
					case	MSG_KEY_ESC:
					case	MSG_KEY_RETURN:		this->internal_state = DISPLAY_STATE_PASSWORD;
									this->CloseNotice();
									this->OpenPasswordInput(TRUE);
									break;
					}
					break;
	
	case	DISPLAY_STATE_NOPASSWD:	switch(msg) {
					case	MSG_KEY_ESC:
					case	MSG_KEY_RETURN:		this->internal_state = DISPLAY_STATE_PASSWORD;
									this->CloseNotice();
									break;
					}
					break;
	
	case	DISPLAY_STATE_BADPASS:	switch(msg) {
					case	MSG_KEY_ESC:
					case	MSG_KEY_RETURN:		keyhandler->WantQuit(FALSE);
									break;
					}
					break;
									
	case	DISPLAY_STATE_PASSWORD:	switch(msg) {
					case	MSG_KEY_EXTENDED:	this->InputAppendChar((char)extended);
									break;
									
					case	MSG_KEY_BACKSPACE:	this->InputBackspace();
									break;
									
					case	MSG_KEY_RETURN:		if(strlen(this->password) < 6) {
										this->internal_state = DISPLAY_STATE_NOPASSWD;
										this->NoticeBadPasswd();
									}
									else {
										this->internal_state = DISPLAY_STATE_NORMAL;
										this->CloseInput();
										strcpy(this->custom_password, this->password);
										if(this->ProbeBookmarkRC()) {
											if(!this->ReadBookmarks()) {
												this->internal_state = DISPLAY_STATE_BADPASS;
												this->NoticeNoMatch();
											}
										}										
										
										if(this->internal_state == DISPLAY_STATE_NORMAL) {
											this->leftwindow_magic = SERVER_MAGIC_START;
											this->window_tab = this->window_left;
											strcpy(this->window_left_label, "local_filesys");
											this->FireupLocalFilesys();
											this->internal_state = DISPLAY_STATE_WELCOME;
											this->AutoLogin();
											this->ShowWelcome();
										}
									}
									break;
					
					case	MSG_KEY_ESC:		this->internal_state = DISPLAY_STATE_NOPASSWD;
									this->NoticeNoPasswd();
									break;
					
					}
					break;
									
	case	DISPLAY_STATE_OS_NOTICE:	switch(msg) {
						case	MSG_KEY_ESC:
						case	MSG_KEY_RETURN:		this->internal_state = DISPLAY_STATE_OPENSITE;
										this->CloseNotice();
										break;
						}
						break;
						
	case	DISPLAY_STATE_NOTICE:	switch(msg) {
					case	MSG_KEY_ESC:
					case	MSG_KEY_RETURN:		this->internal_state = DISPLAY_STATE_NORMAL;
									this->CloseNotice();
									break;
					}
					break;
						
	case	DISPLAY_STATE_OPENSITE:	switch(msg) {
					case	MSG_KEY_UP:		this->ScrollBookmarkSites(TRUE);
									break;
					
					case	MSG_KEY_DOWN:		this->ScrollBookmarkSites(FALSE);
									break;
									
					case	MSG_KEY_PGUP:		this->PageMoveBookmarkSites(FALSE);
									break;
					
					case	MSG_KEY_PGDN:		this->PageMoveBookmarkSites(TRUE);
									break;
									
					case	MSG_KEY_TAB:
					case	MSG_KEY_RIGHT:		this->siteopen_buttonstate++;
									if(this->siteopen_buttonstate > 4)
										this->siteopen_buttonstate = 0;
									
									if((this->siteopen_buttonstate == 0) && !this->filelist_right)
										this->siteopen_buttonstate = 1;
									else if((this->siteopen_buttonstate == 1) && !this->filelist_left)
										this->siteopen_buttonstate = 2;
										
									this->UpdateSiteOpenButtons();
									break;
									
					case	MSG_KEY_LEFT:		this->siteopen_buttonstate--;
									if(this->siteopen_buttonstate < 0)
										this->siteopen_buttonstate = 4;

									if((this->siteopen_buttonstate == 0) && (this->rightwindow_magic == -1))
										this->siteopen_buttonstate = 4;
									else if((this->siteopen_buttonstate == 1) && (this->leftwindow_magic == -1))
										this->siteopen_buttonstate = 0;

									this->UpdateSiteOpenButtons();
									break;

					case	MSG_KEY_RETURN:		switch(this->siteopen_buttonstate) {
									case	0:	if(global_bookmark) {
												this->internal_state = DISPLAY_STATE_NORMAL;
												this->CloseSiteDialog();
												this->FreeWindow(this->window_left);
												this->filelist_left_format = FALSE;
												this->leftwindow_magic = this->FireupServer(this->window_left_label);
											}
											else {
												this->internal_state = DISPLAY_STATE_OS_NOTICE;
												this->DialogNotice(NOTICE_NO_SITE, DEFAULT_OKAY);
											}
											break;
											
									case	1:	if(global_bookmark) {
												this->internal_state = DISPLAY_STATE_NORMAL;
												this->CloseSiteDialog();
												this->FreeWindow(this->window_right);
												this->filelist_right_format = FALSE;
												this->rightwindow_magic = this->FireupServer(this->window_right_label);
											}
											else {
												this->internal_state = DISPLAY_STATE_OS_NOTICE;
												this->DialogNotice(NOTICE_NO_SITE, DEFAULT_OKAY);
											}
											break;
											
									case	2:	this->internal_state = DISPLAY_STATE_PREFS;
											this->internal_state_previous = DISPLAY_STATE_OPENSITE_ADD;
											strcpy(this->alias, "");
											strcpy(this->hostname, "");
											this->port = 21;
											strcpy(this->username, "");
											strcpy(this->password, "");
											strcpy(this->startdir, "/incoming/today");
											strcpy(this->exclude, ".bad");
											strcpy(this->util_dir, "/uploads/utils");
											strcpy(this->game_dir, "/uploads/games");
											strcpy(this->site_who, "who");
											strcpy(this->site_user, "user");
											strcpy(this->site_wkup, "weektop");
											this->refresh_rate = 5;
											this->noop_rate = 60;
											this->use_refresh = this->use_startdir = this->use_exclude = this->use_jump = this->use_track = this->use_chaining = TRUE;
											this->use_noop = this->use_utilgames = this->use_autologin = FALSE;
											this->OpenPrefsDialog();
											break;
											
									case	3:	if(global_bookmark) {
												this->internal_state = DISPLAY_STATE_OPENSITE;
												this->WipeBookmark();
											}
											else {
												this->internal_state = DISPLAY_STATE_OS_NOTICE;
												this->DialogNotice(NOTICE_NO_SITE, DEFAULT_OKAY);
											}
											break;
											
									case	4:	if(global_bookmark) {
												this->internal_state = DISPLAY_STATE_PREFS;
												this->internal_state_previous = DISPLAY_STATE_OPENSITE_MODIFY;
												this->FillInfoForPrefs();
												this->OpenPrefsDialog();
											}
											else {
												this->internal_state = DISPLAY_STATE_OS_NOTICE;
												this->DialogNotice(NOTICE_NO_SITE, DEFAULT_OKAY);
											}
											break;
									}
									break;
									
					case	MSG_KEY_ESC:		this->internal_state = DISPLAY_STATE_NORMAL;
									this->CloseSiteDialog();
									break;
					}
					break;
					
	case	DISPLAY_STATE_PREFS:	switch(msg) {
					case	MSG_KEY_TAB:
					case	MSG_KEY_DOWN:
					case	MSG_KEY_RIGHT:		this->prefsdialog_buttonstate++;
									if(this->prefsdialog_buttonstate > 19)
										this->prefsdialog_buttonstate = 0;
										
									this->UpdatePrefsItems();
									break;
					
					case	MSG_KEY_UP:				
					case	MSG_KEY_LEFT:		this->prefsdialog_buttonstate--;
									if(this->prefsdialog_buttonstate < 0)
										this->prefsdialog_buttonstate = 19;
										
									this->UpdatePrefsItems();
									break;
									
					case	MSG_KEY_EXTENDED:	if(extended == 32) {
										flag = FALSE;
										switch(this->prefsdialog_buttonstate) {
										case	5:	this->use_startdir = !this->use_startdir;
												flag = TRUE;
												break;
											
										case	6:	this->use_exclude = !this->use_exclude;
												flag = TRUE;
												break;
											
										case	7:	this->use_refresh = !this->use_refresh;
												flag = TRUE;
												break;
											
										case	8:	this->use_noop = !this->use_noop;
												flag = TRUE;
												break;
											
										case	9:	this->use_jump = !this->use_jump;
												flag = TRUE;
												break;
											
										case	10:	this->use_track = !this->use_track;
												flag = TRUE;
												break;
											
										case	11:	this->use_autologin = !this->use_autologin;
												flag = TRUE;
												break;

										case	12:	this->use_chaining = !this->use_chaining;
												flag = TRUE;
												break;

										case	13:	this->use_utilgames = !this->use_utilgames;
												flag = TRUE;
												break;
										}
										
										if(flag) {
											this->UpdatePrefsItems();
										}
									}
									else if((extended == 'O') || (extended == 'o')) {
										if(this->internal_state_previous == DISPLAY_STATE_OPENSITE_ADD) {
											// add site
											this->PrefsAddSite();
											this->internal_state = DISPLAY_STATE_OPENSITE;
											this->ClosePrefsDialog();
											this->RedrawBookmarkSites();
											this->bm_save = TRUE;
										}
										else if(this->internal_state_previous == DISPLAY_STATE_OPENSITE_MODIFY) {
											// just modify prefs
											this->PrefsModifySite();
											this->internal_state = DISPLAY_STATE_OPENSITE;
											this->ClosePrefsDialog();
											this->RedrawBookmarkSites();
											this->bm_save = TRUE;
										}
										else {
											// prefs change from main screen
											this->siteopen_bm_realmagic = this->siteprefs_server->GetBMMagic();
											this->PrefsModifySite();
											this->UpdateSitePrefs(this->siteprefs_server);
											this->internal_state = this->internal_state_previous;
											this->ClosePrefsDialog();
											this->bm_save = TRUE;
										}
									}
									else if((extended == 'C') || (extended == 'c')) {
										if(this->internal_state_previous == DISPLAY_STATE_OPENSITE_MODIFY || this->internal_state_previous == DISPLAY_STATE_OPENSITE_ADD) {
											this->internal_state = DISPLAY_STATE_OPENSITE;
											this->ClosePrefsDialog();
										}
										else {
											this->internal_state = this->internal_state_previous;
											this->ClosePrefsDialog();
										}
									}
									break;
					
					case	MSG_KEY_RETURN:		switch(this->prefsdialog_buttonstate) {
									case 0:		this->internal_state = DISPLAY_STATE_PREFSINPUT;
											this->prefs_inputtype = -1;
											this->DialogInput("enter site alias", this->alias, ALIAS_MAX, FALSE);
											break;

									case 1:		this->internal_state = DISPLAY_STATE_PREFSINPUT;
											this->prefs_inputtype = -1;
											this->DialogInput("enter site hostname or IP", this->hostname, HOSTNAME_MAX, FALSE);
											break;

									case 2:		this->internal_state = DISPLAY_STATE_PREFSINPUT;
											this->prefs_inputtype = 0;
											sprintf(this->num_string, "%d", this->port);
											this->DialogInput("enter site port (decimal)", this->num_string, 6, FALSE);
											break;

									case 3:		this->internal_state = DISPLAY_STATE_PREFSINPUT;
											this->prefs_inputtype = -1;
											this->DialogInput("enter username (login)", this->username, USERNAME_MAX, FALSE);
											break;

									case 4:		this->internal_state = DISPLAY_STATE_PREFSINPUT;
											this->prefs_inputtype = -1;
											this->DialogInput("enter password (login)", this->password, PASSWORD_MAX, TRUE);
											break;
											
									case 5:		this->internal_state = DISPLAY_STATE_PREFSINPUT;
											this->prefs_inputtype = -1;
											this->DialogInput("enter starting directory", this->startdir, STARTDIR_MAX, FALSE);
											break;

									case 6:		this->internal_state = DISPLAY_STATE_PREFSINPUT;
											this->prefs_inputtype = -1;
											this->DialogInput("enter patterns to exclude", this->exclude, EXCLUDE_MAX, FALSE);
											break;
											
									case 7:		this->internal_state = DISPLAY_STATE_PREFSINPUT;
											this->prefs_inputtype = 1;
											sprintf(this->num_string, "%d", this->refresh_rate);
											this->DialogInput("enter refresh rate in seconds", this->num_string, 4, FALSE);
											break;

									case 8:		this->internal_state = DISPLAY_STATE_PREFSINPUT;
											this->prefs_inputtype = 2;
											sprintf(this->num_string, "%d", this->noop_rate);
											this->DialogInput("enter NOOP rate in seconds", this->num_string, 4, FALSE);
											break;

									case 13:	this->internal_state = DISPLAY_STATE_PREFSINPUT;
											this->prefs_inputtype = -1;
											this->DialogInput("enter util directory", this->util_dir, GAMEUTILDIR_MAX, FALSE);
											break;
											
									case 14:	this->internal_state = DISPLAY_STATE_PREFSINPUT;
											this->prefs_inputtype = -1;
											this->DialogInput("enter game directory", this->game_dir, GAMEUTILDIR_MAX, FALSE);
											break;
											
									case 15:	this->internal_state = DISPLAY_STATE_PREFSINPUT;
											this->prefs_inputtype = -1;
											this->DialogInput("enter SITE WHO shortcut", this->site_who, SITE_MAX, FALSE);
											break;
											
									case 16:	this->internal_state = DISPLAY_STATE_PREFSINPUT;
											this->prefs_inputtype = -1;
											this->DialogInput("enter SITE USER shortcut", this->site_user, SITE_MAX, FALSE);
											break;
											
									case 17:	this->internal_state = DISPLAY_STATE_PREFSINPUT;
											this->prefs_inputtype = -1;
											this->DialogInput("enter SITE WKUP shortcut", this->site_wkup, SITE_MAX, FALSE);
											break;
											
									case 18:	if(this->internal_state_previous == DISPLAY_STATE_OPENSITE_ADD) {
												// add site
												this->PrefsAddSite();
												this->internal_state = DISPLAY_STATE_OPENSITE;
												this->ClosePrefsDialog();
												this->RedrawBookmarkSites();
												this->bm_save = TRUE;
											}
											else if(this->internal_state_previous == DISPLAY_STATE_OPENSITE_MODIFY) {
												// just modify prefs
												this->PrefsModifySite();
												this->internal_state = DISPLAY_STATE_OPENSITE;
												this->ClosePrefsDialog();
												this->RedrawBookmarkSites();
												this->bm_save = TRUE;
											}
											else {
												// prefs change from main screen
												this->siteopen_bm_realmagic = this->siteprefs_server->GetBMMagic();
												this->PrefsModifySite();
												this->UpdateSitePrefs(this->siteprefs_server);
												this->internal_state = this->internal_state_previous;
												this->ClosePrefsDialog();
												this->bm_save = TRUE;
											}
											break;
									
									case 19:	if(this->internal_state_previous == DISPLAY_STATE_OPENSITE_MODIFY || this->internal_state_previous == DISPLAY_STATE_OPENSITE_ADD) {
												this->internal_state = DISPLAY_STATE_OPENSITE;
												this->ClosePrefsDialog();
											}
											else {
												this->internal_state = this->internal_state_previous;
												this->ClosePrefsDialog();
											}
											break;
									}
									break;
													
					case	MSG_KEY_ESC:		if(this->internal_state_previous == DISPLAY_STATE_OPENSITE_MODIFY || this->internal_state_previous == DISPLAY_STATE_OPENSITE_ADD)
										this->internal_state = DISPLAY_STATE_OPENSITE;
									else
										this->internal_state = this->internal_state_previous;
										
									this->ClosePrefsDialog();
									break;
					}
					break;

	case	DISPLAY_STATE_PREFSINPUT:	switch(msg) {
						case	MSG_KEY_EXTENDED:	this->InputAppendChar((char)extended);
										break;
						
						case	MSG_KEY_BACKSPACE:	this->InputBackspace();
										break;
						
						case	MSG_KEY_RETURN:
						case	MSG_KEY_ESC:		this->CloseInput();

										switch(this->prefs_inputtype) {
										case	0:	temp = atoi(this->num_string);
												if(temp <= 0)
													temp = 21;
												else if(temp > 65535)
													temp -= 65536;
												
												this->port = temp;
												break;
										
										case	1:	temp = atoi(this->num_string);
												if(temp <= 0)
													temp = 5;
												else if(temp > 999)
													temp = 999;
												
												this->refresh_rate = temp;
												break;
										
										case	2:	temp = atoi(this->num_string);
												if(temp <= 0)
													temp = 60;
												else if(temp > 999)
													temp = 999;
												
												this->noop_rate = temp;
												break;
										}
										this->internal_state = DISPLAY_STATE_PREFS;

										this->UpdatePrefsItems();
										break;
						}
						break;

	case	DISPLAY_STATE_WELCOME:		switch(msg) {
						case	MSG_KEY_ESC:
						case	MSG_KEY_RETURN:	this->internal_state = DISPLAY_STATE_NORMAL;
									this->HideWelcome();
									break;
						}
						break;	
	}
}

void CDisplayHandler::NoticeViewFile(char *file)
{
	char	dir[SERVER_WORKINGDIR_SIZE];
	
	global_server->server->ObtainWorkingDir(dir);
	sprintf(this->view_filename, "%s/%s", dir, file);
	this->internal_state = DISPLAY_STATE_VIEW;
	this->OpenView();	
	global_server->server->PostFromDisplay(FOR_SERVER_MSG_REFRESH, "");
}

void CDisplayHandler::ViewFile(void)
{
	SERVERLIST	*sv_temp = global_server;
	FILELIST	*fl_temp;
	int		magic;
	bool		found = FALSE;
	char		dir[SERVER_WORKINGDIR_SIZE];
	
	if(this->window_tab == this->window_left) {
		magic = this->leftwindow_magic;
		fl_temp = this->GetFilelistEntry(this->filelist_left, this->filelist_left_magic);
	}
	else {
		magic = this->rightwindow_magic;
		fl_temp = this->GetFilelistEntry(this->filelist_right, this->filelist_right_magic);
	}
	
	if(!fl_temp->is_dir && strcmp(fl_temp->name, "..")) {		// stupid users tend to try everything to segfault
		while(!found && sv_temp) {
			if(sv_temp->server->GetMagic() == magic)
				found = TRUE;
			else
				sv_temp = sv_temp->next;
		}
	
		if(found) {
			// check if we have to retrieve the file first
			if(sv_temp->server->GetServerType() == SERVER_TYPE_LOCAL) {
				// file is local, open viewer
				this->internal_state = DISPLAY_STATE_VIEW;
				sv_temp->server->ObtainWorkingDir(dir);
				sprintf(this->view_filename, "%s/%s", dir, fl_temp->name);
				this->OpenView();
			}
			else if(!sv_temp->server->IsBusy()) {
				// retrieve file first. server will notice us when the file is on our shell
				sv_temp->server->PostFromDisplay(FOR_SERVER_MSG_VIEWFILE, fl_temp->name);
			}
		}
	}
}

void CDisplayHandler::ChangeSorting(void)
{
	SERVERLIST	*sv_temp = global_server;
	int		magic;
	bool		found = FALSE;

	if(this->window_tab == this->window_left)
		magic = this->leftwindow_magic;
	else
		magic = this->rightwindow_magic;
	
	while(!found && sv_temp) {
		if(sv_temp->server->GetMagic() == magic)
			found = TRUE;
		else
			sv_temp = sv_temp->next;
	}
	
	if(found)
		sv_temp->server->PostFromDisplay(FOR_SERVER_MSG_CHANGE_SORTING, "");
}

void CDisplayHandler::AutoLogin(void)
{
	BOOKMARK	*bm_temp = global_bookmark;
	
	if(!no_autologin) {
		// probe all bookmarks for auto-login flag
		while(bm_temp) {
			if(bm_temp->use_autologin) {
				// we simply open all threads in the left window	
				// a bit of a lame design here, but who really carez on system startup
				this->siteopen_bm_realmagic = bm_temp->magic;
			
				this->FreeWindow(this->window_right);
				this->filelist_right_format = FALSE;
				this->rightwindow_magic = this->FireupServer(this->window_right_label);
			}
			bm_temp = bm_temp->next;
		}
	}
}

void CDisplayHandler::UtilGame(bool use_game)
{
	SERVERLIST	*sv_temp = global_server;
	int		magic;
	bool		found = FALSE;
	
	// determine active site
	if(this->window_tab == this->window_left)
		magic = this->leftwindow_magic;
	else
		magic = this->rightwindow_magic;
	
	while(( (!found && ((mode != MODE_FTP_CHAIN) && (mode != MODE_FXP_CHAIN))) || ((mode == MODE_FTP_CHAIN) || (mode == MODE_FXP_CHAIN)) ) && sv_temp) {
		if((((mode != MODE_FTP_CHAIN) && (mode != MODE_FXP_CHAIN)) && (sv_temp->server->GetMagic() == magic)) || (((mode == MODE_FTP_CHAIN) || (mode == MODE_FXP_CHAIN)) && sv_temp->server->GetChaining() && (sv_temp->server->GetServerType() != SERVER_TYPE_LOCAL))) {
			found = TRUE;
			if(((mode == MODE_FTP_CHAIN) || (mode == MODE_FXP_CHAIN)) || !sv_temp->server->IsBusy()) {
				if(use_game)
					sv_temp->server->PostFromDisplay(FOR_SERVER_MSG_UTILGAME, "G");
				else
					sv_temp->server->PostFromDisplay(FOR_SERVER_MSG_UTILGAME, "U");
			}
		}
		sv_temp = sv_temp->next;
	}
}

void CDisplayHandler::ResetTimer(void)
{
	SERVERLIST	*sv_temp = global_server;
	int		magic;
	bool		found = FALSE;
	
	if(this->window_tab == this->window_left)
		magic = this->leftwindow_magic;
	else
		magic = this->rightwindow_magic;
	
	while(!found && sv_temp) {
		if(sv_temp->server->GetMagic() == magic)
			found = TRUE;
		else
			sv_temp = sv_temp->next;
	}
	
	if(found)
		sv_temp->server->PostFromDisplay(FOR_SERVER_MSG_RESET_TIMER, "");
}

void CDisplayHandler::RefreshSite(bool refresh_all)
{
	SERVERLIST	*sv_temp = global_server;
	int		magic = -1;
	bool		found = FALSE;
	
	if(!refresh_all) {
		// determine active site
		if(this->window_tab == this->window_left)
			magic = this->leftwindow_magic;
		else
			magic = this->rightwindow_magic;
	}

	while(!found && sv_temp) {
		if(!refresh_all) {
			// find server
			if(sv_temp->server->GetMagic() == magic) {
				sv_temp->server->PostFromDisplay(FOR_SERVER_MSG_REFRESH, "");
				found = TRUE;
			}
		}
		else if(sv_temp->server->GetServerType() == SERVER_TYPE_REMOTE)
				sv_temp->server->PostFromDisplay(FOR_SERVER_MSG_REFRESH, "");
			
		sv_temp = sv_temp->next;
	}
}

void CDisplayHandler::CloseSite(void)
{
	SERVERLIST	*sv_temp = global_server;
	int		magic;
	bool		found = FALSE;
		
	if(this->window_tab == this->window_left)
		magic = this->leftwindow_magic;
	else
		magic = this->rightwindow_magic;
	
	if(magic == SERVER_MAGIC_START) {
		this->internal_state = DISPLAY_STATE_NOTICE;
		this->DialogNotice(NOTICE_NO_CLOSE, DEFAULT_OKAY);
	}
	else {
		while(!found && sv_temp) {
			if(sv_temp->server->GetMagic() == magic)
				found = TRUE;
			else
				sv_temp = sv_temp->next;
		}
		
		if(found && !sv_temp->server->IsBusy())
			sv_temp->server->PostFromDisplay(FOR_SERVER_MSG_CLOSE, "");
	}
}

void CDisplayHandler::WipePrep(void)
{
	SERVERLIST	*sv_temp = global_server;
	int		magic;
	bool		found = FALSE;
	
	if(this->window_tab == this->window_left) {
		magic = this->leftwindow_magic;
	}
	else {
		magic = this->rightwindow_magic;
	}

	while(!found && sv_temp) {
		if(sv_temp->server->GetMagic() == magic)
			found = TRUE;
		else
			sv_temp = sv_temp->next;
	}
	
	if(found) {
		if((mode == MODE_FTP_NOCHAIN) || (mode == MODE_FXP_NOCHAIN)) {
			// just undo on the actual server
			if(sv_temp->server->GetServerType() != SERVER_TYPE_LOCAL)
				sv_temp->server->PostFromDisplay(FOR_SERVER_MSG_UNDO, "");
		}
		else {
			// post to all cahined hosts
			sv_temp = global_server;
			while(sv_temp) {
				if(sv_temp->server->GetChaining() && (sv_temp->server->GetServerType() != SERVER_TYPE_LOCAL))
					sv_temp->server->PostFromDisplay(FOR_SERVER_MSG_UNDO, "");
					
				sv_temp = sv_temp->next;
			}
		}
	}
}

void CDisplayHandler::PrepDir(int msg)
{
	SERVERLIST	*sv_save, *sv_temp = global_server;
	FILELIST	*fl_temp;
	int		dest_magic, magic, file_magic;
	char		*start, *cwd, inside_name[512];
	bool		found = FALSE;
	
	if(this->window_tab == this->window_left) {
		magic = this->leftwindow_magic;
		dest_magic = this->rightwindow_magic;
		file_magic = this->filelist_left_magic;
		fl_temp = this->filelist_left;
		cwd = this->window_left_cwd;
	}
	else {
		magic = this->rightwindow_magic;
		dest_magic = this->leftwindow_magic;
		file_magic = this->filelist_right_magic;
		fl_temp = filelist_right;
		cwd = this->window_right_cwd;
	}

	while(!found && sv_temp) {
		if(sv_temp->server->GetMagic() == magic)
			found = TRUE;
		else
			sv_temp = sv_temp->next;
	}
		
	if(found && ((!sv_temp->server->IsBusy() && ((mode != MODE_FTP_CHAIN) && (mode != MODE_FXP_CHAIN))) || ((mode == MODE_FTP_CHAIN) || (mode == MODE_FXP_CHAIN)))) {
		fl_temp = this->GetFilelistEntry(fl_temp, file_magic);
		
		if(fl_temp->is_dir || (msg != FOR_SERVER_MSG_PREP)) {
			// determine last part of dirname for inside-preps
			strcpy(this->temp_string, cwd);
			start = strrchr(this->temp_string, '/');
			if(start)
				strcpy(inside_name, start+1);
			else
				strcpy(inside_name, this->temp_string);		// take full name, since no slash was there (?)
					
			if((mode == MODE_FTP_CHAIN) || (mode == MODE_FXP_CHAIN)) {
				// okay. if we are inside already, do nothing on the source.
				// if we aren't, CD into at the source
				// prep&cd on all chained hosts
				sv_save = sv_temp;
				sv_temp = global_server;
				while(sv_temp) {
					// possible source
					if(msg == FOR_SERVER_MSG_PREP) {
						// see if this is the source, and CD into
						if(sv_temp == sv_save)
							sv_temp->server->PostFromDisplay(FOR_SERVER_MSG_CWD, fl_temp->name);
					}
				
					// now for possible targets (just remote ones can be target in ftp)
					if((sv_temp != sv_save) && (sv_temp->server->GetServerType() == SERVER_TYPE_REMOTE) && (sv_temp->server->GetChaining())) {
						// ok, this one is no local filesys and has chaining on, and is not the source
						if(msg == FOR_SERVER_MSG_PREP)
							sv_temp->server->PostFromDisplay(msg, fl_temp->name);
						else
							sv_temp->server->PostFromDisplay(FOR_SERVER_MSG_PREP, inside_name);
					}
					sv_temp = sv_temp->next;
				}
			}
			else {
				// ftp no chaining 
				// if we are outside, CD in on the source and create prep the dir on the target
				// if we are in, do nothing on src
				// first determine dest server
				sv_save = sv_temp;
				sv_temp = global_server;
				found = FALSE;
				while(!found && sv_temp) {
					if(sv_temp->server->GetMagic() == dest_magic)
						found = TRUE;
					else
						sv_temp = sv_temp->next;
				}
				
				if(found && !sv_temp->server->IsBusy()) {
					if(msg == FOR_SERVER_MSG_PREP) {
						// CD in on src, prep on dest
						sv_save->server->PostFromDisplay(FOR_SERVER_MSG_CWD, fl_temp->name);
						sv_temp->server->PostFromDisplay(msg, fl_temp->name);
					}
					else {
						// prep inside-name on dest only
						sv_temp->server->PostFromDisplay(FOR_SERVER_MSG_PREP, inside_name);
					}
				}
			}
		}
	}
}

void CDisplayHandler::ChangeDir(int msg)
{
	SERVERLIST	*sv_temp1, *sv_temp = global_server;
	FILELIST	*fl_temp;
	int		magic, file_magic;
	bool		found = FALSE;
	
	if(this->window_tab == this->window_left) {
		magic = this->leftwindow_magic;
		file_magic = this->filelist_left_magic;
		fl_temp = this->filelist_left;
	}
	else {
		magic = this->rightwindow_magic;
		file_magic = this->filelist_right_magic;
		fl_temp = filelist_right;
	}

	while(!found && sv_temp) {
		if(sv_temp->server->GetMagic() == magic)
			found = TRUE;
		else
			sv_temp = sv_temp->next;
	}
	
	if(found && ((!sv_temp->server->IsBusy() && ((mode != MODE_FTP_CHAIN) && (mode != MODE_FXP_CHAIN))) || ((mode == MODE_FTP_CHAIN) || (mode == MODE_FXP_CHAIN)))) {
		fl_temp = this->GetFilelistEntry(fl_temp, file_magic);
		
		if(fl_temp->is_dir || (msg == FOR_SERVER_MSG_CWD_UP)) {
			// lets see if the poster is a local one
			if(sv_temp->server->GetServerType() == SERVER_TYPE_LOCAL) {
				// ok, no chaining or whatever.
				if(msg == FOR_SERVER_MSG_CWD_UP)
					sv_temp->server->PostFromDisplay(msg, "");
				else
					sv_temp->server->PostFromDisplay(msg, fl_temp->name);
			}
			else {
				if((mode == MODE_FTP_CHAIN) || (mode == MODE_FXP_CHAIN)) {
					// okay, spread msg to all servers expect local and those who have disabled chaining
					sv_temp1 = sv_temp;
					sv_temp = global_server;
					while(sv_temp) {
						if(((sv_temp->server->GetServerType() == SERVER_TYPE_REMOTE) && (sv_temp->server->GetChaining())) || (sv_temp == sv_temp1)) {
							// ok, this one is no local filesys and has chaining on (or is source)
							// do we have to determine the entry first?
							if(msg == FOR_SERVER_MSG_CWD_UP) {
								sv_temp->server->PostFromDisplay(msg, "");
							}
							else {
								// use name of actual cursor selection
								sv_temp->server->PostFromDisplay(msg, fl_temp->name);
							}
						}
						sv_temp = sv_temp->next;
					}
				}
				else {
					// ftp nochain
					// okay, send the msg to this server only
					if(msg == FOR_SERVER_MSG_CWD_UP) {
						sv_temp->server->PostFromDisplay(msg, "");
					}
					else {
						// use name of actual cursor selection
						sv_temp->server->PostFromDisplay(msg, fl_temp->name);
					}
				}
			}
		}
	}
}

void CDisplayHandler::MarkFiles(bool mark)
{
	FILELIST	*fl_temp;
	int		magic;
	
	if(this->window_tab == this->window_left) {
		fl_temp = this->filelist_left;
		magic = this->jump_stack_left[this->jump_stack_left_pos];
		if(magic == -1)
			magic = 0;
		
		this->filelist_left_magic = magic;
	}
	else {
		fl_temp = this->filelist_right;
		magic = this->jump_stack_right[this->jump_stack_right_pos];
		if(magic == -1)
			magic = 0;
		
		this->filelist_right_magic = magic;
	}
	
	if(fl_temp) {
		while(fl_temp) {
			fl_temp->is_marked = mark;
			fl_temp = fl_temp->next;
		}
		
		this->UpdateFilelistNewPosition(this->window_tab);
		this->UpdateTabBar(FALSE);		
	}
}

bool CDisplayHandler::NamesEqual(char *name1, char *name2)
{
	char	*c1 = name1, *c2 = name2;
	bool	equal = TRUE;
	
	while(equal && *c1) {
		if((*c1 == '.') || (*c1 == '_') || (*c1 == '-')) {
			// special case
			if((*c2 != '.') && (*c2 != '_') && (*c2 != '-'))
				equal = FALSE;
		}
		else {
			if(toupper(*c1) != toupper(*c2))
				equal = FALSE;
		}
		
		c1 += 1;
		c2 += 1;
	}

	return(equal);
}

int CDisplayHandler::DoCompare(FILELIST *src, FILELIST *dest)
{
	FILELIST	*dest_start = dest;
	int		first = -1;
	bool		found;
	
	// mark entries in source, if they aren't in dest (stupid search algo, who carez here)
	while(src) {
		// should we use this entry?
		if(FilterFilename(src->name, NULL)) {
			// we excluded *.diz, *.nfo, *nuke*, .message and .msg
			
			dest = dest_start;
			found = FALSE;
		
			while(!found && dest) {
				// now make an intelligent compare. "-", "." and "_" are equal
				if(this->NamesEqual(src->name, dest->name))
					found = TRUE;
				else
					dest = dest->next;
			}
		
			if(!found) {
				if(first == -1)
					first = src->magic;
			}
		}
		else
			found = TRUE;
			
		src->is_marked = !found;		// mark or demark (!)
		
		src = src->next;
	}

	if(first != -1)
		return(first);
	else
		return(0);		// first entry
}

void CDisplayHandler::CompareFiles(void)
{
	FILELIST	*fl_l, *fl_r;
	
	fl_l = this->filelist_left;
	fl_r = this->filelist_right;

	if(fl_r && fl_l) {
		this->filelist_left_magic = this->DoCompare(fl_l, fl_r);
		this->filelist_right_magic = this->DoCompare(fl_r, fl_l);
		
		// refresh windows
		this->UpdateFilelistNewPosition(this->window_left);
		this->UpdateFilelistNewPosition(this->window_right);
		this->UpdateTabBar(FALSE);
	}
}

void CDisplayHandler::DeleteFile(void)
{
	SERVERLIST	*sv_temp = global_server;
	FILELIST	*fl_temp, *fl_start;
	int		magic, file_magic, done = 0;
	bool		found = FALSE;
		
	if(this->window_tab == this->window_left) {
		magic = this->leftwindow_magic;
		file_magic = this->filelist_left_magic;
		fl_start = this->filelist_left;
	}
	else {
		magic = this->rightwindow_magic;
		file_magic = this->filelist_right_magic;
		fl_start = filelist_right;
	}

	while(!found && sv_temp) {
		if(sv_temp->server->GetMagic() == magic)
			found = TRUE;
		else
			sv_temp = sv_temp->next;
	}
	
	if(found && !sv_temp->server->IsBusy()) {
		// walk through list and see if we have marked entries. otherwise use the actual cursor-position
		fl_temp = fl_start;
		while(fl_temp) {
			if(fl_temp->is_marked) {
				done++;
				if(fl_temp->is_dir)
					sv_temp->server->PostFromDisplay(FOR_SERVER_MSG_DELDIR, fl_temp->name);
				else
					sv_temp->server->PostFromDisplay(FOR_SERVER_MSG_DELFILE, fl_temp->name);
			}
			fl_temp = fl_temp->next;
		}

		if(done == 0) {
			fl_temp = fl_start;
			found = FALSE;
			while(!found && fl_temp) {
				if(fl_temp->magic == file_magic)
					found = TRUE;
				else
					fl_temp = fl_temp->next;
			}
			
			if(found) {
				if(fl_temp->is_dir)
					sv_temp->server->PostFromDisplay(FOR_SERVER_MSG_DELDIR, fl_temp->name); 
				else
					sv_temp->server->PostFromDisplay(FOR_SERVER_MSG_DELFILE, fl_temp->name);
			}
		}
		
		// now issue a refresh
		sv_temp->server->PostFromDisplay(FOR_SERVER_MSG_REFRESH, "");
	}
}

void CDisplayHandler::TransferFile(bool use_ok)
{
	SERVERLIST	*sv_temp = global_server, *sv_src = NULL, *sv_dest = NULL;
	FILELIST	*fl_temp, *fl_start;
	int		dest_magic, src_magic, file_magic, msg, file_msg, dir_msg, refresh_msg = -1;
	bool		found = FALSE, to_src, all_dests = FALSE;
	
	if(this->window_tab == this->window_left) {
		src_magic = this->leftwindow_magic;
		dest_magic = this->rightwindow_magic;
		file_magic = this->filelist_left_magic;
		fl_start = this->filelist_left;
	}
	else {
		src_magic = this->rightwindow_magic;
		dest_magic = this->leftwindow_magic;
		file_magic = this->filelist_right_magic;
		fl_start = filelist_right;
	}
	
	while((!sv_src || !sv_dest) && sv_temp) {
		if(sv_temp->server->GetMagic() == src_magic)
			sv_src = sv_temp;
		else if(sv_temp->server->GetMagic() == dest_magic)
			sv_dest = sv_temp;
			
		sv_temp = sv_temp->next;
	}
	
	// check if we have found our servers
	// this ones for the nex fxp mode...
	if((mode == MODE_FXP_CHAIN) || (mode == MODE_FXP_NOCHAIN)) {

	if(sv_src && sv_dest && !sv_src->server->IsBusy() && !sv_dest->server->IsBusy()) {
		if((sv_src->server->GetServerType() != SERVER_TYPE_LOCAL) && (sv_dest->server->GetServerType() != SERVER_TYPE_LOCAL)) {
			// good. hosts are idle, none of them is local (else do nothing!)
			// walk through the filelist, look for marks
			fl_temp = fl_start;
			found = FALSE;
			while(!found && fl_temp) {
				if(fl_temp->is_marked)
					found = TRUE;
				else
					fl_temp = fl_temp->next;
			}
		
			fl_temp = fl_start;
			while(fl_temp) {
				// search for a marked entry
				if((found && fl_temp->is_marked) || (!found && fl_temp->magic == file_magic)) {
					// we have either a marked entry or we reached the cursor-position
					if(fl_temp->is_dir) {
						sv_src->server->PostFromDisplay(FOR_SERVER_MSG_FXP_DIR_SRC, fl_temp->name, sv_dest->server->GetPID());
						sv_dest->server->PostFromDisplay(FOR_SERVER_MSG_FXP_DIR_DEST, fl_temp->name, sv_src->server->GetPID());
					}
					else {
						if(!use_ok) {
							sv_src->server->PostFromDisplay(FOR_SERVER_MSG_FXP_FILE_SRC, fl_temp->name, sv_dest->server->GetPID());
							sv_dest->server->PostFromDisplay(FOR_SERVER_MSG_FXP_FILE_DEST, fl_temp->name, sv_src->server->GetPID());
						}
						else {
							sv_src->server->PostFromDisplay(FOR_SERVER_MSG_FXP_FILE_SRC, fl_temp->name, sv_dest->server->GetPID());
							sv_dest->server->PostFromDisplay(FOR_SERVER_MSG_FXP_FILE_DEST_AS_OK, fl_temp->name, sv_src->server->GetPID());
						}
					}
				}
				fl_temp = fl_temp->next;
			}
			
			sv_dest->server->PostFromDisplay(FOR_SERVER_MSG_REFRESH, "");
		}
	}

	}
	else	// standard ftp goes here....
	if(sv_src && sv_dest && ( ((mode != MODE_FTP_CHAIN) && !sv_src->server->IsBusy() && !sv_dest->server->IsBusy()) || (mode == MODE_FTP_CHAIN) )) {
		// determine msg to post to src/local
		// FTP_NOCHAIN:	src local:	upload (as_ok) to destination
		//		dest local:	leech to src with flag NO_NOTICE_WHEN_SUCCESSFUL
		//		both remote:	leech (as_ok) to src with flag NOTICE_SINGLE_DEST and magic of dest
		//
		// FTP_CHAIN:	src local:	upload to destinations
		// 		(there is no dest_local)
		//		all remote:	leech to src with flag NOTICE_ALL_DESTS
		//
		
		if(mode == MODE_FTP_NOCHAIN) {
			// no chaining
			if(sv_src->server->GetServerType() == SERVER_TYPE_LOCAL) {
				// src local, dest remote
				if(!use_ok) {
					file_msg = FOR_SERVER_MSG_UPLOAD_NO_WAIT;
					dir_msg = FOR_SERVER_MSG_UPLOAD_DIR_NO_WAIT;
				}
				else {
					file_msg = FOR_SERVER_MSG_UPLOAD_NO_WAIT_AS_OK;
					dir_msg = FOR_SERVER_MSG_UPLOAD_DIR_NO_WAIT_AS_OK;
				}
				
				to_src = FALSE;
			}
			else if(sv_dest->server->GetServerType() == SERVER_TYPE_LOCAL) {
				// src remote, dest local
				file_msg = FOR_SERVER_MSG_LEECH_NO_NOTICE;
				dir_msg = FOR_SERVER_MSG_LEECH_DIR_NO_NOTICE;
				refresh_msg = FOR_SERVER_MSG_REFRESH;
				to_src = TRUE;
			}
			else
			{
				// both remote
				if(!use_ok) {
					file_msg = FOR_SERVER_MSG_LEECH_NOTICE_SINGLE;
					dir_msg = FOR_SERVER_MSG_LEECH_DIR_NOTICE_SINGLE;
				}
				else {
					file_msg = FOR_SERVER_MSG_LEECH_NOTICE_SINGLE_AS_OK;
					dir_msg = FOR_SERVER_MSG_LEECH_DIR_NOTICE_SINGLE_AS_OK;
				}
				
				refresh_msg = FOR_SERVER_MSG_EMIT_REFRESH_SINGLE;
				to_src = TRUE;
			}
		}
		else  {
			// chaining
			if(sv_src->server->GetServerType() == SERVER_TYPE_LOCAL) {
				// src local, dest remote
				file_msg = FOR_SERVER_MSG_UPLOAD_NO_WAIT;
				dir_msg = FOR_SERVER_MSG_UPLOAD_DIR_NO_WAIT;
				to_src = FALSE;
				all_dests = TRUE;
			}
			else
			{
				// all remote
				file_msg = FOR_SERVER_MSG_LEECH_NOTICE_MULTI;
				dir_msg = FOR_SERVER_MSG_LEECH_DIR_NOTICE_MULTI;
				refresh_msg = FOR_SERVER_MSG_EMIT_REFRESH_MULTI;
				to_src = TRUE;
			}
		}
		
		// walk through the filelist, look for marks
		fl_temp = fl_start;
		found = FALSE;
		while(!found && fl_temp) {
			if(fl_temp->is_marked)
				found = TRUE;
			else
				fl_temp = fl_temp->next;
		}
		
		fl_temp = fl_start;
		while(fl_temp) {
			// search for a marked entry
			if((found && fl_temp->is_marked) || (!found && fl_temp->magic == file_magic)) {
				// we have either a marked entry or we reached the cursor-position
				if(fl_temp->is_dir)
					msg = dir_msg;
				else
					msg = file_msg;
				
				if(all_dests) {
					sv_temp = global_server;
					while(sv_temp) {
						if((sv_temp != sv_src) && (sv_temp->server->GetServerType() != SERVER_TYPE_LOCAL)) {
							if(to_src)
								sv_temp->server->PostFromDisplay(msg, fl_temp->name, dest_magic);
							else
								sv_temp->server->PostFromDisplay(msg, fl_temp->name);
						}
						sv_temp = sv_temp->next;
					}
				}
				else {
					if(to_src)
						sv_src->server->PostFromDisplay(msg, fl_temp->name, dest_magic);
					else
						sv_dest->server->PostFromDisplay(msg, fl_temp->name);
				}
			}
			fl_temp = fl_temp->next;
		}
		
		// see if we can post a refresh rite now
		if(!to_src) {
			if(all_dests) {
				sv_temp = global_server;
				while(sv_temp) {
					if((sv_temp != sv_src) && (sv_temp->server->GetServerType() != SERVER_TYPE_LOCAL)) {
						sv_temp->server->PostFromDisplay(FOR_SERVER_MSG_REFRESH, "");
					}
					sv_temp = sv_temp->next;
				}
			}
			else
				sv_dest->server->PostFromDisplay(FOR_SERVER_MSG_REFRESH, "");
		}
		else {
			if(refresh_msg == FOR_SERVER_MSG_EMIT_REFRESH_SINGLE)
				sv_src->server->PostFromDisplay(refresh_msg, "", dest_magic);
			else
				sv_src->server->PostFromDisplay(refresh_msg, "");
		};
	}
}

void CDisplayHandler::UpdateSitePrefs(CServer *server)
{
	BOOKMARK	*bm_temp = global_bookmark;
	bool		found = FALSE;
	
	while(!found && bm_temp) {
		if(bm_temp->magic == this->siteopen_bm_realmagic)
			found = TRUE;
		else
			bm_temp = bm_temp->next;
	}

	if(found)
		server->SetServerPrefs(bm_temp);
}

int CDisplayHandler::TrySite(WINDOW *window)
{
	SERVERLIST	*sv_temp = global_server;
	int		magic;
	bool		found = FALSE;
	
	if(window == this->window_left)
		magic = this->leftwindow_magic;
	else
		magic = this->rightwindow_magic;
	
	while(!found && sv_temp) {
		if(sv_temp->server->GetMagic() == magic)
			found = TRUE;
		else
			sv_temp = sv_temp->next;
	}
	
	if(found) {
		if(!sv_temp->server->IsBusy()) {
			if(sv_temp->server->GetServerType() == SERVER_TYPE_REMOTE)
				return(2);
			else
				return(0);
		}
		else {
			if(sv_temp->server->GetServerType() == SERVER_TYPE_REMOTE)
				return(1);
			else
				return(0);
		}
	}
	
	return(0);
}

void CDisplayHandler::GetSiteAlias(int code)
{
	SERVERLIST	*sv_temp = global_server;
	int		magic;
	bool		found = FALSE;
	
	if(this->window_tab == this->window_left)
		magic = this->leftwindow_magic;
	else
		magic = this->rightwindow_magic;
	
	while(!found && sv_temp) {
		if(sv_temp->server->GetMagic() == magic)
			found = TRUE;
		else
			sv_temp = sv_temp->next;
	}
	
	if(found) {
		if(sv_temp->server->GetServerType() == SERVER_TYPE_REMOTE) {
			switch(code) {
			case	1:
			case	3:	strcpy(this->input_temp, sv_temp->server->GetSiteAlias(code));
					break;

			default:	sprintf(this->input_temp, "%s %s", sv_temp->server->GetSiteAlias(code), sv_temp->server->GetPrefs()->user);
					break;
			}
		}
		else
			strcpy(this->input_temp, "<INTERNAL ERROR>");
	}
	else
		strcpy(this->input_temp, "<INTERNAL ERROR>");
}

CServer *CDisplayHandler::TryPrefs(int *bm_magic)
{
	SERVERLIST	*sv_temp = global_server;
	BOOKMARK	*bm_temp = global_bookmark;
	int		magic;
	bool		found = FALSE;
		
	if(this->window_tab == this->window_left)
		magic = this->leftwindow_magic;
	else
		magic = this->rightwindow_magic;
	
	if(magic == SERVER_MAGIC_START)
		return(NULL);
	else {
		while(!found && sv_temp) {
			if(sv_temp->server->GetMagic() == magic)
				found = TRUE;
			else
				sv_temp = sv_temp->next;
		}
		
		*bm_magic = sv_temp->server->GetBMMagic();
		found = FALSE;
		while(!found && bm_temp) {
			if(bm_temp->magic == *bm_magic)
				found = TRUE;
			else
				bm_temp = bm_temp->next;
		}
		
		if(found)
			return(sv_temp->server);
		else
			return(NULL);
	}
}

CServer *CDisplayHandler::TryOpenLog(void)
{
	SERVERLIST	*sv_temp = global_server;
	int		magic;
	bool		found = FALSE;
		
	if(this->window_tab == this->window_left)
		magic = this->leftwindow_magic;
	else
		magic = this->rightwindow_magic;
	
	if(magic == SERVER_MAGIC_START)
		return(NULL);
	else {
		while(!found && sv_temp) {
			if(sv_temp->server->GetMagic() == magic)
				found = TRUE;
			else
				sv_temp = sv_temp->next;
		}
		
		return(sv_temp->server);
	}
}

void CDisplayHandler::PerformSwitch(void)
{
	BOOKMARK	*bm_temp = global_bookmark;
	SERVERLIST	*sv_temp = global_server;
	char		*label;
	int		magic, bm_magic, n = 0;
	bool		found = FALSE;
		
	while(sv_temp && (n < this->switch_pos)) {
		sv_temp = sv_temp->next;
		n++;
	}

	if(sv_temp) {
		magic = sv_temp->server->GetMagic();
		bm_magic = sv_temp->server->GetBMMagic();
		
		if((this->leftwindow_magic != magic) && (this->rightwindow_magic != magic)) {
			if(this->window_tab == this->window_left) {
				this->leftwindow_magic = sv_temp->server->GetMagic();
				label = this->window_left_label;
			}
			else {
				this->rightwindow_magic = sv_temp->server->GetMagic();
				label = this->window_right_label;
			}
			
			// determine label
			while(!found && bm_temp) { 
				if(bm_temp->magic == bm_magic)
					found = TRUE;
				else
					bm_temp = bm_temp->next;
			}

			if(found)
				strcpy(label, bm_temp->label);
			else
				strcpy(label, "local_filesys");
					
			this->UpdateServerCWD(sv_temp->server, this->window_tab);
			this->UpdateServerFilelist(sv_temp->server, this->window_tab);
			this->FetchBusy(sv_temp->server, this->window_tab);
		}
	}
	// else.. uhm, cant happen
}

void CDisplayHandler::KillServer(CServer *server)
{
	SERVERLIST	*sv_temp1 = NULL, *sv_temp = global_server;
	bool		found = FALSE;
	int		magic;
	WINDOW		*window_to_fill = NULL;
	
	magic = server->GetMagic();
	if(this->leftwindow_magic == magic)
		window_to_fill = this->window_left;
	else if(this->rightwindow_magic == magic)
		window_to_fill = this->window_right;
			
	// first kill the thread
	pthread_cancel(server->GetThread());
	
	// now it's associated object and list-entry
	// no segfault protection, must work!
	while(sv_temp->server != server) {
		sv_temp1 = sv_temp;
		sv_temp = sv_temp->next;
	}
	
	if(sv_temp1) {
		sv_temp1->next = sv_temp->next;
		delete(sv_temp->server);	// free up all allocated resources
		delete(sv_temp);
	}
	else {	// should never happen since local "server" is always open
		global_server = sv_temp->next;
		delete(sv_temp->server);
		delete(sv_temp);
	}
	
	if(window_to_fill) {
		// now let the find-server-for-emtpy-window algorithm take place...
		// simply find a server which isn't in leftwindow_magic or rightwindow_magic
		// if none is available, simply clear it
		// but avoid switching local filesys in front if remote is available!
		sv_temp = global_server->next;

		while(!found && sv_temp) {
			magic = sv_temp->server->GetMagic();
			if((magic != this->leftwindow_magic) && (magic != this->rightwindow_magic))
				found = TRUE;
			else
				sv_temp = sv_temp->next;
		}	

		if(!found) {
			// okiez, none appropriate. see if we can choose the local-filesys
			magic = global_server->server->GetMagic();
			if((magic != this->leftwindow_magic) && (magic != this->rightwindow_magic)) {
				found = TRUE;
				sv_temp = global_server;
			}
		}

		if(found) {
			// welp, do all necessary things to "link" the server to our view
			if(window_to_fill == this->window_left) {
				this->leftwindow_magic = magic;
				strcpy(this->window_left_label, sv_temp->server->GetAlias());
			}
			else {
				this->rightwindow_magic = magic;
				strcpy(this->window_right_label, sv_temp->server->GetAlias());
			}
				
			this->UpdateServerCWD(sv_temp->server, window_to_fill);
			this->UpdateServerFilelist(sv_temp->server, window_to_fill);
			this->FetchBusy(sv_temp->server, window_to_fill);
		}
		else {
			// cleanup occupied view
			this->FreeWindow(window_to_fill);
		}
	}
	// else do nothing since a server bailed out but wasn't in our view
}

void CDisplayHandler::DeMark(WINDOW *window, char *name)
{
	FILELIST	*fl_temp;
	bool		found = FALSE;
	
	if(window == this->window_left)
		fl_temp = this->filelist_left;
	else
		fl_temp = this->filelist_right;
	
	while(!found && fl_temp) {
		if(!strcmp(fl_temp->name, name))
			found = TRUE;
		else
			fl_temp = fl_temp->next;
	}

	if(found) {
		fl_temp->is_marked = FALSE;
		this->UpdateFilelistNewPosition(window);
		this->UpdateTabBar(FALSE);
	}
}

void CDisplayHandler::FreeWindow(WINDOW *window)
{
	FILELIST	*fl_temp, *fl_temp1;
	
	wattrset(window, COLOR_PAIR(STYLE_NORMAL) | A_NORMAL);
	wbkgdset(window, ' ' | COLOR_PAIR(STYLE_NORMAL));
	werase(window);
	wbkgdset(window, ' ');
	wborder(window, 0, 0, 0, 0, 0, 0, 0, 0);
	wnoutrefresh(window);
	doupdate();

	// correct position of tab-bar
	if(window == this->window_left) {
		if(this->filelist_right)
			this->window_tab = this->window_right;

		fl_temp = this->filelist_left;
		this->leftwindow_magic = -1;
		this->filelist_left = NULL;
	}
	else {
		if(this->filelist_left)
			this->window_tab = this->window_left;

		fl_temp = this->filelist_right;
		this->rightwindow_magic = -1;
		this->filelist_right = NULL;
	}
			
	// free filelist to prevent tab-bar switching
	while(fl_temp) {
		fl_temp1 = fl_temp;
		fl_temp = fl_temp->next;
		delete(fl_temp1->name);
		delete(fl_temp1);
	}

	this->UpdateTabBar(FALSE);
}

void CDisplayHandler::LeechOkayNowUpload(int magic, char *file, bool single, bool use_ok)
{
	SERVERLIST	*sv_temp = global_server;
	
	// this one gets called everytime a server successfully started a leech. now we have to notice the destination(s) that a file is available to upload
	// if single = TRUE, post to magic. else to all REMOTE ones except magic
	while(sv_temp) {
		if(single) {
			if(sv_temp->server->GetMagic() == magic) {
				if(!use_ok)
					sv_temp->server->PostFromDisplay(FOR_SERVER_MSG_UPLOAD, file);
				else
					sv_temp->server->PostFromDisplay(FOR_SERVER_MSG_UPLOAD_AS_OK, file);
			}
		}
		else {
			// broadcast, see if the server is chained (NO mode check, because we were in chaining as this was issued)
			if((sv_temp->server->GetMagic() != magic) && (sv_temp->server->GetChaining()) && (sv_temp->server->GetServerType() == SERVER_TYPE_REMOTE))
				sv_temp->server->PostFromDisplay(FOR_SERVER_MSG_UPLOAD, file);		// there is no '.ok' in multi !
		}
		sv_temp = sv_temp->next;
	}
}

void CDisplayHandler::DirOkayNowMKD(int magic, char *dir, bool single)
{
	SERVERLIST	*sv_temp = global_server;
	
	// if single = TRUE, post to magic. else to all REMOTE ones except magic
	while(sv_temp) {
		if(single) {
			if(sv_temp->server->GetMagic() == magic) {
				sv_temp->server->PostFromDisplay(FOR_SERVER_MSG_PREP, dir);
			}
		}
		else {
			// broadcast, see if the server is chained (NO mode check, because we were in chaining as this was issued)
			if((sv_temp->server->GetMagic() != magic) && (sv_temp->server->GetChaining()) && (sv_temp->server->GetServerType() == SERVER_TYPE_REMOTE))
				sv_temp->server->PostFromDisplay(FOR_SERVER_MSG_PREP, dir);
		}
		sv_temp = sv_temp->next;
	}
}

void CDisplayHandler::DirOkayNowCWD(int magic, char *dir, bool single)
{
	SERVERLIST	*sv_temp = global_server;
	
	// if single = TRUE, post to magic. else to all REMOTE ones except magic
	while(sv_temp) {
		if(single) {
			if(sv_temp->server->GetMagic() == magic) {
				sv_temp->server->PostFromDisplay(FOR_SERVER_MSG_CWD, dir);
			}
		}
		else {
			// broadcast, see if the server is chained (NO mode check, because we were in chaining as this was issued)
			if((sv_temp->server->GetMagic() != magic) && (sv_temp->server->GetChaining()) && (sv_temp->server->GetServerType() == SERVER_TYPE_REMOTE))
				sv_temp->server->PostFromDisplay(FOR_SERVER_MSG_CWD, dir);
		}
		sv_temp = sv_temp->next;
	}
}

void CDisplayHandler::TransferOkayNowRefresh(int magic, bool single)
{
	SERVERLIST	*sv_temp = global_server;
	
	// if single = TRUE, post to magic. else to all REMOTE ones except magic
	while(sv_temp) {
		if(single) {
			if((sv_temp->server->GetMagic() == magic) || (sv_temp->server->GetServerType() == SERVER_TYPE_LOCAL)) {
				sv_temp->server->PostFromDisplay(FOR_SERVER_MSG_REFRESH, "");
			}
		}
		else {
			// broadcast, see if the server is chained (NO mode check, because we were in chaining as this was issued)
			if((sv_temp->server->GetMagic() != magic) && (sv_temp->server->GetChaining()))
				sv_temp->server->PostFromDisplay(FOR_SERVER_MSG_REFRESH, "");
		}
		sv_temp = sv_temp->next;
	}
}

void CDisplayHandler::PostUrgent(int msg, int magic, char *param)
{
	SERVERLIST	*sv_temp = global_server;
	bool		found = FALSE;
	
	while(!found && sv_temp) {
		if(sv_temp->server->GetMagic() == magic)
			found = TRUE;
		else
			sv_temp = sv_temp->next;
		 
	}
	
	if(found) {
		sv_temp->server->PostUrgentFromDisplay(msg, param);
	}
	// this one is critical... hmm.. server WAITS for an urgent message :(
}

void CDisplayHandler::HandleServerMessage(int msg, int magic, char *data)
{
	SERVERLIST	*sv_temp = global_server;
	bool		valid = FALSE;
	WINDOW		*window = NULL;
	
	// see if the msg is usable, i.e. if the server still exists
	// no lock on serverlist since we are the only thread which could add/del entries
	while(!valid && sv_temp) {
		if(sv_temp->magic == magic)
			valid = TRUE;
		else	
			sv_temp = sv_temp->next;
	}

	// some msgs dont have a from-server-magic, but another magic... let them fall through
	if(!valid && (msg == SERVER_MSG_NOTICE_UPLOAD_SINGLE || msg == SERVER_MSG_NOTICE_UPLOAD_SINGLE_AS_OK))
		valid = TRUE;
		
	// okiez, handle message
	if(valid) {
		if(magic == this->leftwindow_magic)
			window = this->window_left;
		else if(magic == this->rightwindow_magic)
			window = this->window_right;
			
		if(window) {
			switch(msg) {
			case	SERVER_MSG_BUSY:		this->UpdateServerBusy(window, data);
								break;
			
			case	SERVER_MSG_IDLE:		this->UpdateServerBusy(window, data);
								break;
								
			case	SERVER_MSG_NEW_CWD:		this->UpdateServerCWD(sv_temp->server, window);
								break;
	
			case	SERVER_MSG_NEW_FILELIST:	this->UpdateServerFilelist(sv_temp->server, window);
								break;

			case	SERVER_MSG_DEMARK:		this->DeMark(window, data);
								break;

			case	SERVER_MSG_KILLME:		this->KillServer(sv_temp->server);
								break;


			case	SERVER_MSG_NOTICE_UPLOAD_SINGLE:	this->LeechOkayNowUpload(magic, data, TRUE, FALSE);
									break;

			case	SERVER_MSG_NOTICE_UPLOAD_SINGLE_AS_OK:	this->LeechOkayNowUpload(magic, data, TRUE, TRUE);
									break;
			
			case	SERVER_MSG_NOTICE_UPLOAD_MULTI:		this->LeechOkayNowUpload(magic, data, FALSE, FALSE);
									break;


			case	SERVER_MSG_NOTICE_LEECH_SINGLE_MKD:	this->DirOkayNowMKD(magic, data, TRUE);
									break;
			
			case	SERVER_MSG_NOTICE_LEECH_MULTI_MKD:	this->DirOkayNowMKD(magic, data, FALSE);
									break;
			
			case	SERVER_MSG_NOTICE_LEECH_SINGLE_CWD:	this->DirOkayNowCWD(magic, data, TRUE);
									break;
			
			case	SERVER_MSG_NOTICE_LEECH_MULTI_CWD:	this->DirOkayNowCWD(magic, data, FALSE);
									break;


			case	SERVER_MSG_URGENT_OKAY:			this->PostUrgent(FOR_SERVER_MSG_URGENT_OKAY, magic, data);
									break;

			case	SERVER_MSG_URGENT_ERROR:		this->PostUrgent(FOR_SERVER_MSG_URGENT_ERROR, magic, data);
									break;


			case	SERVER_MSG_EMIT_REFRESH_SINGLE:		this->TransferOkayNowRefresh(magic, TRUE);
									break;
			
			case	SERVER_MSG_EMIT_REFRESH_MULTI:		this->TransferOkayNowRefresh(magic, FALSE);
									break;

			case	SERVER_MSG_NOTICE_VIEW:			this->NoticeViewFile(data);
									break;
			}
		}
		else {
			// non window-related things (don't depend on a window)
			switch(msg) {
			case	SERVER_MSG_KILLME:		this->KillServer(sv_temp->server);
								break;


			case	SERVER_MSG_NOTICE_UPLOAD_SINGLE:	this->LeechOkayNowUpload(magic, data, TRUE, FALSE);
									break;

			case	SERVER_MSG_NOTICE_UPLOAD_SINGLE_AS_OK:	this->LeechOkayNowUpload(magic, data, TRUE, TRUE);
									break;
			
			case	SERVER_MSG_NOTICE_UPLOAD_MULTI:		this->LeechOkayNowUpload(magic, data, FALSE, FALSE);
									break;


			case	SERVER_MSG_NOTICE_LEECH_SINGLE_MKD:	this->DirOkayNowMKD(magic, data, TRUE);
									break;
			
			case	SERVER_MSG_NOTICE_LEECH_MULTI_MKD:	this->DirOkayNowMKD(magic, data, FALSE);
									break;
			
			case	SERVER_MSG_NOTICE_LEECH_SINGLE_CWD:	this->DirOkayNowCWD(magic, data, TRUE);
									break;
			
			case	SERVER_MSG_NOTICE_LEECH_MULTI_CWD:	this->DirOkayNowCWD(magic, data, FALSE);
									break;


			case	SERVER_MSG_URGENT_OKAY:			this->PostUrgent(FOR_SERVER_MSG_URGENT_OKAY, magic, data);
									break;
			
			case	SERVER_MSG_URGENT_ERROR:		this->PostUrgent(FOR_SERVER_MSG_URGENT_ERROR, magic, data);
									break;


			case	SERVER_MSG_EMIT_REFRESH_SINGLE:		this->TransferOkayNowRefresh(magic, TRUE);
									break;
			
			case	SERVER_MSG_EMIT_REFRESH_MULTI:		this->TransferOkayNowRefresh(magic, FALSE);
									break;

			case	SERVER_MSG_NOTICE_VIEW:			this->NoticeViewFile(data);
									break;
			}
		}
	}
}

void CDisplayHandler::Loop(void)
{
	MSG_LIST	*msg_temp;
	SERVER_MSG_LIST	*server_msg_temp, *server_msg_temp1;
	STATUS_MSG_LIST	*status_msg_temp;
	int		msg, extended, magic, speed_wait = 0;
	bool		no_action, force_speed;
	char		*data;
	
	this->thread_running = TRUE;

	do {
		no_action = TRUE;
		
		// look for keyhandler msgs
		pthread_mutex_lock(&(this->message_lock));
		msg_temp = this->message_stack;
		
		if(msg_temp) {
			this->message_stack = msg_temp->next;
			msg = msg_temp->msg;
			extended = msg_temp->extended;
			delete(msg_temp);
			pthread_mutex_unlock(&(this->message_lock));
			this->HandleMessage(msg, extended);
			no_action = FALSE;
		}
		else
			pthread_mutex_unlock(&(this->message_lock));
					
		if((this->internal_state == DISPLAY_STATE_NORMAL) || (this->internal_state == DISPLAY_STATE_WELCOME) || (this->internal_state == DISPLAY_STATE_PASSWORD)) {
			// no windows which clutter up desktop
			
			// look for new status_log lines
			pthread_mutex_lock(&(this->status_lock));
			status_msg_temp = this->status_msg_stack;
		
			if(status_msg_temp) {
				this->status_msg_stack = status_msg_temp->next;
				this->AddStatusLine(status_msg_temp->line, status_msg_temp->highlight);
				delete(status_msg_temp->line);
				delete(status_msg_temp);
				no_action = FALSE;
			}
			pthread_mutex_unlock(&(this->status_lock));

			if(this->internal_state == DISPLAY_STATE_NORMAL) {
				// look for server msgs
				pthread_mutex_lock(&(this->server_message_lock));
				server_msg_temp = this->server_message_stack;
				if(server_msg_temp) {
					this->server_message_stack = server_msg_temp->next;
					msg = server_msg_temp->msg;
					magic = server_msg_temp->magic;
					if(server_msg_temp->data) {
						data = new(char[strlen(server_msg_temp->data) + 1]);
						strcpy(data, server_msg_temp->data);
					}
					else
						data = NULL;
					
					delete(server_msg_temp->data);
					delete(server_msg_temp);
				
					// now remove identical msg's which came from the same server (useless msgs)
					server_msg_temp = this->server_message_stack;
					server_msg_temp1 = NULL;
					while(server_msg_temp) {
						if((server_msg_temp->magic == magic) && (server_msg_temp->msg == msg) && (server_msg_temp->data == NULL)) {
							// remove this one
							if(server_msg_temp1) {
								server_msg_temp1->next = server_msg_temp->next;
								delete(server_msg_temp);
								server_msg_temp = server_msg_temp1->next;
							}
							else {
								this->server_message_stack = server_msg_temp->next;
								delete(server_msg_temp);
								server_msg_temp = this->server_message_stack;
							}
						}
						else {
							server_msg_temp1 = server_msg_temp;
							server_msg_temp = server_msg_temp->next;
						}
					}
				
					pthread_mutex_unlock(&(this->server_message_lock));
					this->HandleServerMessage(msg, magic, data);
					if(data)
						delete(data);
						
					no_action = FALSE;
				}
				else
					pthread_mutex_unlock(&(this->server_message_lock));
			}
		}
				
		if(no_action) {
			speed_wait++;

			if(speed_wait > SPEED_UPDATE_SLEEP) {
				speed_wait = 0;
				force_speed = TRUE;
			}
			else
				force_speed = FALSE;

			// update speed-information
			if(this->leftwindow_busy && (internal_state == DISPLAY_STATE_NORMAL))
				this->UpdateSpeed(TRUE, force_speed);
			if(this->rightwindow_busy && (internal_state == DISPLAY_STATE_NORMAL))
				this->UpdateSpeed(FALSE, force_speed);
				
			usleep(DISPLAY_MSG_SLEEP);
		}
	} while(TRUE);		// thread will be killed by main thread
}

void CDisplayHandler::InitColors(void)
{
	start_color();
	init_pair(STYLE_NORMAL, COLOR_WHITE, COLOR_BLUE);
	init_pair(STYLE_MARKED, COLOR_YELLOW, COLOR_BLUE);
	init_pair(STYLE_INVERSE, COLOR_BLACK, COLOR_CYAN);
	init_pair(STYLE_MARKED_INVERSE, COLOR_YELLOW, COLOR_CYAN);
	init_pair(STYLE_WHITE, COLOR_WHITE, COLOR_BLACK);
	
	if(has_colors())
		this->inverse_mono = 0;
	else
		this->inverse_mono = A_REVERSE;
}

void CDisplayHandler::DetermineScreenSize(void)
{
	this->window_probe = newwin(0, 0, 0, 0);
	getmaxyx(this->window_probe, (this->terminal_max_y), (this->terminal_max_x));
	delwin(this->window_probe);
}

void CDisplayHandler::OpenDefaultWindows(void)
{
	// create default windows based on actual screen-size
	this->window_left_width = this->terminal_max_x / 2;
	this->window_right_width = this->terminal_max_x - this->window_left_width;
	
	this->window_command = newwin(2, this->terminal_max_x, this->terminal_max_y - 2, 0);
	this->window_status = newwin(this->status_win_size, this->terminal_max_x, this->terminal_max_y - this->status_win_size - 2, 0);
	this->window_left = newwin(this->terminal_max_y - this->status_win_size - 2, this->window_left_width, 0, 0);
	this->window_right = newwin(this->terminal_max_y - this->status_win_size - 2, this->window_right_width, 0, this->window_left_width);

	leaveok(window_command, TRUE);
	leaveok(window_status, TRUE);
	leaveok(window_left, TRUE);
	leaveok(window_right, TRUE);

	scrollok(window_left, TRUE);
	scrollok(window_right, TRUE);
}

void CDisplayHandler::DrawCommandKeys(char *command, int y)
{
	char	*c = command;
	int	x = 0;
	
	while(*c) {
		if(*c != '.')
			mvwaddch(this->window_command, y, x, (unsigned long int)(*c));
		
		c += 1;
		x++;
	}
}

void CDisplayHandler::RebuildScreen(void)
{
	// command-lines on bottom of screen
	wattrset(this->window_command, COLOR_PAIR(STYLE_WHITE) | A_NORMAL);
	wbkgdset(this->window_command, ' ' | COLOR_PAIR(STYLE_WHITE));
	werase(this->window_command);
	wbkgdset(this->window_command, ' ');
		
	wattrset(this->window_command, COLOR_PAIR(STYLE_INVERSE) | inverse_mono);
	mvwaddnstr(window_command, 0, 0, CONTROL_PANEL_1, this->terminal_max_x);
	mvwaddnstr(window_command, 1, 0, CONTROL_PANEL_2, this->terminal_max_x);

	wattrset(this->window_command, COLOR_PAIR(STYLE_WHITE) | A_NORMAL);
	this->DrawCommandKeys(CONTROL_PANEL_1_KEYS, 0);
	this->DrawCommandKeys(CONTROL_PANEL_2_KEYS, 1);
	wnoutrefresh(window_command);

	// status window
	wattrset(this->window_status, COLOR_PAIR(STYLE_NORMAL) | A_NORMAL);
	wbkgdset(this->window_status, ' ' | COLOR_PAIR(STYLE_NORMAL));
	werase(this->window_status);
	wbkgdset(this->window_status, ' ');
	wnoutrefresh(window_status);

	// the 2 filelister
	wattrset(this->window_left, COLOR_PAIR(STYLE_NORMAL) | A_NORMAL);
	wbkgdset(this->window_left, ' ' | COLOR_PAIR(STYLE_NORMAL));
	werase(this->window_left);
	wbkgdset(this->window_left, ' ');
	wborder(this->window_left, 0, 0, 0, 0, 0, 0, 0, 0);
	wnoutrefresh(window_left);
	
	wattrset(this->window_right, COLOR_PAIR(STYLE_NORMAL) | A_NORMAL);
	wbkgdset(this->window_right, ' ' | COLOR_PAIR(STYLE_NORMAL));
	werase(this->window_right);
	wbkgdset(this->window_right, ' ');
	wborder(this->window_right, 0, 0, 0, 0, 0, 0, 0, 0);
	wnoutrefresh(window_right);
	
	doupdate();
}

void CDisplayHandler::PostMessage(int msg)
{
	this->PostMessage(msg, 0);
}

void CDisplayHandler::PostMessage(int msg, int extended)
{
	MSG_LIST	*msg_temp, *msg_new;
	
	pthread_mutex_lock(&(this->message_lock));

	msg_temp = this->message_stack;
	msg_new = new(MSG_LIST);
	msg_new->next = NULL;
	
	if(msg_temp) {
		while(msg_temp->next)
			msg_temp = msg_temp->next;
		
		msg_temp->next = msg_new;
	}
	else
		this->message_stack = msg_new;

	msg_new->msg = msg;
	msg_new->extended = extended;
	
	pthread_mutex_unlock(&(this->message_lock));
}

void CDisplayHandler::PostMessageFromServer(int msg, int magic, char *data)
{
	SERVER_MSG_LIST	*msg_temp, *msg_new;
	
	pthread_mutex_lock(&(this->server_message_lock));

	msg_temp = this->server_message_stack;
	msg_new = new(SERVER_MSG_LIST);
	msg_new->next = NULL;
	
	if(msg_temp) {
		while(msg_temp->next)
			msg_temp = msg_temp->next;
		
		msg_temp->next = msg_new;
	}
	else
		this->server_message_stack = msg_new;

	msg_new->msg = msg;
	msg_new->magic = magic;
	if(data) {
		msg_new->data = new(char[strlen(data) + 1]);
		strcpy(msg_new->data, data);
	}
	else
		msg_new->data = NULL;
	
	pthread_mutex_unlock(&(this->server_message_lock));
}

void CDisplayHandler::PostStatusLine(char *line, bool highlight)
{
	STATUS_MSG_LIST	*msg_temp, *msg_new;
	
	pthread_mutex_lock(&(this->status_lock));

	msg_temp = this->status_msg_stack;
	msg_new = new(STATUS_MSG_LIST);
	msg_new->next = NULL;
	msg_new->line = new(char[strlen(line) + 1]);
	
	if(msg_temp) {
		while(msg_temp->next)
			msg_temp = msg_temp->next;
		
		msg_temp->next = msg_new;
	}
	else
		this->status_msg_stack = msg_new;

	strcpy(msg_new->line, line);
	msg_new->highlight = highlight;
	
	pthread_mutex_unlock(&(this->status_lock));
}

void CDisplayHandler::FireupLocalFilesys(void)
{
	// let the main task create thread for local filesys-window
	keyhandler->WantFireupLocal();

	while(!global_server)
		usleep(200000);		// we need to wait until the server got registered!
}

int CDisplayHandler::FireupServer(char *label)
{
	CServer		*new_server;
	SERVERLIST	*new_sv, *sv_temp = global_server;
	BOOKMARK	*bm_temp = global_bookmark;
	bool		found = FALSE;
	
	while(!found && bm_temp) {
		if(bm_temp->magic == this->siteopen_bm_realmagic)
			found = TRUE;
		else
			bm_temp = bm_temp->next;
	}
	
	// no check if valid !
	
	new_sv = new(SERVERLIST);
	new_sv->next = NULL;
	
	new_server = new(CServer);
	new_server->SetServerType(SERVER_TYPE_REMOTE);
	new_server->SetBMMagic(bm_temp->magic);
	new_server->SetServerPrefs(bm_temp);
	new_sv->server = new_server;
	
	strcpy(label, bm_temp->label);
	
	// local filesys exists atleast !
	while(sv_temp->next)
		sv_temp = sv_temp->next;
		
	sv_temp->next = new_sv;
	new_sv->magic = sv_temp->magic + 1;

	new_server->SetMagic(new_sv->magic);

	// we create the new thread
	FireupRemoteServer(new_server);

	return(new_sv->magic);
}

void CDisplayHandler::PostToServer(int msg, bool mustbe_dir, int type)
{
	SERVERLIST	*sv_temp = global_server;		// will just be modified by this thead
	FILELIST	*filelist, *file_entry;
	int		magic, entry_magic;
	bool		found = FALSE;
		
	if(this->window_tab == this->window_left) {
		magic = this->leftwindow_magic;
		filelist = this->filelist_left;
		entry_magic = this->filelist_left_magic;
	}
	else {
		magic = this->rightwindow_magic;
		filelist = this->filelist_right;
		entry_magic = this->filelist_right_magic;
	}
	
	// see if it's a valid magic for an existing server thread
	while(!found && sv_temp) {
		if(sv_temp->magic == magic)
			found = TRUE;
		else
			sv_temp = sv_temp->next;
	}

	if(found) {
		switch(type) {
		case	MSG_TYPE_STD:	file_entry = this->GetFilelistEntry(filelist, entry_magic);
					if(mustbe_dir) {
						if(file_entry->is_dir)
							sv_temp->server->PostFromDisplay(msg, file_entry->name);
					}
					else
						sv_temp->server->PostFromDisplay(msg, file_entry->name);
					
					break;
		
		case	MSG_TYPE_INPUT:		sv_temp->server->PostFromDisplay(msg, this->input_temp);
						break;

		case	MSG_TYPE_OLDINPUT:	sv_temp->server->PostFromDisplay(msg, this->old_input_temp);
						break;
		}
	}
	// else drop the message (?)
}
