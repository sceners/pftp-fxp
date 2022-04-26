#include <stdio.h>
#include <curses.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <dirent.h>
#include <malloc.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <fcntl.h>
#include <pwd.h>
#include "defines.h"
#include "tcp.h"
#include "server.h"
#include "displayhandler.h"

#include <stdio.h>
#include <unistd.h>

bool FilterFilename(char *filename, char *filter);
 
extern	CDisplayHandler	*display;
extern	SERVERLIST	*global_server;
extern	pthread_mutex_t	syscall_lock;
extern	char		okay_dir[256], uselocal_label[256];
extern	int		random_refresh;
extern	bool		stealth_enabled, ff_enabled;

CServer::CServer()
{		
	pthread_mutex_init(&(this->cwd_lock), NULL);
	pthread_mutex_init(&(this->busy_lock), NULL);
	pthread_mutex_init(&(this->filelist_lock), NULL);
	pthread_mutex_init(&(this->displaymsg_lock), NULL);

	this->pid = (int)getpid();

	this->actual_filelist = this->internal_filelist = NULL;
	this->display_msg_stack = NULL;
	this->param = NULL;
	this->busy = NULL;

	this->urgent = FALSE;
	this->is_busy = FALSE;
	this->alpha_sort = TRUE;
	this->have_undo = FALSE;
			
	strcpy(this->working_dir, "<unknown>");
	
	prefs.label = NULL;
	prefs.host = NULL;
	prefs.user = NULL;
	prefs.pass = NULL;
	prefs.startdir = NULL;
	prefs.exclude = NULL;
	prefs.util_dir = NULL;
	prefs.game_dir = NULL;
	prefs.site_who = NULL;
	prefs.site_user = NULL;
	prefs.site_wkup = NULL;
	
	this->use_stealth = stealth_enabled;
	this->use_local = FALSE;
	
	this->noop_slept = this->refresh_slept = 0;
	this->error = E_NO_ERROR;
	this->dir_cache = NULL;
}

CServer::~CServer()
{
	FILELIST	*fl_temp, *fl_temp1;
	ACTION_MSG_LIST	*ac_temp, *ac_temp1;
	DIRCACHE	*dc_temp, *dc_temp1;
	CACHELIST	*cl_temp, *cl_temp1;
	
	//pthread_mutex_lock(&(this->filelist_lock));
	fl_temp = this->actual_filelist;
	while(fl_temp) {
		fl_temp1 = fl_temp;
		fl_temp = fl_temp->next;
		delete(fl_temp1->name);
		delete(fl_temp1);
	}
	
	//pthread_mutex_unlock(&(this->filelist_lock));

	//pthread_mutex_lock(&(this->displaymsg_lock));
	ac_temp = this->display_msg_stack;
	while(ac_temp) {
		ac_temp1 = ac_temp;
		ac_temp = ac_temp->next;
		delete(ac_temp1->param);
		delete(ac_temp1);
	}
	
	//pthread_mutex_unlock(&(this->displaymsg_lock));

	if(prefs.label) {
		delete(prefs.label);
		delete(prefs.host);
		delete(prefs.user);
		delete(prefs.pass);
		delete(prefs.startdir);
		delete(prefs.exclude);
		delete(prefs.util_dir);
		delete(prefs.game_dir);
		delete(prefs.site_who);
		delete(prefs.site_user);
		delete(prefs.site_wkup);
	}

	dc_temp = this->dir_cache;
	while(dc_temp) {
		dc_temp1 = dc_temp;
		dc_temp = dc_temp->next;
		
		// free associated cachelist
		cl_temp = dc_temp1->cachelist;
		while(cl_temp) {
			cl_temp1 = cl_temp;
			cl_temp = cl_temp->next;
			delete(cl_temp1->name);
			delete(cl_temp1);
		}
		
		delete(dc_temp1->dirname);
		delete(dc_temp1);
	}
	
	this->tcp.CloseControl();
}

char *CServer::GetSiteAlias(int code)
{
	switch(code) {
	case	1:	return(this->prefs.site_who);
			break;
	
	case	2:	return(this->prefs.site_user);
			break;
	
	default:	return(this->prefs.site_wkup);
			break;
	}
}

char *CServer::GetFilter(void)
{
	if(this->prefs.use_exclude)
		return(this->prefs.exclude);
	else
		return(NULL);
}

void CServer::HandleMessage(int msg, char *param, int magic)
{
	switch(msg) {
	case	FOR_SERVER_MSG_RESET_TIMER:	this->refresh_slept = 0;
						break;
						
	case	FOR_SERVER_MSG_REFRESH:	if(this->server_type == SERVER_TYPE_LOCAL) {
						this->LocalGetDirlist();
					}
					else {
						if(!this->RefreshFiles())
							this->EvalError();
					}
					break;
					
	case	FOR_SERVER_MSG_UTILGAME:	// just remote ones get that
						if(this->prefs.use_utilgames) {
							if(!strcmp(param, "U")) {
								if(this->ChangeWorkingDir(this->prefs.util_dir)) {
									if(!this->GetWorkingDir())
										this->EvalError();
									if(!this->RefreshFiles())
										this->EvalError();
								}
							}
							else {
								if(this->ChangeWorkingDir(this->prefs.game_dir)) {
									if(!this->GetWorkingDir())
										this->EvalError();
									if(!this->RefreshFiles())
										this->EvalError();
								}
							}
						}
						// else drop
						break;
						
	case	FOR_SERVER_MSG_CWD:	if(this->server_type == SERVER_TYPE_LOCAL) {
						if(this->LocalChangeWorkingDir(param)) {
							this->LocalGetWorkingDir();
							this->LocalGetDirlist();
						}
					}
					else {
						if(this->ChangeWorkingDir(param)) {
							if(!this->GetWorkingDir())
								this->EvalError();
							
							if(!this->RefreshFiles())
								this->EvalError();
						}
					}
					break;

	case	FOR_SERVER_MSG_CWD_UP:	if(this->param)
						delete(this->param);
					this->param = new(char[3]);
					strcpy(this->param, "..");
					
					if(this->server_type == SERVER_TYPE_LOCAL) {
						if(this->LocalChangeWorkingDir("..")) {
							this->LocalGetWorkingDir();
							this->LocalGetDirlist();
						}
					}
					else {
						if(this->ChangeWorkingDir("..")) {
							if(!this->GetWorkingDir())
								this->EvalError();
							
							if(!this->RefreshFiles())
								this->EvalError();
						}
					}
					break;

	case	FOR_SERVER_MSG_MKD:	if(this->server_type == SERVER_TYPE_LOCAL) {
						this->LocalMakeDir(param, TRUE);
					}
					else {
						if(!this->MakeDir(param, TRUE, FALSE))
							this->EvalError();
					}
					break;
					
	case	FOR_SERVER_MSG_FXP_DIR_SRC:		// skip for now...
							break;
	
	case	FOR_SERVER_MSG_FXP_DIR_DEST:		// skip for now...
							break;
							
	case	FOR_SERVER_MSG_FXP_FILE_SRC:		// we are source
							if(!this->FXPFileSrc(param, magic))
								this->EvalError();
							break;
							
	case	FOR_SERVER_MSG_FXP_FILE_DEST:		// we are destination
							if(!this->FXPFileDest(param, magic, FALSE))
								this->EvalError();
							break;
							
	case	FOR_SERVER_MSG_FXP_FILE_DEST_AS_OK:	// we are destination
							if(!this->FXPFileDest(param, magic, TRUE))
								this->EvalError();
							break;
							
	case	FOR_SERVER_MSG_UPLOAD:			// only remote servers get this message
							if(!this->UploadFile(param, FALSE, FALSE))
								this->EvalError();
							break;
	
	case	FOR_SERVER_MSG_UPLOAD_AS_OK:		// only remote servers get this message
							if(!this->UploadFile(param, FALSE, TRUE))
								this->EvalError();
							break;
	
	case	FOR_SERVER_MSG_UPLOAD_NO_WAIT:		// only remote servers get this message
							if(!this->UploadFile(param, TRUE, FALSE))
								this->EvalError();
							break;
	
	case	FOR_SERVER_MSG_UPLOAD_NO_WAIT_AS_OK:	// only remote servers get this message
							if(!this->UploadFile(param, TRUE, TRUE))
								this->EvalError();
							break;
	
	case	FOR_SERVER_MSG_VIEWFILE:		// only remote gets this
							if(!this->LeechFile(param, NOTICE_TYPE_NOTICE_VIEW, FALSE, 0))
								this->EvalError();
							break;
							
	case	FOR_SERVER_MSG_LEECH_NO_NOTICE:		// only remote servers get this message
							if(!this->LeechFile(param, NOTICE_TYPE_NO_NOTICE, FALSE, 0))
								this->EvalError();
							break;
	
	case	FOR_SERVER_MSG_LEECH_NOTICE_SINGLE:	// only remote servers get this message
							if(!this->LeechFile(param, NOTICE_TYPE_NOTICE_SINGLE, FALSE, magic))
								this->EvalError();
							break;
	
	case	FOR_SERVER_MSG_LEECH_NOTICE_SINGLE_AS_OK:	// only remote servers get this message
								if(!this->LeechFile(param, NOTICE_TYPE_NOTICE_SINGLE, TRUE, magic))
									this->EvalError();
								break;
	
	case	FOR_SERVER_MSG_LEECH_NOTICE_MULTI:	// only remote servers get this message
							if(!this->LeechFile(param, NOTICE_TYPE_NOTICE_MULTI, FALSE, 0))
								this->EvalError();
							break;
	
	
	case	FOR_SERVER_MSG_EMIT_REFRESH_SINGLE:	display->PostMessageFromServer(SERVER_MSG_EMIT_REFRESH_SINGLE, magic, param);
							break;
	
	case	FOR_SERVER_MSG_EMIT_REFRESH_MULTI:	display->PostMessageFromServer(SERVER_MSG_EMIT_REFRESH_MULTI, this->magic, param);
							break;
							
	case	FOR_SERVER_MSG_UPLOAD_CWD:			// this one is for the local-fs only
								if(this->LocalChangeWorkingDir(param)) {
									this->LocalGetWorkingDir();
									this->LocalGetDirlist();
									display->PostMessageFromServer(SERVER_MSG_URGENT_OKAY, magic, param);
								}
								else
									display->PostMessageFromServer(SERVER_MSG_URGENT_ERROR, magic, param);
														
								break;
								
	case	FOR_SERVER_MSG_UPLOAD_DIR_NO_WAIT:		// only remote servers get this message
								if(!urgent) {
									// we need to prepare the local server first
									this->urgent = TRUE;
									this->UploadDirStart(param);
								}
								else {
									this->urgent = FALSE;
									this->UploadDir(param, TRUE, FALSE);
								}
								break;
	
	case	FOR_SERVER_MSG_UPLOAD_DIR_NO_WAIT_AS_OK:	// only remote servers get this message
								if(!urgent) {
									// we need to prepare the local server first
									this->urgent = TRUE;
									this->UploadDirStart(param);
								}
								else {
									this->urgent = FALSE;
									this->UploadDir(param, TRUE, TRUE);
								}
								break;
	
	case	FOR_SERVER_MSG_LEECH_DIR_NO_NOTICE:		// only remote servers get this message
								this->LeechDir(param, NOTICE_TYPE_NO_NOTICE, FALSE, 0);
								break;
	
	case	FOR_SERVER_MSG_LEECH_DIR_NOTICE_SINGLE:		// only remote servers get this message
								this->LeechDir(param, NOTICE_TYPE_NOTICE_SINGLE, FALSE, magic);
								break;

	case	FOR_SERVER_MSG_LEECH_DIR_NOTICE_SINGLE_AS_OK:	// only remote servers get this message
								this->LeechDir(param, NOTICE_TYPE_NOTICE_SINGLE, TRUE, magic);
								break;

	case	FOR_SERVER_MSG_LEECH_DIR_NOTICE_MULTI:		// only remote servers get this message
								this->LeechDir(param, NOTICE_TYPE_NOTICE_MULTI, FALSE, 0);
								break;
						
	case	FOR_SERVER_MSG_DELFILE:	if(this->server_type == SERVER_TYPE_LOCAL)
						this->LocalDeleteFile(param);
					else {
						if(!this->DeleteFile(param))
							this->EvalError();
					}
					break;
					
	case	FOR_SERVER_MSG_DELDIR:	if(this->server_type == SERVER_TYPE_LOCAL)
						this->LocalDeleteDir(param);
					else {
						if(!this->DeleteDir(param))
							this->EvalError();
					}
					break;
					
	case	FOR_SERVER_MSG_RENFROM:	if(this->server_type == SERVER_TYPE_LOCAL)
						this->LocalRenFrom(param);
					else {
						this->RenFrom(param);
					}
					break;
					
	case	FOR_SERVER_MSG_RENTO:	if(this->server_type == SERVER_TYPE_LOCAL)
						this->LocalRenTo(param);
					else {
						if(!this->RenTo(param));
							this->EvalError();
					}
					break;
					
	case	FOR_SERVER_MSG_PREP:	if(this->server_type == SERVER_TYPE_LOCAL) {
						this->LocalMakeDir(param, TRUE);
					}
					else {
						// remember undo position
						strcpy(this->undo_cwd, this->working_dir);
						
						if(!this->MakeDir(param, TRUE, TRUE))
							this->EvalError();
					}
					break;
					
	case	FOR_SERVER_MSG_UNDO:	if(this->have_undo) {
						if(this->ChangeWorkingDir(this->undo_cwd)) {
							if(!this->GetWorkingDir())
								this->EvalError();
							
							// try to remove dir
							if(!this->DeleteDir(this->undo_mkd))
								this->EvalError();
							
							if(!this->RefreshFiles())
								this->EvalError();
						}
						
						this->have_undo = FALSE;
					}
					break;

	case	FOR_SERVER_MSG_SITE:	// just remote ones get this
					if(!this->SendSITE(param))
						this->EvalError();
					break;
					
	case	FOR_SERVER_MSG_CHANGE_SORTING:	this->alpha_sort = !this->alpha_sort;
						this->internal_filelist = this->actual_filelist;
						this->actual_filelist = NULL;
						this->SortFilelist(FALSE);
						this->PostToDisplay(SERVER_MSG_NEW_FILELIST);
						break;
						
	case	FOR_SERVER_MSG_CLOSE:	this->KillMe(FALSE);
					break;
	}
}

void CServer::SetServerPrefs(BOOKMARK *bm)
{
	if(prefs.label)
		delete(prefs.label);
	if(prefs.host)
		delete(prefs.host);
	if(prefs.user)
		delete(prefs.user);
	if(prefs.pass)
		delete(prefs.pass);
	if(prefs.startdir)
		delete(prefs.startdir);
	if(prefs.exclude)
		delete(prefs.exclude);
	if(prefs.util_dir)
		delete(prefs.util_dir);
	if(prefs.game_dir)
		delete(prefs.game_dir);
	if(prefs.site_who)
		delete(prefs.site_who);
	if(prefs.site_user)
		delete(prefs.site_user);
	if(prefs.site_wkup)
		delete(prefs.site_wkup);
		
	prefs.label = new(char[strlen(bm->label) + 1]);
	strcpy(prefs.label, bm->label);
	prefs.host = new(char[strlen(bm->host) + 1]);
	strcpy(prefs.host, bm->host);
	prefs.user = new(char[strlen(bm->user) + 1]);
	strcpy(prefs.user, bm->user);
	prefs.pass = new(char[strlen(bm->pass) + 1]);
	strcpy(prefs.pass, bm->pass);
	prefs.startdir = new(char[strlen(bm->startdir) + 1]);
	strcpy(prefs.startdir, bm->startdir);
	prefs.exclude = new(char[strlen(bm->exclude) + 1]);
	strcpy(prefs.exclude, bm->exclude);
	prefs.util_dir = new(char[strlen(bm->util_dir) + 1]);
	strcpy(prefs.util_dir, bm->util_dir);
	prefs.game_dir = new(char[strlen(bm->game_dir) + 1]);
	strcpy(prefs.game_dir, bm->game_dir);
	prefs.site_who = new(char[strlen(bm->site_who) + 1]);
	strcpy(prefs.site_who, bm->site_who);
	prefs.site_user = new(char[strlen(bm->site_user) + 1]);
	strcpy(prefs.site_user, bm->site_user);
	prefs.site_wkup = new(char[strlen(bm->site_wkup) + 1]);
	strcpy(prefs.site_wkup, bm->site_wkup);

	prefs.port = bm->port;
	prefs.refresh_rate = bm->refresh_rate;
	prefs.noop_rate = bm->noop_rate;
	
	prefs.use_refresh = bm->use_refresh;
	prefs.use_noop = bm->use_noop;
	prefs.use_startdir = bm->use_startdir;
	prefs.use_exclude = bm->use_exclude;
	prefs.use_jump = bm->use_jump;
	prefs.use_track = bm->use_track;
	prefs.use_autologin = bm->use_autologin;
	prefs.use_chaining = bm->use_chaining;
	prefs.use_utilgames = bm->use_utilgames;
}

void CServer::EvalError(void)
{
	bool	have_error = TRUE;
	
	switch(this->error) {
	case	E_NO_ERROR:	have_error = FALSE;
				break;
	
	case	E_BAD_WELCOME:	sprintf(temp_string, "[%s]: %s", prefs.label, E_MSG_BAD_WELCOME);
				break;
	
	case	E_BAD_USER:	sprintf(temp_string, "[%s]: %s", prefs.label, E_MSG_BAD_USER);
				break;
	
	case	E_BAD_PASS:	sprintf(temp_string, "[%s]: %s", prefs.label, E_MSG_BAD_PASS);
				break;
	
	case	E_CONTROL_RESET:	sprintf(temp_string, "[%s]: %s", prefs.label, E_MSG_CONTROL_RESET);
					break;

	case	E_CTRL_SOCKET_CREATE:	sprintf(temp_string, "[%s]: %s", prefs.label, E_MSG_CTRL_SOCKET_CREATE);
					break;
	
	case	E_CTRL_SOCKET_CONNECT:	sprintf(temp_string, "[%s]: %s", prefs.label, E_MSG_CTRL_SOCKET_CONNECT);
					break;
	
	case	E_CTRL_ADDRESS_RESOLVE:	sprintf(temp_string, "[%s]: %s", prefs.label, E_MSG_CTRL_ADDRESS_RESOLVE);
					break;
						
	case	E_CTRL_TIMEOUT:		sprintf(temp_string, "[%s]: %s", prefs.label, E_MSG_CTRL_TIMEOUT);
					break;
					
	case	E_BAD_PWD:		sprintf(temp_string, "[%s]: %s", prefs.label, E_MSG_BAD_PWD);
					break;
					
	case	E_BAD_MSG:		sprintf(temp_string, "[%s]: %s", prefs.label, E_MSG_BAD_MSG);
					break;

	case	E_BAD_LOCALFILE:	sprintf(temp_string, "[%s]: %s", prefs.label, E_MSG_BAD_LOCALFILE);
					break;

	case	E_BAD_TYPE:		sprintf(temp_string, "[%s]: %s", prefs.label, E_MSG_BAD_TYPE);
					break;

	case	E_BAD_PORT:		sprintf(temp_string, "[%s]: %s", prefs.label, E_MSG_BAD_PORT);
					break;

	case	E_BAD_LIST:		sprintf(temp_string, "[%s]: %s", prefs.label, E_MSG_BAD_LIST);
					break;

	case	E_SOCKET_BIND:		sprintf(temp_string, "[%s]: %s", prefs.label, E_MSG_SOCKET_BIND);
					break;

	case	E_SOCKET_LISTEN:	sprintf(temp_string, "[%s]: %s", prefs.label, E_MSG_SOCKET_LISTEN);
					break;

	case	E_SOCKET_ACCEPT_TIMEOUT:	sprintf(temp_string, "[%s]: %s", prefs.label, E_MSG_SOCKET_ACCEPT_TIMEOUT);
						break;

	case	E_SOCKET_ACCEPT:	sprintf(temp_string, "[%s]: %s", prefs.label, E_MSG_SOCKET_ACCEPT);
					break;

	case	E_DATA_TIMEOUT:		sprintf(temp_string, "[%s]: %s", prefs.label, E_MSG_DATA_TIMEOUT);
					break;

	case	E_DATA_TCPERR:		sprintf(temp_string, "[%s]: %s", prefs.label, E_MSG_DATA_TCPERR);
					break;

	case	E_SOCKET_DATA_CREATE:	sprintf(temp_string, "[%s]: %s", prefs.label, E_MSG_SOCKET_DATA_CREATE);
					break;
					
	case	E_BAD_NOOP:		sprintf(temp_string, "[%s]: %s", prefs.label, E_MSG_BAD_NOOP);
					break;
					
	case	E_BAD_RETR:		sprintf(temp_string, "[%s]: '%s': %s", prefs.label, this->param, E_MSG_BAD_RETR);
					break;
					
	case	E_BAD_STOR:		sprintf(temp_string, "[%s]: '%s': %s", prefs.label, this->param, E_MSG_BAD_STOR);
					break;
					
	case	E_BAD_FILESIZE:		sprintf(temp_string, "[%s]: '%s': %s", prefs.label, this->param, E_MSG_BAD_FILESIZE);
					break;
					
	case	E_WRITEFILE_ERROR:	sprintf(temp_string, "[%s]: '%s': %s", prefs.label, this->param, E_MSG_WRITEFILE_ERROR);
					break;
					
	case	E_WRITEFILE_TIMEOUT:	sprintf(temp_string, "[%s]: '%s': %s", prefs.label, this->param, E_MSG_WRITEFILE_TIMEOUT);
					break;
					
	case	E_BAD_DELE:		sprintf(temp_string, "[%s]: '%s': %s", prefs.label, this->param, E_MSG_BAD_DELE);
					break;
					
	case	E_BAD_RMD:		sprintf(temp_string, "[%s]: '%s': %s", prefs.label, this->param, E_MSG_BAD_RMD);
					break;
					
	case	E_BAD_RENAME:		sprintf(temp_string, "[%s]: '%s': %s", prefs.label, this->param, E_MSG_BAD_RENAME);
					break;
					
	case	E_BAD_SITE:		sprintf(temp_string, "[%s]: '%s': %s", prefs.label, this->param, E_MSG_BAD_SITE);
					break;
					
	case	E_BAD_STEALTH:		sprintf(temp_string, "[%s]: %s", prefs.label, E_MSG_BAD_STEALTH);
					break;
					
	case	E_BAD_PASV:		sprintf(temp_string, "[%s]: %s", prefs.label, E_MSG_BAD_PASV);
					break;
					
	case	E_PASV_FAILED:		sprintf(temp_string, "[%s]: '%s' : %s", prefs.label, this->param, E_MSG_PASV_FAILED);
					break;
					
	default:		sprintf(temp_string, "[%s]: undefined error [you shouldn't get that error].", prefs.label);
				break;
	}

	if(have_error)
		display->PostStatusLine(temp_string, TRUE);

	// deadly errors
	if(this->error == E_CONTROL_RESET)
		this->KillMe(TRUE);
		
	this->error = E_NO_ERROR;
}

void CServer::KillMe(bool emergency)
{
	// post that we had to bail out
	if(emergency)
		sprintf(this->temp_string, "[%s]: bailing out.", this->prefs.label);
	else
		sprintf(this->temp_string, "[%s]: logging off.", this->prefs.label);
	
	display->PostStatusLine(this->temp_string, TRUE);
	this->PostToDisplay(SERVER_MSG_KILLME);
	
	// simply wait until we get killed
	do {
		usleep(1000000);
	} while(1);
}

void CServer::Run(void)
{
	ACTION_MSG_LIST	*ac_temp;
	bool		no_action;
	int		msg = -1, magic, random_add = 0;

	this->is_busy = TRUE;
	
	sprintf(this->temp_string, "[%s]: launching thread...", this->prefs.label);
	display->PostStatusLine(this->temp_string, TRUE);

	if(this->server_type == SERVER_TYPE_LOCAL) {		
		this->LocalGetWorkingDir();
		this->LocalGetDirlist();
		this->is_busy = FALSE;
	}
	else
	{
		if(!strcmp(this->prefs.label, uselocal_label))
			this->use_local = TRUE;
			
		// try to login
		if(!this->Login()) {
			this->EvalError();
			this->KillMe(TRUE);
		}

		// see if we have a nice starting dir
		if(this->prefs.use_startdir) {
			if(!this->ChangeWorkingDir(this->prefs.startdir)) {
				sprintf(this->temp_string, "[%s]: unable to use '%s' as a startdir, ignoring.", this->prefs.label, this->prefs.startdir);
				display->PostStatusLine(this->temp_string, TRUE);
			}
		}
		
		if(!this->GetWorkingDir())
			this->EvalError();

		if(!this->RefreshFiles()) {
			this->EvalError();
			this->KillMe(TRUE);
		}
		
		this->is_busy = FALSE;
	}
		
	do {
		no_action = TRUE;
		
		// look for actions
		pthread_mutex_lock(&(this->displaymsg_lock));
		ac_temp = this->display_msg_stack;
		
		if(ac_temp) {
			// see if we have to process an urgent message
			if((this->urgent && ((ac_temp->msg == FOR_SERVER_MSG_URGENT_OKAY) || (ac_temp->msg == FOR_SERVER_MSG_URGENT_ERROR))) || (!this->urgent)) {
				this->display_msg_stack = ac_temp->next;
				pthread_mutex_unlock(&(this->displaymsg_lock));

				if(!this->urgent)
					msg = ac_temp->msg;
					
				magic = ac_temp->magic;

				if(ac_temp->param) {
					this->param = new(char[strlen(ac_temp->param)+1]);
					strcpy(this->param, ac_temp->param);
				}
				else
					this->param = NULL;

				if(!this->urgent || (this->urgent && (ac_temp->msg == FOR_SERVER_MSG_URGENT_OKAY))) {
					this->is_busy = TRUE;
					this->HandleMessage(msg, ac_temp->param, magic);
					if(!this->urgent)
						this->is_busy = FALSE;
				}
				else {
					this->urgent = FALSE;
					this->is_busy = FALSE;
					this->PostBusy(NULL);
				}
				
				if(this->param)
					delete(this->param);

				this->param = NULL;
				
				delete(ac_temp->param);
				delete(ac_temp);
				no_action = FALSE;
			}
			else
				pthread_mutex_unlock(&(this->displaymsg_lock));
		}
		else
			pthread_mutex_unlock(&(this->displaymsg_lock));
		
		// perform automatic functions (can just be enabled with remote servers)
		// auto-refresh
		if(this->prefs.use_refresh) {
			if(this->refresh_slept >= (SLEEPS_NEEDED * (this->prefs.refresh_rate + random_add))) {
				random_add = (int) (- this->prefs.refresh_rate / 2 + ((double)(this->prefs.refresh_rate)*rand()/(RAND_MAX+1.0))) * random_refresh;
				this->is_busy = TRUE;
				this->refresh_slept = 0;
				if(!this->RefreshFiles())
					this->EvalError();
					
				this->is_busy = FALSE;
			}
		}
		
		// auto-noop
		if(this->prefs.use_noop) {
			if(this->noop_slept >= (SLEEPS_NEEDED * this->prefs.noop_rate)) {
				this->is_busy = TRUE;
				this->noop_slept = 0;
				if(!this->Noop())
					this->EvalError();

				this->is_busy = FALSE;
			}
		}

		if(no_action) {
			if(this->prefs.use_noop)
				this->noop_slept++;
			if(this->prefs.use_refresh)
				this->refresh_slept++;
				
			usleep(ACTION_MSG_SLEEP);
		}
	} while(1);
}

void CServer::PostToDisplay(int msg)
{
	display->PostMessageFromServer(msg, this->magic, NULL);
}

char *CServer::ObtainBusy(void)
{
	char	*busy = NULL;
	
	pthread_mutex_lock(&(this->busy_lock));

	if(this->busy) {
		busy = new(char[strlen(this->busy) + 1]);
		strcpy(busy, this->busy);
	}
	
	pthread_mutex_unlock(&(this->busy_lock));

	return(busy);
}

void CServer::ObtainWorkingDir(char *cwd)
{	
	pthread_mutex_lock(&(this->cwd_lock));

	strcpy(cwd, this->working_dir);
			
	pthread_mutex_unlock(&(this->cwd_lock));

}

FILELIST *CServer::ObtainFilelist(bool *use_jump)
{
	FILELIST	*fl_temp, *fl_temp1 = NULL, *fl_new, *fl_start = NULL;
	
	pthread_mutex_lock(&(this->filelist_lock));

	fl_temp = this->actual_filelist;

	while(fl_temp) {
		fl_new = new(FILELIST);
		fl_new->next = NULL;
		fl_new->magic = fl_temp->magic;
		fl_new->is_marked = FALSE;
		fl_new->name = new(char[strlen(fl_temp->name) + 1]);
		strcpy(fl_new->name, fl_temp->name);
		strcpy(fl_new->date, fl_temp->date);
		strcpy(fl_new->owner, fl_temp->owner);
		strcpy(fl_new->mode, fl_temp->mode);
		fl_new->size = fl_temp->size;
		fl_new->time = fl_temp->time;
		fl_new->is_dir = fl_temp->is_dir;
		
		if(!fl_temp1)
			fl_start = fl_new;
		else
			fl_temp1->next = fl_new;
		
		fl_temp1 = fl_new;	
		fl_temp = fl_temp->next;
	}

	pthread_mutex_unlock(&(this->filelist_lock));

	*use_jump = this->prefs.use_jump;
	return(fl_start);
}

void CServer::LocalRenFrom(char *from)
{
	strcpy(this->rename_temp, from);
}

void CServer::LocalRenTo(char *to)
{
	if(rename(this->rename_temp, to) == -1) {
		sprintf(this->temp_string, "[%s]: unable to rename '%s'", this->prefs.label, this->rename_temp);
		display->PostStatusLine(this->temp_string, FALSE);
	}

	this->LocalGetDirlist();
}

void CServer::LocalMakeDir(char *dir, bool try_cd)
{
	int	ret;
	
	ret = mkdir(dir, 0);
	chmod(dir, S_IRUSR | S_IWUSR | S_IXUSR);
	
	if((ret == 0) || try_cd) {
		if(this->LocalChangeWorkingDir(dir)) {
			this->LocalGetWorkingDir();
			this->LocalGetDirlist();
		}	
	}
	else {
		sprintf(this->temp_string, "[%s]: unable to create '%s'", this->prefs.label, dir);
		display->PostStatusLine(this->temp_string, FALSE);
	}
}

bool CServer::LocalChangeWorkingDir(char *dir)
{	
	if(!chdir(dir))
		return(TRUE);
	
	sprintf(this->temp_string, "[%s]: unable to cwd to '%s'", this->prefs.label, dir);
	display->PostStatusLine(this->temp_string, FALSE);
	return(FALSE);
}

void CServer::LocalDeleteFile(char *file)
{
	if(remove(file) == -1) {
		sprintf(this->temp_string, "[%s]: unable to delete '%s'", this->prefs.label, file);
		display->PostStatusLine(this->temp_string, FALSE);
	}
}

void CServer::LocalDeleteDir(char *dir)
{
	if(rmdir(dir) == -1) {
		sprintf(this->temp_string, "[%s]: unable to delete '%s'", this->prefs.label, dir);
		display->PostStatusLine(this->temp_string, FALSE);
	}
}

void CServer::PostBusy(char *busy)
{
	pthread_mutex_lock(&(this->busy_lock));

	if(this->busy)
		delete(this->busy);

	this->busy = NULL;

	if(!busy) {
		display->PostMessageFromServer(SERVER_MSG_IDLE, this->magic, NULL);
	}
	else {
		this->busy = new(char[strlen(busy) + 1]);
		strcpy(this->busy, busy);
		display->PostMessageFromServer(SERVER_MSG_BUSY, this->magic, busy);
	}

	pthread_mutex_unlock(&(this->busy_lock));
}

bool CServer::SendSITE(char *site)
{
	char	temp[10];
	
	this->PostBusy("SITE");
	this->tcp.FlushStack();

	// check for QUOTE
	strncpy(temp, site, 6);
	temp[6] = '\0';
	
	if(!strcasecmp(temp, "QUOTE ")) {
		// quote the whole string
		sprintf(this->temp_string, "%s\r\n", site+6);
	}
	else {
		sprintf(this->temp_string, "SITE %s\r\n", site);
	}
	
	if(!this->tcp.SendData(this->temp_string)) {
		this->error = E_CONTROL_RESET;
		this->PostBusy(NULL);
		return(FALSE);
	}

	if(!this->tcp.WaitForMessage()) {
		this->error = E_BAD_SITE;
		this->PostBusy(NULL);
		return(FALSE);
	}

	this->PostBusy(NULL);
	return(TRUE);
}

void CServer::RenFrom(char *from)
{
	this->PostBusy("RNFR");
	this->tcp.FlushStack();
	
	sprintf(this->temp_string, "RNFR %s\r\n", from);
	if(!this->tcp.SendData(this->temp_string)) {
		this->error = E_CONTROL_RESET;	// will (not yet) be eval'd
	}

	if(!this->tcp.WaitForMessage()) {
		this->error = E_BAD_RENAME;	// will not be evaluated
	}
	this->PostBusy(NULL);
}

bool CServer::RenTo(char *to)
{
	this->PostBusy("RNTO");
	this->tcp.FlushStack();
	
	sprintf(this->temp_string, "RNTO %s\r\n", to);
	if(!this->tcp.SendData(this->temp_string)) {
		this->error = E_CONTROL_RESET;
		this->PostBusy(NULL);
		return(FALSE);
	}

	if(!this->tcp.WaitForMessage()) {
		this->error = E_BAD_RENAME;
		this->PostBusy(NULL);
		return(FALSE);
	}

	this->AddEntryToCache(to);
	this->PostBusy(NULL);
	return(this->RefreshFiles());
}

bool CServer::DeleteFile(char *file)
{
	this->PostBusy("DELE");
	this->tcp.FlushStack();
	
	sprintf(this->temp_string, "DELE %s\r\n", file);
	if(!this->tcp.SendData(this->temp_string)) {
		this->error = E_CONTROL_RESET;
		this->PostBusy(NULL);
		return(FALSE);
	}

	if(!this->tcp.WaitForMessage()) {
		this->error = E_BAD_DELE;
		this->PostBusy(NULL);
		return(FALSE);
	}

	// okiez, all worked fine. we can post a msg to deselect the entry
	display->PostMessageFromServer(SERVER_MSG_DEMARK, this->magic, file);
	this->PostBusy(NULL);
	
	return(TRUE);
}

bool CServer::DeleteDir(char *dir)
{
	this->PostBusy(" RMD");
	this->tcp.FlushStack();
	
	sprintf(this->temp_string, "RMD %s\r\n", dir);
	if(!this->tcp.SendData(this->temp_string)) {
		this->error = E_CONTROL_RESET;
		this->PostBusy(NULL);
		return(FALSE);
	}

	if(!this->tcp.WaitForMessage()) {
		this->error = E_BAD_RMD;
		this->PostBusy(NULL);
		return(FALSE);
	}

	// okiez, all worked fine. we can post a msg to deselect the entry
	display->PostMessageFromServer(SERVER_MSG_DEMARK, this->magic, dir);
	this->PostBusy(NULL);
	
	return(TRUE);
}

bool CServer::MakeDir(char *dir, bool try_cd, bool use_undo)
{
	char	*start, *end, *buffer, *real_dir = NULL;
	bool	fail = FALSE, use_real_name = FALSE;
	
	this->PostBusy(" MKD");
	this->tcp.FlushStack();
	
	sprintf(this->temp_string, "MKD %s\r\n", dir);
	if(!this->tcp.SendData(this->temp_string)) {
		this->error = E_CONTROL_RESET;
		this->PostBusy(NULL);
		return(FALSE);
	}

	if(!this->tcp.WaitForMessage()) {
		if(!try_cd) {
			this->error = E_BAD_MKD;
			this->PostBusy(NULL);
			return(FALSE);
		}
		else
			use_real_name = TRUE;
	}

	// now get the real name of the created dir
	if(!use_real_name) {
		buffer = this->tcp.GetControlBuffer();
		start = strchr(buffer, '"');
		if(start) {
			start += 1;
			end = strchr(start, '"');
		
			if(end) {
				*end = '\0';
				strcpy(this->temp_string, start);
				*end = '"';
			
				// extract the last part of it
				start = strrchr(this->temp_string, '/');
				if(start) {
					// extract last part
					start = start + 1;
					real_dir = new(char[strlen(start) + 1]);
					strcpy(real_dir, start);
				}
				else {
					// seems like we have no preceding path, use the full length
					real_dir = new(char[strlen(this->temp_string) + 1]);
					strcpy(real_dir, this->temp_string);
				}
			}
			else
				fail = TRUE;
		}
		else
			fail = TRUE;
	}
	else {
		real_dir = new(char[strlen(dir) + 1]);
		strcpy(real_dir, dir);
	}
		
	if(fail) {
		// our last chance, we take the original name... hmmrz
		real_dir = new(char[strlen(dir) + 1]);
		strcpy(real_dir, dir);
	}
	
	if(!use_real_name) {
		if(use_undo) {
			strcpy(this->undo_mkd, real_dir);
			this->have_undo = TRUE;
		}
		
		this->AddEntryToCache(real_dir);
		delete(real_dir);
	}
		
	if(this->ChangeWorkingDir(dir)) {
		if(this->GetWorkingDir()) {
			this->PostBusy(NULL);
			return(this->RefreshFiles());
		}
		else {
			this->PostBusy(NULL);
			return(FALSE);
		}
	}
	else {
		this->PostBusy(NULL);
		return(FALSE);
	}
}

bool CServer::Noop(void)
{
	this->PostBusy("NOOP");
	this->tcp.FlushStack();
	
	if(!this->tcp.SendData("NOOP\r\n")) {
		this->error = E_CONTROL_RESET;
		this->PostBusy(NULL);
		return(FALSE);
	}

	if(!this->tcp.WaitForMessage()) {
		this->error = E_BAD_NOOP;
		this->PostBusy(NULL);
		return(FALSE);
	}

	this->PostBusy(NULL);
	return(TRUE);
}

bool CServer::ChangeWorkingDir(char *dir)
{	
	this->PostBusy(" CWD");
	this->tcp.FlushStack();
	
	sprintf(this->temp_string, "CWD %s\r\n", dir);
	if(!this->tcp.SendData(this->temp_string)) {
		this->error = E_CONTROL_RESET;
		this->EvalError();
		this->PostBusy(NULL);
		return(FALSE);
	}
	
	if(!this->tcp.WaitForMessage()) {
		sprintf(this->temp_string, "[%s]: unable to cwd to '%s'", this->prefs.label, dir);
		display->PostStatusLine(this->temp_string, FALSE);
		this->PostBusy(NULL);
		return(FALSE);
	}

	this->PostBusy(NULL);
	return(TRUE);
}

void CServer::LocalGetWorkingDir(void)
{
	pthread_mutex_lock(&(this->cwd_lock));

	getcwd(this->working_dir, SERVER_WORKINGDIR_SIZE);

	pthread_mutex_unlock(&(this->cwd_lock));

	this->PostToDisplay(SERVER_MSG_NEW_CWD);
}

void CServer::LocalGetDirlist(void)
{
	DIR		*dir = NULL;
	struct dirent	*dirent = NULL;
	int		magic = 0;
	FILELIST	*fl_temp = NULL, *fl_new = NULL;
	struct stat	status;
	struct passwd	*pw_ent = NULL;
	bool		invalid;
	char		*end = NULL, *start = NULL;
	
	dir = opendir(this->working_dir);
	
	if(dir) {
		// build up new list
		fl_temp = NULL;
		
		while((dirent = readdir(dir))) {
			if(strcmp(dirent->d_name, ".") && strcmp(dirent->d_name, "..")) {

				fl_new = new(FILELIST);
				fl_new->next = NULL;
				fl_new->magic = magic;
				magic++;
			
				fl_new->name = new(char[strlen(dirent->d_name) + 1]);
				strcpy(fl_new->name, dirent->d_name);

				// now get additional information about file/dir
				sprintf(this->temp_string, "%s/%s", this->working_dir, fl_new->name);
				stat(this->temp_string, &status);

				fl_new->is_dir = S_ISDIR(status.st_mode);
				fl_new->mode[0] = fl_new->is_dir ? 'd' : '-';
				fl_new->mode[1] = status.st_mode & S_IRUSR ? 'r' : '-';
				fl_new->mode[2] = status.st_mode & S_IWUSR ? 'w' : '-';
				fl_new->mode[3] = status.st_mode & S_IXUSR ? 'x' : '-';
				fl_new->mode[4] = status.st_mode & S_IRGRP ? 'r' : '-';
				fl_new->mode[5] = status.st_mode & S_IWGRP ? 'w' : '-';
				fl_new->mode[6] = status.st_mode & S_IXGRP ? 'x' : '-';
				fl_new->mode[7] = status.st_mode & S_IROTH ? 'r' : '-';
				fl_new->mode[8] = status.st_mode & S_IWOTH ? 'w' : '-';
				fl_new->mode[9] = status.st_mode & S_IXOTH ? 'x' : '-';
				fl_new->mode[10] = '\0';
			
				pw_ent = getpwuid(status.st_uid);
				if(pw_ent)
					strcpy(fl_new->owner, pw_ent->pw_name);
				else
					strcpy(fl_new->owner, "<INVALD>");
			
				fl_new->size = status.st_size;
				sprintf(this->temp_string, ctime(&status.st_mtime));

				invalid = FALSE;
				end = strrchr(this->temp_string, ':');
				if(end) {
					*end = '\0';
					start = strchr(this->temp_string, ' ');
					if(start)
						start += 1;
					else
						invalid = TRUE;
				}
				else
					invalid = TRUE;
				
				if(!invalid)
					strcpy(fl_new->date, start);
				else
					strcpy(fl_new->date, "INVALID");
				
				if(fl_temp)
					fl_temp->next = fl_new;
				else
					this->internal_filelist = fl_new;
			
				fl_temp = fl_new;
			}
		}
	
		closedir(dir);

		this->SortFilelist(TRUE);
		this->PostToDisplay(SERVER_MSG_NEW_FILELIST);
	}
}

void CServer::PostFromDisplay(int msg, char *param)
{
	this->PostFromDisplay(msg, param, 0);
}

void CServer::PostFromDisplay(int msg, char *param, int magic)
{
	ACTION_MSG_LIST	*msg_temp, *msg_new;
	
	pthread_mutex_lock(&(this->displaymsg_lock));

	msg_temp = this->display_msg_stack;
	msg_new = new(ACTION_MSG_LIST);
	msg_new->next = NULL;
	msg_new->msg = msg;
	msg_new->param = new(char[strlen(param) + 1]);
	strcpy(msg_new->param, param);
	msg_new->magic = magic;
			
	if(msg_temp) {
		while(msg_temp->next)
			msg_temp = msg_temp->next;
		
		msg_temp->next = msg_new;
	}
	else
		this->display_msg_stack = msg_new;

	pthread_mutex_unlock(&(this->displaymsg_lock));
}

void CServer::PostUrgentFromDisplay(int msg, char *param)
{
	ACTION_MSG_LIST	*msg_temp, *msg_new;
	
	pthread_mutex_lock(&(this->displaymsg_lock));

	msg_temp = this->display_msg_stack;
	msg_new = new(ACTION_MSG_LIST);
	msg_new->next = NULL;
	msg_new->msg = msg;
	msg_new->param = new(char[strlen(param) + 1]);
	strcpy(msg_new->param, param);
	msg_new->magic = 0;
			
	this->display_msg_stack = msg_new;
	msg_new->next = msg_temp;
	
	pthread_mutex_unlock(&(this->displaymsg_lock));
}

bool CServer::Login(void)
{
	this->PostBusy("CONN");
	
	sprintf(this->temp_string, "[%s]: logging into '%s'", prefs.label, prefs.host);
	display->PostStatusLine(this->temp_string, FALSE);
	if(!this->tcp.OpenControl(&(this->prefs))) {
		this->error = this->tcp.GetError();
		this->PostBusy(NULL);
		return(FALSE);
	}
	
	this->PostBusy("WLCM");

	// wait for welcome message
	if(!this->tcp.WaitForMessage()) {
		this->error = E_BAD_WELCOME;
		this->PostBusy(NULL);
		return(FALSE);
	}
	
	this->PostBusy("USER");

	// send USER
	sprintf(this->temp_string, "USER %s\r\n", this->prefs.user);
	if(!this->tcp.SendData(this->temp_string)) {
		this->error = E_CONTROL_RESET;
		this->PostBusy(NULL);
		return(FALSE);
	}
	
	if(!this->tcp.WaitForMessage()) {
		this->error = E_BAD_USER;
		this->PostBusy(NULL);
		return(FALSE);
	}
	
	this->PostBusy("PASS");

	// send PASS
	sprintf(this->temp_string, "PASS %s\r\n", this->prefs.pass);
	if(!this->tcp.SendData(this->temp_string)) {
		this->error = E_CONTROL_RESET;
		this->PostBusy(NULL);
		return(FALSE);
	}
	
	if(!this->tcp.WaitForMessage()) {
		this->error = E_BAD_PASS;
		this->PostBusy(NULL);
		return(FALSE);
	}
	
	this->PostBusy(NULL);
	return(TRUE);
}

bool CServer::GetWorkingDir(void)
{
	char	*buffer;
	
	this->PostBusy(" PWD");
	this->tcp.FlushStack();
	
	if(!this->tcp.SendData("PWD\r\n")) {
		this->error = E_CONTROL_RESET;
		this->PostBusy(NULL);
		return(FALSE);
	}
	
	if(!this->tcp.WaitForMessage()) {
		this->error = E_BAD_PWD;
		this->PostBusy(NULL);
		return(FALSE);
	}

	buffer = this->tcp.GetControlBuffer();
	buffer = strchr(buffer, '"') + 1;

	pthread_mutex_lock(&(this->cwd_lock));
	if(!buffer || !strchr(buffer, '"')) {
		sprintf(this->temp_string, "[%s]: troubles while getting current working dir.", this->prefs.label);
		display->PostStatusLine(this->temp_string, TRUE);
		strcpy(this->working_dir, "INVALID");
	}
	else {
		*(strchr(buffer, '"')) = '\0';
		strcpy(this->working_dir, buffer);
	}
	pthread_mutex_unlock(&(this->cwd_lock));

	this->PostToDisplay(SERVER_MSG_NEW_CWD);
	this->PostBusy(NULL);
	return(TRUE);
}

bool CServer::LeechDir(char *dir, int notice_type, bool as_ok, int dest_magic)
{
	FILELIST	*fl_temp;
	bool		okay, filter;
	
	// CWD into dir and get filelist
	if(!this->ChangeWorkingDir(dir))
		return(FALSE);
	
	if(!this->GetWorkingDir()) {
		this->EvalError();
		return(FALSE);
	}
	
	if(!this->RefreshFiles()) {
		this->EvalError();
		return(FALSE);
	}
	
	// post MKD notice
	switch(notice_type) {
	case	NOTICE_TYPE_NOTICE_SINGLE:	display->PostMessageFromServer(SERVER_MSG_NOTICE_LEECH_SINGLE_MKD, dest_magic, dir);
						break;
	
	case	NOTICE_TYPE_NOTICE_MULTI:	display->PostMessageFromServer(SERVER_MSG_NOTICE_LEECH_MULTI_MKD, this->magic, dir);
						break;
	}

	// now leech all files in the dir (and post correct notices)
	fl_temp = this->actual_filelist;
	okay = TRUE;
	while(okay && fl_temp) {
		if(!fl_temp->is_dir) {
			if(this->prefs.use_exclude)
				filter = FilterFilename(fl_temp->name, this->prefs.exclude);
			else
				filter = FilterFilename(fl_temp->name, NULL);
			
			if(filter) {
				if(this->param)
					delete(this->param);
				this->param = new(char[strlen(fl_temp->name)+1]);
				strcpy(this->param, fl_temp->name);
			
				if(!(okay = this->LeechFile(fl_temp->name, notice_type, as_ok, dest_magic))) {
					// continue in case of DUPE
					if(this->error == E_BAD_STOR)
						okay = TRUE;
					
					this->EvalError();
				}
			}
		}
		fl_temp = fl_temp->next;
	}

	if(!okay)
		return(FALSE);
	
	// now CWD back on this host (and all noticed)
	if(!this->ChangeWorkingDir(".."))
		return(FALSE);
	
	if(!this->GetWorkingDir()) {
		this->EvalError();
		return(FALSE);
	}
	
	if(!this->RefreshFiles()) {
		this->EvalError();
		return(FALSE);
	}
	
	// post CWD notice
	switch(notice_type) {
	case	NOTICE_TYPE_NOTICE_SINGLE:	display->PostMessageFromServer(SERVER_MSG_NOTICE_LEECH_SINGLE_CWD, dest_magic, "..");
						break;
	
	case	NOTICE_TYPE_NOTICE_MULTI:	display->PostMessageFromServer(SERVER_MSG_NOTICE_LEECH_MULTI_CWD, this->magic, "..");
						break;
	}

	return(TRUE);
}

bool CServer::LeechFile(char *file, int notice_type, bool as_ok, int dest_magic)
{
	char		*end, *start, *buffer, port_str[256], name[SERVER_WORKINGDIR_SIZE];
	long		size;
	struct timeval	before, after;
	float		speed;
	
	this->PostBusy("RETR");
	this->tcp.FlushStack();
	
	sprintf(this->temp_string, "[%s]: leeching '%s'", this->prefs.label, file);
	display->PostStatusLine(this->temp_string, FALSE);
	
	if(!this->tcp.SendData("TYPE I\r\n")) {
		this->error = E_CONTROL_RESET;
		this->PostBusy(NULL);
		return(FALSE);
	}
	
	if(!this->tcp.WaitForMessage()) {
		this->error = E_BAD_TYPE;
		this->PostBusy(NULL);
		return(FALSE);
	}
	
/*	if(!this->tcp.SendData("MODE S\r\n")) {
		this->error = E_CONTROL_RESET;
		this->PostBusy(NULL);
		return(FALSE);
	}
	
	this->tcp.WaitForMessage();

*/	if(!this->tcp.OpenData(port_str, this->use_local)) {
		this->error = this->tcp.GetError();
		this->PostBusy(NULL);
		return(FALSE);
	}
	
	sprintf(this->temp_string, "PORT %s\r\n", port_str);
	if(!this->tcp.SendData(this->temp_string)) {
		this->error = E_CONTROL_RESET;
		this->PostBusy(NULL);
		return(FALSE);
	}
	
	if(!this->tcp.WaitForMessage()) {
		this->tcp.CloseData();
		this->error = E_BAD_PORT;
		this->PostBusy(NULL);
		return(FALSE);
	}
	
	sprintf(this->temp_string, "RETR %s\r\n", file);
	if(!this->tcp.SendData(this->temp_string)) {
		this->error = E_CONTROL_RESET;
		this->PostBusy(NULL);
		return(FALSE);
	}
	
	if(!this->tcp.WaitForMessage()) {
		this->tcp.CloseData();
		this->error = E_BAD_RETR;
		this->PostBusy(NULL);
		return(FALSE);
	}

	// welp, determine filesize
	buffer = this->tcp.GetControlBuffer();
	start = strrchr(buffer, '(');
	if(!start) {
		this->tcp.CloseData();
		this->error = E_BAD_FILESIZE;
		this->PostBusy(NULL);
		return(FALSE);
	}

	end = strchr(start, ' ');
	if(!end) {
		this->tcp.CloseData();
		this->error = E_BAD_FILESIZE;
		this->PostBusy(NULL);
		return(FALSE);
	}
	
	start += 1;
	*end = '\0';
	size = atol(start);
	*end = ' ';
	
	if(!this->tcp.AcceptData()) {
		this->error = this->tcp.GetError();
		this->PostBusy(NULL);
		return(FALSE);
	}

	// build full dirname from local filesys
	 global_server->server->ObtainWorkingDir(name);
	  
	// now remove old .okay and .error files for this one
	sprintf(this->temp_string, "%s%s.okay", okay_dir, file);
	remove(this->temp_string);
	sprintf(this->temp_string, "%s%s.error", okay_dir, file);
	remove(this->temp_string);
	
	sprintf(this->temp_string, "%s/%s", name, file);
	
	// if we should notice the dest(s), do it now
	switch(notice_type) {
	case	NOTICE_TYPE_NOTICE_SINGLE:	if(!as_ok)
							display->PostMessageFromServer(SERVER_MSG_NOTICE_UPLOAD_SINGLE, dest_magic, file);
						else
							display->PostMessageFromServer(SERVER_MSG_NOTICE_UPLOAD_SINGLE_AS_OK, dest_magic, file);
						break;
	
	case	NOTICE_TYPE_NOTICE_MULTI:	display->PostMessageFromServer(SERVER_MSG_NOTICE_UPLOAD_MULTI, this->magic, file);
						break;
	}
	
	if(!this->tcp.ReadFile(this->temp_string, size)) {
		this->error = this->tcp.GetError();
		this->tcp.CloseData();
		this->tcp.WaitForMessage();
		this->PostBusy(NULL);
		return(FALSE);
	}
		
	this->tcp.CloseData();

	if(!this->tcp.WaitForMessage()) {
		this->error = this->tcp.GetError();
		this->PostBusy(NULL);
		return(FALSE);
	}
	
	// okiez, all worked fine. we can post a msg to deselect the entry
	display->PostMessageFromServer(SERVER_MSG_DEMARK, this->magic, file);

	this->tcp.GetTimevals(&before, &after, &size);
	speed = (after.tv_sec - before.tv_sec) * 1000000 + (after.tv_usec - before.tv_usec) + 1;
	speed = ((float)size / ((float)(speed) / 1000000.0)) / 1024;
	sprintf(this->temp_string, "[%s]: leeched '%s' (%ld) at %4.02f kb/s", this->prefs.label, file, size, speed);
	display->PostStatusLine(this->temp_string, FALSE);

	this->PostBusy(NULL);

	if(notice_type == NOTICE_TYPE_NOTICE_VIEW)
		display->PostMessageFromServer(SERVER_MSG_NOTICE_VIEW, this->magic, file);
	
	return(TRUE);
}

void CServer::UploadDirStart(char *dir)
{
	SERVERLIST	*local = global_server;
	
	// we have to get the LOCAL server to change dir and refresh the files
	// after that, he should notice us that he is finished, and we can continue

	local->server->PostFromDisplay(FOR_SERVER_MSG_UPLOAD_CWD, dir, this->magic);
}

bool CServer::UploadDir(char *dir, bool no_wait, bool as_ok)
{
	SERVERLIST	*local = global_server;
	FILELIST	*fl_start, *fl_temp, *fl_temp1;
	bool		dummy, okay, filter;
	
	// at this point, the LOCAL server already noticed us, that he has changed into the to-be-copied dir
	// now we need to create the dir on this server, CWD into and refresh files
	if(!this->MakeDir(dir, TRUE, FALSE)) {
		this->EvalError();
		return(FALSE);
	}
	
	// now obtain a filelist from the LOCAL server, and upload every file
	fl_start = local->server->ObtainFilelist(&dummy);
	
	fl_temp = fl_start;
	okay = TRUE;
	while(okay && fl_temp) {
		if(!fl_temp->is_dir) {
			if(this->prefs.use_exclude)
				filter = FilterFilename(fl_temp->name, this->prefs.exclude);
			else
				filter = FilterFilename(fl_temp->name, NULL);
			
			if(filter) {
				if(this->param)
					delete(this->param);
				this->param = new(char[strlen(fl_temp->name)+1]);
				strcpy(this->param, fl_temp->name);
			
				if(!(okay = this->UploadFile(fl_temp->name, no_wait, as_ok))) {
					// continue in case of DUPE
					if(this->error == E_BAD_STOR)
						okay = TRUE;
					
					this->EvalError();
				}
			}
		}
		fl_temp = fl_temp->next;
	}

	// free the obtained filelist
	fl_temp = fl_start;
	while(fl_temp) {
		fl_temp1 = fl_temp;
		fl_temp = fl_temp->next;
		delete(fl_temp1->name);
		delete(fl_temp1);
	}
	
	if(!okay)
		return(FALSE);
	
	// now CWD back on this host and on the LOCAL filesys
	local->server->PostFromDisplay(FOR_SERVER_MSG_CWD, "..");
	
	if(!this->ChangeWorkingDir(".."))
		return(FALSE);
	
	if(!this->GetWorkingDir()) {
		this->EvalError();
		return(FALSE);
	}
	
	if(!this->RefreshFiles()) {
		this->EvalError();
		return(FALSE);
	}
	
	return(TRUE);
}

void CServer::PostStatusFile(char *filename, char *suffix)
{
	FILE	*file;
	char	temp[256];
	
	sprintf(temp, "%s%d.%s.%s", okay_dir, this->pid, filename, suffix);
	
	file = fopen(temp, "w");
	fclose(file);
}

void CServer::PostStatusFile(char *filename, char *suffix, char *text)
{
	FILE	*file;
	char	temp[256];
	
	sprintf(temp, "%s%d.%s.%s", okay_dir, this->pid, filename, suffix);
	
	file = fopen(temp, "w");
	fprintf(file, "%s", text);
	fclose(file);
}

bool CServer::WaitForStatusFile(int dpid, char *filename, char *suffix)
{
	struct stat	stat_out;
	char	temp[256], temp2[256];
	int	try_file = 0;
	
	sprintf(temp, "%s%d.%s.%s", okay_dir, dpid, filename, suffix);
	sprintf(temp2, "%s%d.%s.ABORT", okay_dir, dpid, filename);

	while((try_file < 80) && (stat(temp, &stat_out) == -1) && (stat(temp2, &stat_out) == -1)) {
		try_file++;
		usleep(250000);
	}
	
	if((stat(temp, &stat_out) == -1)) {
		remove(temp2);
		return(FALSE);
	}
	else
		remove(temp);
		
	return(TRUE);
}

bool CServer::WaitForStatusFile(int dpid, char *filename, char *suffix, char *out)
{
	struct stat	stat_out;
	FILE	*file;
	char	temp[256], temp2[256];
	int	try_file = 0;
	
	sprintf(temp, "%s%d.%s.%s", okay_dir, dpid, filename, suffix);
	sprintf(temp2, "%s%d.%s.ABORT", okay_dir, dpid, filename);
	
	while((try_file < 80) && (stat(temp, &stat_out) == -1) && (stat(temp2, &stat_out) == -1)) {
		try_file++;
		usleep(250000);
	}
	
	if((stat(temp, &stat_out) == -1)) {
		remove(temp2);
		return(FALSE);
	}
	else {
		file = fopen(temp, "r");
		fgets(out, 31, file);
		fclose(file);
		remove(temp);
	}
		
	return(TRUE);
}

bool CServer::FXPFileDest(char *file, int src_pid, bool as_ok)
{
	char	port_msg[32], temp[256], *start;
	struct timeval	before, after;
	long	size;
	float	speed;
		
	this->PostBusy("HSHK");
	this->tcp.FlushStack();
	
	// TYPE without error checking (shrugs)
	this->tcp.SendData("TYPE I\r\n");
	this->tcp.WaitForMessage();
	
	// wait until SRC had issued PASV
	if(!this->WaitForStatusFile(src_pid, file, "PASV_OK", port_msg)) {
		this->error = E_NO_ERROR;	// error printed by SRC
		this->PostBusy(NULL);
		return(FALSE);
	}
	
	// send PORT str
	sprintf(temp, "PORT %s\r\n", port_msg);
	if(!this->tcp.SendData(temp)) {
		this->error = E_CONTROL_RESET;
		this->PostBusy(NULL);
		this->PostStatusFile(file, "ABORT");
		return(FALSE);
	}
	
	if(!this->tcp.WaitForMessage()) {
		this->error = E_BAD_PORT;
		this->PostBusy(NULL);
		this->PostStatusFile(file, "ABORT");
		return(FALSE);
	}

	// try to up the file, issue STOR
	if(!as_ok)
		strcpy(this->temp_string,  file);
	else {
		strcpy(temp, file);
		start = strrchr(temp, '.');
		if(start) {
			*start = '\0';
			sprintf(this->temp_string, "%s.ok.%s", temp, start+1);
		}
		else
			sprintf(this->temp_string, "%s.ok", temp);
	}

	sprintf(temp, "STOR %s\r\n", this->temp_string);
	if(!this->tcp.SendData(temp)) {
		this->error = E_CONTROL_RESET;
		this->PostBusy(NULL);
		this->PostStatusFile(file, "ABORT");
		return(FALSE);
	}
	
	if(!this->tcp.WaitForMessage()) {
		this->error = E_BAD_STOR;
		this->PostBusy(NULL);
		this->PostStatusFile(file, "ABORT");
		return(FALSE);
	}
	
	this->PostStatusFile(file, "STOR_OK");	// notify src
	if(!this->WaitForStatusFile(src_pid, file, "RETR_OK", port_msg)) {
		this->error = E_NO_ERROR;	// src prints error
		this->PostBusy(NULL);
		return(FALSE);
	}
	
	// GO!
	this->PostBusy(">FXP");	
	gettimeofday(&before, NULL);
	
	if(!this->tcp.WaitForMessage(0)) {
		this->error = E_PASV_FAILED;	// PASV transfer failed (src notices that on its own)
		this->PostBusy(NULL);
		return(FALSE);
	}

	this->AddEntryToCache(this->temp_string);

	gettimeofday(&after, NULL);
	size = atol(port_msg);
	speed = (after.tv_sec - before.tv_sec) * 1000000.0 + (after.tv_usec - before.tv_usec) + 1;
	speed = ((float)size / ((float)(speed) / 1000000.0)) / 1024;
	sprintf(this->temp_string, "[%s]: uploaded '%s' (%ld) at %4.02f kb/s", this->prefs.label, file, size, speed);
	display->PostStatusLine(this->temp_string, FALSE);

	this->PostBusy(NULL);
	return(TRUE);
}

bool CServer::FXPFileSrc(char *file, int dest_pid)
{
	char	*buffer, *start, *end, port_msg[32], temp[256];
	int	len;
	
	this->PostBusy("HSHK");
	this->tcp.FlushStack();
	
	// TYPE without error checking (shrugs)
	this->tcp.SendData("TYPE I\r\n");
	this->tcp.WaitForMessage();
	
	// send PASV, extract PORT info and post file, then wait (until DEST sent PORT and then STOR)
	if(!this->tcp.SendData("PASV\r\n")) {
		this->error = E_CONTROL_RESET;
		this->PostBusy(NULL);
		this->PostStatusFile(file, "ABORT");
		return(FALSE);
	}
	
	if(!this->tcp.WaitForMessage()) {
		this->error = E_BAD_PASV;
		this->PostBusy(NULL);
		this->PostStatusFile(file, "ABORT");
		return(FALSE);
	}
	
	buffer = this->tcp.GetControlBuffer();
	// extract PORT info
	start = strrchr(buffer, '(') + 1;
	end = strrchr(buffer, ')') - 1;
	strncpy(port_msg, start, end - start + 1);
	len = end - start;
	*(port_msg + len + 1) = '\0';

	this->PostStatusFile(file, "PASV_OK", port_msg);
	if(!this->WaitForStatusFile(dest_pid, file, "STOR_OK")) {
		this->error = E_NO_ERROR;	// error is printed by dest already
		this->PostBusy(NULL);
		return(FALSE);
	}
	
	// okay, dest had a successful PORT, and STOR accepted the file. lets try a RETR
	sprintf(temp, "RETR %s\r\n", file);
	if(!this->tcp.SendData(temp)) {
		this->error = E_CONTROL_RESET;
		this->PostBusy(NULL);
		this->PostStatusFile(file, "ABORT");
		return(FALSE);
	}
	
	if(!this->tcp.WaitForMessage()) {
		this->error = E_BAD_RETR;
		this->PostBusy(NULL);
		this->PostStatusFile(file, "ABORT");
		return(FALSE);
	}
	
	// extract file size (not correct if file is upped *shugs*)
	buffer = this->tcp.GetControlBuffer();
	start = strrchr(buffer, '(') + 1;
	end = strchr(start, ' ');
	strncpy(port_msg, start, end - start + 1);
	len = end - start;
	*(port_msg + len + 1) = '\0';

	this->PostStatusFile(file, "RETR_OK", port_msg);	// notify dest and GO!
	this->PostBusy("FXP>");	

	if(!this->tcp.WaitForMessage(0)) {
		this->error = E_NO_ERROR;	// PASV transfer failed, dest will print error (i hope)
		this->PostBusy(NULL);
		return(FALSE);
	}
	
	display->PostMessageFromServer(SERVER_MSG_DEMARK, this->magic, file);	

	this->PostBusy(NULL);
	return(TRUE);
}

bool CServer::UploadFile(char *file, bool no_wait, bool as_ok)
{
	char		*start, temp_name[256], port_str[256], name[SERVER_WORKINGDIR_SIZE];
	struct timeval	before, after;
	long		size;
	float		speed;
	
	this->PostBusy("STOR");
	this->tcp.FlushStack();
	
	sprintf(this->temp_string, "[%s]: uploading '%s'", this->prefs.label, file);
	display->PostStatusLine(this->temp_string, FALSE);
	
	if(!this->tcp.SendData("TYPE I\r\n")) {
		this->error = E_CONTROL_RESET;
		this->PostBusy(NULL);
		return(FALSE);
	}
	
	if(!this->tcp.WaitForMessage()) {
		this->error = E_BAD_TYPE;
		this->PostBusy(NULL);
		return(FALSE);
	}
	
/*	if(!this->tcp.SendData("MODE S\r\n")) {
		this->error = E_CONTROL_RESET;
		this->PostBusy(NULL);
		return(FALSE);
	}
	
	this->tcp.WaitForMessage();

*/	if(!this->tcp.OpenData(port_str, this->use_local)) {
		this->error = this->tcp.GetError();
		this->PostBusy(NULL);
		return(FALSE);
	}
	
	sprintf(this->temp_string, "PORT %s\r\n", port_str);
	if(!this->tcp.SendData(this->temp_string)) {
		this->error = E_CONTROL_RESET;
		this->PostBusy(NULL);
		return(FALSE);
	}
	
	if(!this->tcp.WaitForMessage()) {
		this->tcp.CloseData();
		this->error = E_BAD_PORT;
		this->PostBusy(NULL);
		return(FALSE);
	}
	
	if(!as_ok) {
		sprintf(this->temp_string, "STOR %s\r\n", file);
		this->AddEntryToCache(file);
	}
	else {
		strcpy(temp_name, file);
		start = strrchr(temp_name, '.');
		if(start) {
			*start = '\0';
			sprintf(this->temp_string, "%s.ok.%s", temp_name, start+1);
			this->AddEntryToCache(this->temp_string);
			sprintf(this->temp_string, "STOR %s.ok.%s\r\n", temp_name, start+1);
		}
		else {
			sprintf(this->temp_string, "%s.ok", temp_name);
			this->AddEntryToCache(this->temp_string);
			sprintf(this->temp_string, "STOR %s.ok\r\n", temp_name);
		}
	}
	
	if(!this->tcp.SendData(this->temp_string)) {
		this->error = E_CONTROL_RESET;
		this->PostBusy(NULL);
		return(FALSE);
	}
	
	if(!this->tcp.WaitForMessage()) {
		this->tcp.CloseData();
		this->error = E_BAD_STOR;
		this->PostBusy(NULL);
		return(FALSE);
	}

	if(!this->tcp.AcceptData()) {
		this->error = this->tcp.GetError();
		this->PostBusy(NULL);
		return(FALSE);
	}

	// build full dirname from local filesys
	global_server->server->ObtainWorkingDir(name);
	sprintf(this->temp_string, "%s/%s", name, file);
	
	if(!this->tcp.WriteFile(this->temp_string, no_wait)) {
		this->error = this->tcp.GetError();
		this->tcp.CloseData();
		this->tcp.WaitForMessage();
		this->PostBusy(NULL);
		return(FALSE);
	}
		
	this->tcp.CloseData();

	if(!this->tcp.WaitForMessage()) {
		this->error = this->tcp.GetError();
		this->PostBusy(NULL);
		return(FALSE);
	}
	
	this->tcp.GetTimevals(&before, &after, &size);
	speed = (after.tv_sec - before.tv_sec) * 1000000.0 + (after.tv_usec - before.tv_usec) + 1;
	speed = ((float)size / ((float)(speed) / 1000000.0)) / 1024;
	sprintf(this->temp_string, "[%s]: uploaded '%s' (%ld) at %4.02f kb/s", this->prefs.label, file, size, speed);
	display->PostStatusLine(this->temp_string, FALSE);

	this->PostBusy(NULL);
	return(TRUE);
}

bool CServer::CheckStealth(char *filename)
{
	char	buffer[256];
	FILE	*file;
	
	if((file = fopen(filename, "r"))) {
		fgets(buffer, 255, file);
		fclose(file);

		if(strstr(buffer, "total "))
			return(TRUE);
		else {
			sprintf(this->temp_string, "[%s]: unable to enable stealth mode, using std. mode", this->prefs.label);
			display->PostStatusLine(this->temp_string, TRUE);
			remove(filename);
			return(FALSE);
		}
	}

	sprintf(this->temp_string, "[%s]: unable to enable stealth mode, using std. mode", this->prefs.label);
	display->PostStatusLine(this->temp_string, TRUE);
	return(FALSE);
}

bool CServer::RefreshFiles(void)
{
	char	port_str[256], temp_file[256];
	
	if(!this->use_stealth) {
		if(ff_enabled)
			this->PostBusy("-tra");
		else
			this->PostBusy("LIST");
	}
	else
		this->PostBusy("STLT");
		
	this->tcp.FlushStack();
	
	pthread_mutex_lock(&syscall_lock);
	tmpnam(temp_file);
	pthread_mutex_unlock(&syscall_lock);
	
	if(this->use_stealth) {
		// stealth mode
		if(!this->tcp.SendData("STAT -la\r\n")) {
			this->error = E_CONTROL_RESET;
			this->PostBusy(NULL);
			return(FALSE);
		}
	
		this->tcp.SetStealth(TRUE, temp_file);
		
		if(!this->tcp.WaitForMessage()) {
			this->tcp.SetStealth(FALSE, temp_file);
			this->error = E_BAD_STEALTH;
			this->PostBusy(NULL);
			return(FALSE);
		}	

		this->tcp.SetStealth(FALSE, temp_file);

		this->use_stealth = this->CheckStealth(temp_file);
	}

	if(!this->use_stealth) {
		if(!this->tcp.SendData("TYPE A\r\n")) {
			this->error = E_CONTROL_RESET;
			this->PostBusy(NULL);
			return(FALSE);
		}
	
		if(!this->tcp.WaitForMessage()) {
			this->error = E_BAD_TYPE;
			this->PostBusy(NULL);
			return(FALSE);
		}
	
		if(!this->tcp.OpenData(port_str, this->use_local)) {
			this->error = this->tcp.GetError();
			this->PostBusy(NULL);
			return(FALSE);
		}
	
		sprintf(this->temp_string, "PORT %s\r\n", port_str);
		if(!this->tcp.SendData(this->temp_string)) {
			this->error = E_CONTROL_RESET;
			this->PostBusy(NULL);
			return(FALSE);
		}
	
		if(!this->tcp.WaitForMessage()) {
			this->tcp.CloseData();
			this->error = E_BAD_PORT;
			this->PostBusy(NULL);
			return(FALSE);
		}
	
		strcpy(this->temp_string, "LIST -");
		if(ff_enabled)
			strcat(this->temp_string, "lrt\r\n");
		else
			strcat(this->temp_string, "la\r\n");
			
		if(!this->tcp.SendData(this->temp_string)) {
			this->error = E_CONTROL_RESET;
			this->PostBusy(NULL);
			return(FALSE);
		}
			
		if(!this->tcp.WaitForMessage()) {
			this->tcp.CloseData();
			this->error = E_BAD_LIST;
			this->PostBusy(NULL);
			return(FALSE);
		}

		if(!this->tcp.AcceptData()) {
			this->error = this->tcp.GetError();
			this->PostBusy(NULL);
			return(FALSE);
		}
	}
	
	if(!this->use_stealth) {
		if(!this->tcp.ReadFile(temp_file, -1)) {
			this->tcp.CloseData();
			this->error = this->tcp.GetError();
			remove(temp_file);
			this->PostBusy(NULL);
			return(FALSE);
		}
	
		this->tcp.CloseData();

		if(!this->tcp.WaitForMessage()) {
			this->error = this->tcp.GetError();
			remove(temp_file);
			this->PostBusy(NULL);
			return(FALSE);
		}
	}
	
	this->FormatFilelist(temp_file);
	this->PostBusy(NULL);
	return(TRUE);
}

void CServer::FormatFilelist(char *filename)
{
	FILE		*file_in;
	FILELIST	*fl_temp, *fl_new;
	char		*start, name[512], dummy[12], date1[10], date2[10], date3[10];
	int		magic = 0, n, blocks;
	char		*start_fix, *end_fix, month_name[][5] = {"Jan ", "Feb ", "Mar ", "Apr ", "May ", "Jun ", "Jul ", "Aug ", "Sep ", "Oct ", "Nov ", "Dec "};
	bool		use_fix, space;
	
	if((file_in = fopen(filename, "r"))) {
		// build up new list
		fl_temp = NULL;

		do {
			fgets(this->temp_string, 511, file_in);
			if(!feof(file_in)) {
				this->tcp.StripANSI(this->temp_string);
				
				if(strncmp(this->temp_string, "total", 5)) {
					fl_new = new(FILELIST);
					fl_new->next = NULL;

					// determine if we have all the needed fields
					// this one is tricky. first we need to determine the beginning of the date, since this is
					// the only stop-point that keeps beeing the same for all sites/dirs/dates/...
					// we cant rely on counting spaces since it could be a symlink or a file with a space
					n = 0;
					end_fix = NULL;
					use_fix = 0;
					while(!end_fix && (n < 12)) {
						end_fix = strstr(this->temp_string, month_name[n]);
						n++;
					}
					
					if(end_fix) {
						// now we have the beginning of the date
						// here we can count blocks to see if we have groups or not
						start_fix = this->temp_string;
						space = TRUE;
						blocks = 0;
						while(start_fix < end_fix) {
							if(space) {
								// look for a space
								if(*start_fix == ' ') {
									blocks++;
									space = !space; 
								}
							}
							else {
								// look for a non-space
								if(*start_fix != ' ') {
									space = !space;
								}
							}
							start_fix += 1;
						}
						
						if(blocks < 5) {
							if(blocks < 4)
								use_fix = 2;	// not even groups
							else
								use_fix = 1;	// just no user
						}
					}
					// else ... humm? what?? strange... well, we'll segfault soon then
					
					// break line into correct parts
					if(!use_fix) {
						if(this->temp_string[0] != 'l') {
							sscanf(this->temp_string, "%s %s %s %s %ld %s %s %s", fl_new->mode, dummy, fl_new->owner, dummy, &(fl_new->size), date1, date2, date3);
							start = strstr(this->temp_string, date3);
							start += strlen(date3) + 1;
							strcpy(name, start);
							if(strrchr(name, '\r'))
								*strrchr(name, '\r') = '\0';
							if(strrchr(name, '\n'))
								*strrchr(name, '\n') = '\0';
							
							fl_new->name = new(char[strlen(name) + 1]);
							strcpy(fl_new->name, name);
					
							if(this->temp_string[0] == 'd')
								fl_new->is_dir = TRUE;
							else
								fl_new->is_dir = FALSE;

							if(strlen(date2) > 1)
								sprintf(fl_new->date, "%s %s %s", date1, date2, date3);
							else
								sprintf(fl_new->date, "%s  %s %s", date1, date2, date3);
							fl_new->magic = magic;
						}
						else {
							sscanf(this->temp_string, "%s %s %s %s %ld %s %s %s", fl_new->mode, dummy, fl_new->owner, dummy, &(fl_new->size), date1, date2, date3);
							start = strstr(this->temp_string, " -> ");
							if(start)
								*start = '\0';
							
							start = strstr(this->temp_string, date3);
							start += strlen(date3) + 1;
							strcpy(name, start);
							if(strrchr(name, '\r'))
								*strrchr(name, '\r') = '\0';
							if(strrchr(name, '\n'))
								*strrchr(name, '\n') = '\0';
							
							fl_new->name = new(char[strlen(name) + 1]);
							strcpy(fl_new->name, name);
					
							if(this->temp_string[0] == 'l')
								fl_new->is_dir = TRUE;
							else
								fl_new->is_dir = FALSE;

							if(strlen(date2) > 1)
								sprintf(fl_new->date, "%s %s %s", date1, date2, date3);
							else
								sprintf(fl_new->date, "%s  %s %s", date1, date2, date3);
							fl_new->magic = magic;
						}
					}
					else if(use_fix == 1) {
						// use fix for sitez like STH (gftpd)
						if(this->temp_string[0] != 'l') {
							sscanf(this->temp_string, "%s %s %s %ld %s %s %s", fl_new->mode, dummy, fl_new->owner, &(fl_new->size), date1, date2, date3);
							start = strstr(this->temp_string, date3);
							start += strlen(date3) + 1;
							strcpy(name, start);
							if(strrchr(name, '\r'))
								*strrchr(name, '\r') = '\0';
							if(strrchr(name, '\n'))
								*strrchr(name, '\n') = '\0';
							
							fl_new->name = new(char[strlen(name) + 1]);
							strcpy(fl_new->name, name);
					
							if(this->temp_string[0] == 'd')
								fl_new->is_dir = TRUE;
							else
								fl_new->is_dir = FALSE;

							if(strlen(date2) > 1)
								sprintf(fl_new->date, "%s %s %s", date1, date2, date3);
							else
								sprintf(fl_new->date, "%s  %s %s", date1, date2, date3);
							fl_new->magic = magic;
						}
						else {
							sscanf(this->temp_string, "%s %s %s %ld %s %s %s", fl_new->mode, dummy, fl_new->owner, &(fl_new->size), dummy, dummy, dummy);
							start = strstr(this->temp_string, " -> ");
							if(start)
								*start = '\0';
							
							start = strstr(this->temp_string, dummy);
							start += strlen(dummy) + 1;
							strcpy(name, start);
							if(strrchr(name, '\r'))
								*strrchr(name, '\r') = '\0';
							if(strrchr(name, '\n'))
								*strrchr(name, '\n') = '\0';
							
							fl_new->name = new(char[strlen(name) + 1]);
							strcpy(fl_new->name, name);
					
							if(this->temp_string[0] == 'l')
								fl_new->is_dir = TRUE;
							else
								fl_new->is_dir = FALSE;

							if(strlen(date2) > 1)
								sprintf(fl_new->date, "%s %s %s", date1, date2, date3);
							else
								sprintf(fl_new->date, "%s  %s %s", date1, date2, date3);
							fl_new->magic = magic;
						}
					}
					else {
						// use fix for entries with no user, no group (FUCK YOU)
						strcpy(fl_new->owner, "none");
						
						if(this->temp_string[0] != 'l') {
							sscanf(this->temp_string, "%s %s %ld %s %s %s", fl_new->mode, dummy, &(fl_new->size), date1, date2, date3);
							start = strstr(this->temp_string, date3);
							start += strlen(date3) + 1;
							strcpy(name, start);
							if(strrchr(name, '\r'))
								*strrchr(name, '\r') = '\0';
							if(strrchr(name, '\n'))
								*strrchr(name, '\n') = '\0';
							
							fl_new->name = new(char[strlen(name) + 1]);
							strcpy(fl_new->name, name);
					
							if(this->temp_string[0] == 'd')
								fl_new->is_dir = TRUE;
							else
								fl_new->is_dir = FALSE;

							if(strlen(date2) > 1)
								sprintf(fl_new->date, "%s %s %s", date1, date2, date3);
							else
								sprintf(fl_new->date, "%s  %s %s", date1, date2, date3);
							fl_new->magic = magic;
						}
						else {
							sscanf(this->temp_string, "%s %s %ld %s %s %s", fl_new->mode, dummy, &(fl_new->size), dummy, dummy, dummy);
							start = strstr(this->temp_string, " -> ");
							if(start)
								*start = '\0';
							
							start = strstr(this->temp_string, dummy);
							start += strlen(dummy) + 1;
							strcpy(name, start);
							if(strrchr(name, '\r'))
								*strrchr(name, '\r') = '\0';
							if(strrchr(name, '\n'))
								*strrchr(name, '\n') = '\0';
							
							fl_new->name = new(char[strlen(name) + 1]);
							strcpy(fl_new->name, name);
					
							if(this->temp_string[0] == 'l')
								fl_new->is_dir = TRUE;
							else
								fl_new->is_dir = FALSE;

							if(strlen(date2) > 1)
								sprintf(fl_new->date, "%s %s %s", date1, date2, date3);
							else
								sprintf(fl_new->date, "%s  %s %s", date1, date2, date3);
							fl_new->magic = magic;
						}
					}
					
					if(strcmp(fl_new->name, ".") && strcmp(fl_new->name, "..")) {
						magic++;
				
						if(fl_temp)
							fl_temp->next = fl_new;
						else
							this->internal_filelist = fl_new;
		
						fl_temp = fl_new;
					}
					else {
						delete(fl_new->name);
						delete(fl_new);
					}
				}
			}
		} while(!feof(file_in));
		
		fclose(file_in);
		// determine changings
		if(this->prefs.use_track)
			this->UseDirCache();

		this->SortFilelist(TRUE);
		this->PostToDisplay(SERVER_MSG_NEW_FILELIST);
	}
	remove(filename);
}

void CServer::AddEntryToCache(char *entry)
{
	DIRCACHE	*dc_temp = this->dir_cache;
	CACHELIST	*cl_new;
	bool		found = FALSE;
	
	// now lets see if we already have a list corresponding to our actual working dir
	while(!found && dc_temp) {
		if(!strcmp(dc_temp->dirname, this->working_dir))
			found = TRUE;
		else
			dc_temp = dc_temp->next;
	}

	if(found) {
		// add our entry
		cl_new = new(CACHELIST);
		cl_new->next = dc_temp->cachelist;
		cl_new->name = new(char[strlen(entry) + 1]);
		strcpy(cl_new->name, entry);
		dc_temp->cachelist = cl_new;
	}
}

void CServer::UseDirCache(void)
{
	FILELIST	*fl_temp;
	DIRCACHE	*dc_temp1 = NULL, *dc_temp = this->dir_cache;
	CACHELIST	*cl_temp, *cl_temp1;
	bool		found = FALSE;
	int		n;
		
	// now lets see if we already have a list corresponding to our actual working dir
	while(!found && dc_temp) {
		if(!strcmp(dc_temp->dirname, this->working_dir))
			found = TRUE;
		else {
			dc_temp1 = dc_temp;
			dc_temp = dc_temp->next;
		}
	}

	if(found) {
		// okiez, let's see if we have new files/dirs
		fl_temp = this->internal_filelist;
		while(fl_temp) {
			cl_temp = dc_temp->cachelist;
			found = FALSE;
			while(!found && cl_temp) {
				if(!strcmp(fl_temp->name, cl_temp->name))
					found = TRUE;
				else
					cl_temp = cl_temp->next;
			}

			if(!found) {
				if(this->prefs.exclude)
					found = FilterFilename(fl_temp->name, this->prefs.exclude);
				else
					found = FilterFilename(fl_temp->name, NULL);
				
				if(found) {
					if(fl_temp->is_dir)
						sprintf(this->temp_string, "[%s]: -dir- %s (%s)", this->prefs.label, fl_temp->name, fl_temp->owner);
					else
						sprintf(this->temp_string, "[%s]: -file- %s (%s)", this->prefs.label, fl_temp->name, fl_temp->owner);
					
					display->PostStatusLine(this->temp_string, TRUE);
					beep();
				}
			}
			fl_temp = fl_temp->next;
		}
		
		// free cached list
		if(!dc_temp1)
			this->dir_cache = dc_temp->next;
		else
			dc_temp1->next = dc_temp->next;
		
		cl_temp = dc_temp->cachelist;
		while(cl_temp) {
			cl_temp1 = cl_temp;
			cl_temp = cl_temp->next;
			delete(cl_temp1->name);
			delete(cl_temp1);
		}
		
		delete(dc_temp->dirname);
		delete(dc_temp);
	}

	// make our list as the new one (if we are allowed, depending on cache size)
	dc_temp1 = new(DIRCACHE);
	dc_temp1->dirname = new(char[strlen(this->working_dir) + 1]);
	strcpy(dc_temp1->dirname, this->working_dir);
	dc_temp1->cachelist = NULL;
	dc_temp1->next = this->dir_cache;
		
	fl_temp = this->internal_filelist;
	cl_temp1 = NULL;
	while(fl_temp) {
		cl_temp = new(CACHELIST);
		cl_temp->name = new(char[strlen(fl_temp->name) + 1]);
		strcpy(cl_temp->name, fl_temp->name);
		cl_temp->next = NULL;
			
		if(cl_temp1)
			cl_temp1->next = cl_temp;
		else
			dc_temp1->cachelist = cl_temp;
			
		cl_temp1 = cl_temp;
		fl_temp = fl_temp->next;
	}
		
	this->dir_cache = dc_temp1;

	// see if we should strip the oldest dircache
	dc_temp = this->dir_cache;
	n = 0;
	while(dc_temp) {
		n++;
		dc_temp = dc_temp->next;
	}
	if(n >= SERVER_DIR_CACHE) {		// okay, kill the last list
		dc_temp = this->dir_cache;
		dc_temp1 = NULL;
		while(dc_temp->next) {
			dc_temp1 = dc_temp;
			dc_temp = dc_temp->next;
		}
		
		dc_temp1->next = NULL;
		delete(dc_temp->dirname);
		cl_temp = dc_temp->cachelist;
		while(cl_temp) {
			cl_temp1 = cl_temp;
			cl_temp = cl_temp->next;
			delete(cl_temp1->name);
			delete(cl_temp1);
		}
	}
}

void CServer::SortFilelist(bool add_root)
{
	bool		found, finished = FALSE;
	int		m, year;
	time_t		elapsed_time;
	struct tm	file_time;
	char		tmp[32], month_name[][4] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
	FILELIST	*fl_temp, *fl_temp1, *fl_new, *fl_temp2, *fl_best, *fl_best_before;
	int		magic;
	
	pthread_mutex_lock(&(this->filelist_lock));

	// free old filelist
	fl_temp = this->actual_filelist;
	while(fl_temp) {
		fl_temp1 = fl_temp;
		fl_temp = fl_temp->next;
		delete(fl_temp1->name);
		delete(fl_temp1);
	}
	fl_temp = this->actual_filelist = NULL;

	// first calc the times for all entries (used even if no chronosort is needed for newest files/dirs)
	elapsed_time = ::time(NULL);
	year = localtime(&elapsed_time)->tm_year;

	fl_temp = this->internal_filelist;
	while(fl_temp) {
		file_time.tm_sec = file_time.tm_min = file_time.tm_hour = file_time.tm_isdst = 0;
		file_time.tm_year = year;

		// month
		strncpy(tmp, fl_temp->date, 3);
		tmp[3] = '\0';
		m = 0;
		found = FALSE;

		while(!found && (m < 12)) {
			if(!strcasecmp(tmp, month_name[m]))
				found = TRUE;
			else
				m++;
		}
		file_time.tm_mon = m;
			
		// day
		strncpy(tmp, fl_temp->date + 4, 2);
		tmp[2] = '\0';
		file_time.tm_mday = atoi(tmp);

		// hour & min or year
		strncpy(tmp, fl_temp->date + 7, 5);
		tmp[5] = '\0';
	
		if(strchr(tmp, ':')) {
			// k, was a valid time
			tmp[2] = '\0';
			file_time.tm_hour = atoi(tmp);
			file_time.tm_min = atoi(tmp + 3);
		}
		else
			file_time.tm_year = atoi(tmp + 1);

		// generate overall seconds
		fl_temp->time = mktime(&file_time);

		fl_temp = fl_temp->next;
	}
	
	// now to the real sorting
	
	fl_new = new(FILELIST);
	fl_new->next = NULL;
	fl_new->name = new(char[3]);
	strcpy(fl_new->name, "..");
	fl_new->is_dir = TRUE;
	fl_new->size = 0;
	strcpy(fl_new->owner, "<none>");
	strcpy(fl_new->mode, "<none>    ");
	strcpy(fl_new->date, "            ");
	fl_new->time = 0;
	this->actual_filelist = fl_temp = fl_new;
	
	if(!add_root) {
		fl_temp1 = this->internal_filelist;
		this->internal_filelist = this->internal_filelist->next;
		delete(fl_temp1->name);
		delete(fl_temp1);
	}
	
	if(this->internal_filelist) {
		while(!finished) {
			// walk through list in search of the newest/alpha-smallest entry
			fl_temp1 = this->internal_filelist;
			fl_best = fl_temp1;
			fl_best_before = fl_temp2 = NULL;
		
			while(fl_temp1) {
				if(this->alpha_sort) {
					if(strcmp(fl_temp1->name, fl_best->name) < 0) {
						fl_best_before = fl_temp2;
						fl_best = fl_temp1;
					}
				}
				else {
					if(fl_temp1->time > fl_best->time) {
						fl_best_before = fl_temp2;
						fl_best = fl_temp1;
					}
					else if(fl_temp1->time == fl_best->time) {
						if(strcmp(fl_temp1->name, fl_best->name) < 0) {
							fl_best_before = fl_temp2;
							fl_best = fl_temp1;
						}
					}
				}
			
				fl_temp2 = fl_temp1;
				fl_temp1 = fl_temp1->next;
			}
		
			// fl_best now points to our best entry, add to new list
			fl_new = new(FILELIST);
			fl_new->next = NULL;
			fl_new->name = new(char[strlen(fl_best->name) + 1]);
			strcpy(fl_new->name, fl_best->name);
			fl_new->size = fl_best->size;
			fl_new->is_dir = fl_best->is_dir;
			strcpy(fl_new->owner, fl_best->owner);
			strcpy(fl_new->mode, fl_best->mode);
			strcpy(fl_new->date, fl_best->date);
			fl_new->time = fl_best->time;
		
			fl_temp->next = fl_new;
			fl_temp = fl_new;
		
			// kill the best entry from old list
			if(fl_best_before)
				fl_best_before->next = fl_best->next;
			else
				this->internal_filelist = fl_best->next;
		
			delete(fl_best->name);
			delete(fl_best);

			// see if we are finished
			if(this->internal_filelist == NULL)
				finished = TRUE;
		}
	}
		
	// now the actual_filelist is alpha/chrono sorted.
	// lets pick the dirs and move them to front
	fl_temp = this->actual_filelist->next;		// first entry should be ".." !
	fl_temp1 = this->actual_filelist;
	fl_temp2 = this->actual_filelist;
	
	while(fl_temp) {
		if(fl_temp->is_dir) {
			// k, we found a dir. unlink the entry from it's original position
			fl_temp1->next = fl_temp->next;
			
			// now link after the last known directory
			fl_best = fl_temp2->next;
			fl_temp2->next = fl_temp;
			fl_temp->next = fl_best;
			
			// remember that position
			fl_temp2 = fl_temp;

		}
		fl_temp1 = fl_temp;
		fl_temp = fl_temp->next;
	}

	// fixup magics
	fl_temp = this->actual_filelist;
	magic = 0;
	while(fl_temp) {
		fl_temp->magic = magic;
		magic++;
		fl_temp = fl_temp->next;
	}
	
	// delete internal list
	fl_temp = this->internal_filelist;
	while(fl_temp) {
		fl_temp1 = fl_temp;
		fl_temp = fl_temp->next;
		delete(fl_temp1->name);
		delete(fl_temp1);
	}
	this->internal_filelist = NULL;
	
	pthread_mutex_unlock(&(this->filelist_lock));
}
