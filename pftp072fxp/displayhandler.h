#ifndef	__DISPLAYHANDLER_H
#define	__DISPLAYHANDLER_H

class CDisplayHandler {
private:
	bool		thread_attr_init;
	bool		thread_running, default_windows, bm_save;
	int		inverse_mono, terminal_max_y, terminal_max_x, status_win_size;
	int		window_left_width, window_right_width;
	int		leftwindow_magic, rightwindow_magic;
	int		jump_stack_left[JUMP_STACK], jump_stack_right[JUMP_STACK], jump_stack_left_pos, jump_stack_right_pos;
	char		password[PASSWORD_SIZE], custom_password[PASSWORD_SIZE], temp_string[512], *statuslog[STATUS_LOG];
	char		input_temp[INPUT_TEMP_MAX], old_input_temp[INPUT_TEMP_MAX];
	float		speed_left, speed_left_old, speed_right, speed_right_old;
	
	bool		statuslog_highlight[STATUS_LOG];
	MSG_LIST	*message_stack;
	SERVER_MSG_LIST	*server_message_stack;
	STATUS_MSG_LIST	*status_msg_stack;
	char		*leftwindow_busy, *rightwindow_busy, *log[LOG_LINES], window_left_cwd[SERVER_WORKINGDIR_SIZE], window_right_cwd[SERVER_WORKINGDIR_SIZE], window_left_label[ALIAS_MAX+5], window_right_label[ALIAS_MAX+5];
	
	FILELIST	*filelist_left, *filelist_right;
	int		filelist_left_magic, filelist_right_magic, filelist_left_ypos, filelist_right_ypos;
	int		filelist_left_entries, filelist_right_entries;
	int		xsite_buttonstate;
	bool		filelist_left_format, filelist_right_format;
	WINDOW		*window_welcome, *window_switch, *window_log, *window_tab, *window_input, *window_notice, *window_prefs, *window_dialog, *window_probe, *window_status, *window_command, *window_left, *window_right;
		
	void		InitColors(void);
	void		DetermineScreenSize(void);
	void		OpenDefaultWindows(void);
	void		DrawCommandKeys(char *, int);
	void		RebuildScreen(void);
	void		HandleMessage(int msg, int extended);
	void		HandleServerMessage(int msg, int magic, char *data);
	void		KillServer(CServer *server);
		
	// update routines
	char		*GetFilelistString(FILELIST *, int, int, bool *, bool);
	FILELIST	*GetFilelistEntry(FILELIST *, int);
	void		UpdateServerCWD(CServer *, WINDOW *window);
	void		UpdateServerFilelist(CServer *, WINDOW *window);
	void		UpdateTabBar(bool dont_remove);
	void		UpdateFilelistNewPosition(WINDOW *window);
	void		UpdateFilelistScroll(bool dir_up, bool);
	void		UpdateFilelistPageMove(bool dir_up);
	void		UpdateServerBusy(WINDOW *, char *);
	void		FetchBusy(CServer *, WINDOW *);
	void		FileToggleMark(void);
	void		UpdateXSiteButtons(void);
	void		UpdateMode(void);
	void		LeechOkayNowUpload(int, char *, bool, bool);
	void		DirOkayNowMKD(int magic, char *dir, bool single);
	void		DirOkayNowCWD(int magic, char *dir, bool single);
	void		TransferOkayNowRefresh(int, bool);
	void		ResetTimer(void);
	void		UpdateSpeed(bool left_win, bool force);
	void		DrawSpeed(bool left_win, bool force);
	void		ChangeSorting(void);
		
	// view window
	int		view_line, view_lines_max;
	char		view_buffer[VIEW_BUFFER_MAX], view_filename[SERVER_WORKINGDIR_SIZE + 64];
	
	void		NoticeViewFile(char *);
	void		ViewFile(void);
	bool		OpenView(void);
	void		CloseView(void);
	void		RefreshView(void);
	
	// switch window
	int		switch_pos, switch_count, switch_start;
	
	void		OpenSwitchWindow(void);
	void		CloseSwitchWindow(void);
	void		ScrollSwitch(bool);
	void		PageMoveSwitch(bool);
	void		RedrawSwitchWindow(void);
	void		ScrollView(bool);
	void		PageMoveView(bool);
		
	// log window
	int		log_start;
	
	CServer		*TryOpenLog(void);
	void		OpenLog(CServer *server);
	void		ScrollLog(bool dir_up);
	void		PageMoveLog(bool dir_up);
	void		RefreshLog(void);
	void		CloseLog(void);
	
	// prefs dialog
	char		alias[ALIAS_MAX], hostname[HOSTNAME_MAX], username[USERNAME_MAX], pass[PASSWORD_MAX];
	char		startdir[STARTDIR_MAX], exclude[EXCLUDE_MAX], util_dir[GAMEUTILDIR_MAX], game_dir[GAMEUTILDIR_MAX], site_who[SITE_MAX], site_user[SITE_MAX], site_wkup[SITE_MAX], num_string[10];
	int		port, refresh_rate, noop_rate;
	bool		use_refresh, use_noop, use_startdir, use_exclude, use_autologin, use_chaining, use_jump, use_track, use_utilgames;
	int		prefsdialog_buttonstate, prefs_inputtype;
	CServer		*siteprefs_server;
	
	void		OpenPrefsDialog(void);
	void		ClosePrefsDialog(void);
	void		UpdatePrefsItems(void);

	void		FillInfoForPrefs(void);
	void		PrefsAddSite(void);
	void		PrefsModifySite(void);
	void		UpdateSitePrefs(CServer *);
	CServer		*TryPrefs(int *);
				
	// siteopen dialog
	int		siteopen_buttonstate;
	
	void		OpenSiteDialog(void);
	void		CloseSiteDialog(void);
	void		UpdateSiteOpenButtons();

	// bookmark-selection in opensite
	int		siteopen_bm_magic, siteopen_bm_startmagic, siteopen_bm_realmagic;
	
	void		ScrollBookmarkSites(bool dir_up);
	void		PageMoveBookmarkSites(bool dir_up);
	void		WipeBookmark(void);
	void		RedrawBookmarkSites(void);
	
	// status log
	int		status_line;
	
	void		ScrollStatusUp(void);
	void		ScrollStatusDown(void);
	void		DisplayStatusLog(void);
	
	// misc
	void		ShowWelcome(void);
	void		HideWelcome(void);

	void		PostUrgent(int, int, char *);
	void		AutoLogin(void);
	void		GetSiteAlias(int);
	int		TrySite(WINDOW *);	
	void		MarkFiles(bool);
	bool		NamesEqual(char *, char *);
	int		DoCompare(FILELIST *, FILELIST *);
	void		CompareFiles(void);
	void		UtilGame(bool);
	void		RefreshSite(bool);
	void		DeleteFile(void);
	void		TransferFile(bool);
	void		PrepDir(int);
	void		WipePrep(void);
	void		ChangeDir(int);
	void		DeMark(WINDOW *, char *);
	
public:
	void		AddStatusLine(char *, bool);

	// bookmark stuff
public:
	bool		ProbeBookmarkRC(void);

	void		SaveBookmarks(char *path);

private:
	int		input_chars, input_maxchars, input_cursorpos, pass_pos;
	bool		input_hidden;
	char		*input, hidden_input[512];
		
	void		NoticeNoBookmark(void);
	void		DialogNotice(char *, char *);
	void		CloseNotice(void);
	void		DialogInput(char *, char *, int, bool);
	void		DialogXSite(void);
	void		CloseSiteInput(void);
	void		CloseInput(void);
	void		InputAppendChar(char);
	void		InputBackspace(void);
	void		UpdateInput(void);
	
	void		OpenPasswordInput(bool hidden);
	void		NoticeNoPasswd(void);
	void		NoticeBadPasswd(void);
	void		NoticeNoMatch(void);
	bool		ReadBookmarks(void);
	void		Decrypt(char *in, char *out);
	void		Encrypt(char *in, char *out);

	// fireup stiff
	void		FireupLocalFilesys(void);
	int		FireupServer(char *);
	
	// to server
	void		PostToServer(int, bool, int);

	void		FreeWindow(WINDOW *);
	void		PerformSwitch(void);
	void		CloseSite(void);
				
public:
	int			internal_state, internal_state_previous;
	pthread_t		thread;
	pthread_attr_t		thread_attr;
	pthread_mutex_t		message_lock, server_message_lock, status_lock;
	
	CDisplayHandler();
	~CDisplayHandler();
	
	bool	Init(void);
	void	Loop(void);

	void	PostMessage(int msg);
	void	PostMessage(int msg, int extended);
	void	PostMessageFromServer(int msg, int magic, char *);
	void	PostStatusLine(char *line, bool highlight);
};

#endif
