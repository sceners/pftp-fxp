#ifndef __SERVER_H
#define	__SERVER_H

typedef struct _FILELIST
{
	char			*name;
	char			owner[9], mode[11], date[13];
	unsigned long		size;
	time_t			time;
	bool			is_dir, is_marked;
	int			magic;
	struct _FILELIST	*next;
} FILELIST;

class CServer {
private:
	int		magic, bm_magic, server_type, error, noop_slept, refresh_slept, pid;
	bool		have_undo, use_local;
	char		*busy, *param, working_dir[SERVER_WORKINGDIR_SIZE], undo_cwd[SERVER_WORKINGDIR_SIZE], undo_mkd[256], rename_temp[INPUT_TEMP_MAX], temp_string[512];
	pthread_t	thread;
	pthread_mutex_t	busy_lock, cwd_lock, filelist_lock, displaymsg_lock;
	CTCP		tcp;
	FILELIST	*actual_filelist, *internal_filelist;
	BOOKMARK	prefs;
	DIRCACHE	*dir_cache;
	ACTION_MSG_LIST	*display_msg_stack;
	bool		urgent, is_busy, alpha_sort, use_stealth;
		
	void		PostBusy(char *);
	
	bool		LocalChangeWorkingDir(char *dir);
	void		LocalGetWorkingDir(void);
	void		LocalGetDirlist(void);
	void		LocalMakeDir(char *, bool);
	void		LocalDeleteFile(char *);
	void		LocalDeleteDir(char *);
	void		LocalRenFrom(char *);
	void		LocalRenTo(char *);
	void		PostToDisplay(int msg);

	void		HandleMessage(int msg, char *param, int magic);
	void		EvalError(void);
	void		SortFilelist(bool);

	void		PostStatusFile(char *, char *);
	void		PostStatusFile(char *, char *, char *);
	bool		WaitForStatusFile(int, char *, char *);
	bool		WaitForStatusFile(int, char *, char *, char *);
	
	// remote actions
	void		KillMe(bool);
	bool		Login(void);
	bool		Noop(void);
	bool		ChangeWorkingDir(char *dir);
	bool		GetWorkingDir(void);
	bool		MakeDir(char *, bool, bool);
	bool		DeleteFile(char *);
	bool		DeleteDir(char *);
	void		RenFrom(char *);
	bool		RenTo(char *);
	bool		SendSITE(char *);
	void		FormatFilelist(char *);
	void		UseDirCache(void);
	bool		RefreshFiles(void);
	bool		LeechFile(char *, int, bool, int);
	bool		LeechDir(char *, int, bool, int);
	bool		FXPFileSrc(char *, int);
	bool		FXPFileDest(char *, int, bool);
	bool		UploadFile(char *, bool, bool);
	void		UploadDirStart(char *);
	bool		UploadDir(char *, bool, bool);
	void		AddEntryToCache(char *);
	bool		CheckStealth(char *);
		
public:
	CServer();
	~CServer();

	void		SetMagic(int magic) {this->magic = magic;};
	int		GetMagic(void) {return(this->magic);};
	void		SetServerType(int type) {this->server_type = type;};
	int		GetServerType(void) {return(this->server_type);};
	void		SetServerPrefs(BOOKMARK *bm);
	BOOKMARK	*GetPrefs(void) {return(&(this->prefs));};
	char		*GetSiteAlias(int);
	bool		GetChaining(void) {return(this->prefs.use_chaining);};
	char		*GetFilter(void);
	CTCP		*GetTCP(void) {return(&(this->tcp));};
	pthread_t	*GetThreadAddress(void) {return(&(this->thread));};
	pthread_t	GetThread(void) {return(this->thread);};
	void		Run(void);
	void		ObtainWorkingDir(char *);
	FILELIST	*ObtainFilelist(bool *use_jump);
	char		*ObtainBusy(void);
	void		PostFromDisplay(int msg, char *param);
	void		PostUrgentFromDisplay(int msg, char *param);
	void		PostFromDisplay(int msg, char *param, int magic);
	char		*GetAlias(void) {return(this->prefs.label);};
	void		SetBMMagic(int bmm) {this->bm_magic = bmm;};
	int		GetBMMagic(void) {return(this->bm_magic);};
	int		GetPID(void) {return(this->pid);};
	float		GetSpeed(void) {return(this->tcp.GetSpeed());};
	bool		IsBusy(void) {return(this->is_busy);};
};

typedef struct _SERVERLIST
{
	CServer			*server;
	int			magic;
	struct _SERVERLIST	*next;
} SERVERLIST;

#endif
