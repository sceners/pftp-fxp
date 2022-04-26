#include <stdio.h>
#include <curses.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include "defines.h"
#include "tcp.h"
#include "server.h"
#include "displayhandler.h"
#include "keyhandler.h"

extern	BOOKMARK	*global_bookmark;
extern	int		bm_magic_max;

void CDisplayHandler::SaveBookmarks(char *path)
{
	FILE		*file_out;
	char		*line = new(char[2048]), *enc = new(char[4096]);
	BOOKMARK	*bm_temp = global_bookmark;
	
	chdir(path);
	
	// backup old bookmark-file
	rename(BOOKMARK_RC, BOOKMARK_RC_BAK);
	
	pass_pos = 0;
	if((file_out = fopen(BOOKMARK_RC, "w"))) {
		// put magic and version number
		strcpy(line, PASS_MAGIC);
		Encrypt(line, enc);
		fprintf(file_out, "%s\n%s\n", BOOKMARK_ID, enc);

		// put the bookmarks
		while(bm_temp) {
			Encrypt(bm_temp->label, enc);
			fprintf(file_out, "%s\n", enc);
			Encrypt(bm_temp->host, enc);
			fprintf(file_out, "%s\n", enc);
			Encrypt(bm_temp->user, enc);
			fprintf(file_out, "%s\n", enc);
			Encrypt(bm_temp->pass, enc);
			fprintf(file_out, "%s\n", enc);
			Encrypt(bm_temp->startdir, enc);
			fprintf(file_out, "%s\n", enc);
			Encrypt(bm_temp->exclude, enc);
			fprintf(file_out, "%s\n", enc);
			Encrypt(bm_temp->util_dir, enc);
			fprintf(file_out, "%s\n", enc);
			Encrypt(bm_temp->game_dir, enc);
			fprintf(file_out, "%s\n", enc);
			Encrypt(bm_temp->site_who, enc);
			fprintf(file_out, "%s\n", enc);
			Encrypt(bm_temp->site_user, enc);
			fprintf(file_out, "%s\n", enc);
			Encrypt(bm_temp->site_wkup, enc);
			fprintf(file_out, "%s\n", enc);
			
			sprintf(line, "%d", bm_temp->port);
			Encrypt(line, enc);
			fprintf(file_out, "%s\n", enc);
			sprintf(line, "%d", bm_temp->refresh_rate);
			Encrypt(line, enc);
			fprintf(file_out, "%s\n", enc);
			sprintf(line, "%d", bm_temp->noop_rate);
			Encrypt(line, enc);
			fprintf(file_out, "%s\n", enc);
			
			if(bm_temp->use_refresh)
				strcpy(line, TRUE_MAGIC);
			else
				strcpy(line, FALSE_MAGIC);
			Encrypt(line, enc);
			fprintf(file_out, "%s\n", enc);
			
			if(bm_temp->use_noop)
				strcpy(line, TRUE_MAGIC);
			else
				strcpy(line, FALSE_MAGIC);
			Encrypt(line, enc);
			fprintf(file_out, "%s\n", enc);
			
			if(bm_temp->use_jump)
				strcpy(line, TRUE_MAGIC);
			else
				strcpy(line, FALSE_MAGIC);
			Encrypt(line, enc);
			fprintf(file_out, "%s\n", enc);
			
			if(bm_temp->use_track)
				strcpy(line, TRUE_MAGIC);
			else
				strcpy(line, FALSE_MAGIC);
			Encrypt(line, enc);
			fprintf(file_out, "%s\n", enc);
			
			if(bm_temp->use_startdir)
				strcpy(line, TRUE_MAGIC);
			else
				strcpy(line, FALSE_MAGIC);
			Encrypt(line, enc);
			fprintf(file_out, "%s\n", enc);
			
			if(bm_temp->use_exclude)
				strcpy(line, TRUE_MAGIC);
			else
				strcpy(line, FALSE_MAGIC);
			Encrypt(line, enc);
			fprintf(file_out, "%s\n", enc);

			if(bm_temp->use_autologin)
				strcpy(line, TRUE_MAGIC);
			else
				strcpy(line, FALSE_MAGIC);
			Encrypt(line, enc);
			fprintf(file_out, "%s\n", enc);
			
			if(bm_temp->use_chaining)
				strcpy(line, TRUE_MAGIC);
			else
				strcpy(line, FALSE_MAGIC);
			Encrypt(line, enc);
			fprintf(file_out, "%s\n", enc);
			
			if(bm_temp->use_utilgames)
				strcpy(line, TRUE_MAGIC);
			else
				strcpy(line, FALSE_MAGIC);
			Encrypt(line, enc);
			fprintf(file_out, "%s\n", enc);
		
			bm_temp = bm_temp->next;
		}

		fclose(file_out);
	}
	
	delete(line);
	delete(enc);
}

void CDisplayHandler::Decrypt(char *in, char *out)
{
	int	decrypt, value, prefix;
	char	*c = in, *o = out;
	
	if(strrchr(in, '\r'))
		*(strrchr(in, '\r')) = '\0';
	if(strrchr(in, '\n'))
		*(strrchr(in, '\n')) = '\0';

	// decrypt string
	while(*c) {
		prefix = (int)*c;
		c += 1;

		if(prefix == 127)
			value = 0;
		else if(prefix == 128)
			value = (int)'\n';
		else if(prefix == 129)
			value = (int)'\r';
		else
			value = (int)*c;
		
		decrypt = value - (int)(this->custom_password[pass_pos]);
		c += 1;
		
		if(decrypt < 0)
			decrypt += 256;
		
		*o = (char)decrypt;

		o += 1;
		if(this->custom_password[pass_pos+1])
			pass_pos++;
		else
			pass_pos = 0;
	}
	
	*o = '\0';
}

void CDisplayHandler::Encrypt(char *in, char *out)
{
	int	encrypt;
	char	*c = in, *o = out, prefix;

	// encrypt string
	while(*c) {
		encrypt = (int)(*c) + (int)(this->custom_password[pass_pos]);
		
		if(encrypt == 0) {
			prefix = 127;
			encrypt = 32;
		}
		else if(encrypt == '\n') {
			prefix = 128;
			encrypt = 32;
		}
		else if(encrypt == '\r') {
			prefix = 129;
			encrypt = 32;
		}
		else {
			prefix = 130;
			if(encrypt < 0)
				encrypt += 256;
		}
		
		*o = prefix;
		o += 1;
		
		*o = (char)encrypt;
		o += 1;
		c += 1;

		if(this->custom_password[pass_pos+1])
			pass_pos++;
		else
			pass_pos = 0;
	}

	*o = '\0';
}

bool CDisplayHandler::ReadBookmarks(void)
{
	FILE		*file_in;
	char		*line = new(char[4096]), *out = new(char[2048]);
	BOOKMARK	*bm_new, *bm_temp = NULL;
	
	pass_pos = 0;
	if((file_in = fopen(BOOKMARK_RC, "r"))) {
		//ignore bookmark-id
		fgets(line, 4095, file_in);
		
		// parse magic
		fgets(line, 4095, file_in);
		this->Decrypt(line, out);
		if(strcmp(PASS_MAGIC, out)) {
			fclose(file_in);
			delete(line);
			delete(out);
			return(FALSE);
		}

		// parse bookmarks
		while(!feof(file_in)) {
			bm_new = new(BOOKMARK);
			bm_new->next = NULL;
		
			fgets(line, 4095, file_in);
			if(!feof(file_in)) {
				Decrypt(line, out);
				bm_new->label = new(char[strlen(out) + 1]);
				strcpy(bm_new->label, out);
			
				fgets(line, 4095, file_in);
				Decrypt(line, out);
				bm_new->host = new(char[strlen(out) + 1]);
				strcpy(bm_new->host, out);
			
				fgets(line, 4095, file_in);
				Decrypt(line, out);
				bm_new->user = new(char[strlen(out) + 1]);
				strcpy(bm_new->user, out);
			
				fgets(line, 4095, file_in);
				Decrypt(line, out);
				bm_new->pass = new(char[strlen(out) + 1]);
				strcpy(bm_new->pass, out);
			
				fgets(line, 4095, file_in);
				Decrypt(line, out);
				bm_new->startdir = new(char[strlen(out) + 1]);
				strcpy(bm_new->startdir, out);
		
				fgets(line, 4095, file_in);
				Decrypt(line, out);
				bm_new->exclude = new(char[strlen(out) + 1]);
				strcpy(bm_new->exclude, out);
			
				fgets(line, 4095, file_in);
				Decrypt(line, out);
				bm_new->util_dir = new(char[strlen(out) + 1]);
				strcpy(bm_new->util_dir, out);
			
				fgets(line, 4095, file_in);
				Decrypt(line, out);
				bm_new->game_dir = new(char[strlen(out) + 1]);
				strcpy(bm_new->game_dir, out);
			
				fgets(line, 4095, file_in);
				Decrypt(line, out);
				bm_new->site_who = new(char[strlen(out) + 1]);
				strcpy(bm_new->site_who, out);
			
				fgets(line, 4095, file_in);
				Decrypt(line, out);
				bm_new->site_user = new(char[strlen(out) + 1]);
				strcpy(bm_new->site_user, out);
			
				fgets(line, 4095, file_in);
				Decrypt(line, out);
				bm_new->site_wkup = new(char[strlen(out) + 1]);
				strcpy(bm_new->site_wkup, out);
			

				fgets(line, 4095, file_in);
				Decrypt(line, out);
				bm_new->port = atoi(out);
			
				fgets(line, 4095, file_in);
				Decrypt(line, out);
				bm_new->refresh_rate = atoi(out);
			
				fgets(line, 4095, file_in);
				Decrypt(line, out);
				bm_new->noop_rate = atoi(out);
			

				fgets(line, 4095, file_in);
				Decrypt(line, out);
				bm_new->use_refresh = strcmp(out, TRUE_MAGIC) ? FALSE : TRUE;
			
				fgets(line, 4095, file_in);
				Decrypt(line, out);
				bm_new->use_noop = strcmp(out, TRUE_MAGIC) ? FALSE : TRUE;
			
				fgets(line, 4095, file_in);
				Decrypt(line, out);
				bm_new->use_jump = strcmp(out, TRUE_MAGIC) ? FALSE : TRUE;
			
				fgets(line, 4095, file_in);
				Decrypt(line, out);
				bm_new->use_track = strcmp(out, TRUE_MAGIC) ? FALSE : TRUE;
			
				fgets(line, 4095, file_in);
				Decrypt(line, out);
				bm_new->use_startdir = strcmp(out, TRUE_MAGIC) ? FALSE : TRUE;
			
				fgets(line, 4095, file_in);
				Decrypt(line, out);
				bm_new->use_exclude = strcmp(out, TRUE_MAGIC) ? FALSE : TRUE;
			
				fgets(line, 4095, file_in);
				Decrypt(line, out);
				bm_new->use_autologin = strcmp(out, TRUE_MAGIC) ? FALSE : TRUE;
			
				fgets(line, 4095, file_in);
				Decrypt(line, out);
				bm_new->use_chaining = strcmp(out, TRUE_MAGIC) ? FALSE : TRUE;
			
				fgets(line, 4095, file_in);
				Decrypt(line, out);
				bm_new->use_utilgames = strcmp(out, TRUE_MAGIC) ? FALSE : TRUE;
			
				bm_magic_max++;
				bm_new->magic = bm_magic_max;
				
				if(bm_temp)
					bm_temp->next = bm_new;
				else
					global_bookmark = bm_new;
		
				bm_temp = bm_new;
			}
		}
		
		fclose(file_in);
		delete(line);
		delete(out);
		return(TRUE);
	}
	delete(line);
	delete(out);
	return(FALSE);
}

bool CDisplayHandler::ProbeBookmarkRC(void)
{
	FILE	*file_probe;
	
	if((file_probe = fopen(BOOKMARK_RC, "r"))) {
		fclose(file_probe);
		return(TRUE);
	}
	else
		return(FALSE);
}

void CDisplayHandler::DialogNotice(char *notice, char *button)
{
	// initialize notice window
	this->window_notice = newwin(5, 60, this->terminal_max_y / 2 - 2, this->terminal_max_x / 2 - 30);
	leaveok(window_notice, TRUE);

	wattrset(this->window_notice, COLOR_PAIR(STYLE_NORMAL) | A_NORMAL);
	wbkgdset(this->window_notice, ' ' | COLOR_PAIR(STYLE_NORMAL));
	werase(this->window_notice);
	wbkgdset(this->window_notice, ' ');
	wborder(this->window_notice, 0, 0, 0, 0, 0, 0, 0, 0);

	mvwaddnstr(this->window_notice, 1, 2, notice, 56);
	wattrset(this->window_notice, COLOR_PAIR(STYLE_WHITE) | this->inverse_mono);
	mvwaddstr(this->window_notice, 3, 30 - strlen(button) / 2, button);
	
	wnoutrefresh(window_notice);
	doupdate();
}

void CDisplayHandler::CloseNotice(void)
{
	delwin(this->window_notice);
	this->window_notice = NULL;
	redrawwin(this->window_command);
	redrawwin(this->window_status);
	redrawwin(this->window_left);
	redrawwin(this->window_right);

	if(this->window_dialog)
		redrawwin(this->window_dialog);
	if(this->window_input)
		redrawwin(this->window_input);
}

void CDisplayHandler::NoticeNoMatch(void)
{
	this->DialogNotice(NOTICE_PASSNOMATCH, DEFAULT_OKAY);
}

void CDisplayHandler::NoticeNoBookmark(void)
{
	this->DialogNotice(NOTICE_NOBOOKMARK, DEFAULT_OKAY);
}

void CDisplayHandler::NoticeNoPasswd(void)
{
	this->DialogNotice(NOTICE_NOPASSWD, DEFAULT_OKAY);
}

void CDisplayHandler::NoticeBadPasswd(void)
{
	this->DialogNotice(NOTICE_BADPASSWD, DEFAULT_OKAY);
}

void CDisplayHandler::OpenPasswordInput(bool hidden)
{
	strcpy(this->password, "");
	this->DialogInput(DIALOG_ENTERPASS, this->password, PASSWORD_SIZE - 1, hidden);
}

void CDisplayHandler::CloseInput(void)
{
	delwin(this->window_input);
	this->window_input = NULL;
	redrawwin(this->window_command);
	redrawwin(this->window_status);
	redrawwin(this->window_left);
	redrawwin(this->window_right);

	if(this->window_dialog)
		redrawwin(this->window_dialog);

	if(this->window_prefs)
		redrawwin(this->window_prefs);
}

void CDisplayHandler::DialogInput(char *title, char *input, int max, bool hidden)
{
	unsigned int	n;
	
	// initialize input window
	this->window_input = newwin(5, 60, this->terminal_max_y / 2 - 2, this->terminal_max_x / 2 - 30);
	leaveok(this->window_input, TRUE);
	
	wattrset(this->window_input, COLOR_PAIR(STYLE_NORMAL) | A_NORMAL);
	wbkgdset(this->window_input, ' ' | COLOR_PAIR(STYLE_NORMAL));
	werase(this->window_input);
	wbkgdset(this->window_input, ' ');
	wborder(this->window_input, 0, 0, 0, 0, 0, 0, 0, 0);

	mvwaddnstr(this->window_input, 1, 2, title, 56);
	this->input = input;

	for(n = 0; n < strlen(input); n++)
		*(this->hidden_input + n) = '*';
	*(this->hidden_input + n) = '\0';
			
	this->input_hidden = hidden;
	this->input_chars = strlen(this->input);
	this->input_maxchars = max;
	this->UpdateInput();

	wnoutrefresh(window_input);
	doupdate();
}

void CDisplayHandler::DialogXSite(void)
{
	unsigned int	n;
	
	// initialize input window
	this->window_input = newwin(7, 60, this->terminal_max_y / 2 - 3, this->terminal_max_x / 2 - 30);
	leaveok(this->window_input, TRUE);
	
	wattrset(this->window_input, COLOR_PAIR(STYLE_NORMAL) | A_NORMAL);
	wbkgdset(this->window_input, ' ' | COLOR_PAIR(STYLE_NORMAL));
	werase(this->window_input);
	wbkgdset(this->window_input, ' ');
	wborder(this->window_input, 0, 0, 0, 0, 0, 0, 0, 0);

	mvwaddnstr(this->window_input, 1, 2, "enter SITE command or use aliases", 56);
	this->input = this->input_temp;

	for(n = 0; n < strlen(input); n++)
		*(this->hidden_input + n) = '*';
	*(this->hidden_input + n) = '\0';
			
	this->input_hidden = FALSE;
	this->input_chars = strlen(this->input);
	this->input_maxchars = INPUT_TEMP_MAX-1;
	this->UpdateInput();

	this->xsite_buttonstate = 0;
	this->UpdateXSiteButtons();
}

void CDisplayHandler::UpdateXSiteButtons(void)
{
	int	n;

	// remove old button selection and draw active one

	for(n = 0; n < 4; n++) {
		if(n == this->xsite_buttonstate)
			wattrset(this->window_input, COLOR_PAIR(STYLE_WHITE) | this->inverse_mono);
		else
			wattrset(this->window_input, COLOR_PAIR(STYLE_MARKED) | A_BOLD);

		switch(n) {
		case	1:	mvwaddstr(this->window_input, 5, 7, "[ WHO ]");
				break;

		case	2:	mvwaddstr(this->window_input, 5, 26, "[ USER ]");
				break;

		case	3:	mvwaddstr(this->window_input, 5, 45, "[ WKUP ]");
				break;
		}

	}
	
	wnoutrefresh(window_input);
	doupdate();
}

void CDisplayHandler::CloseSiteInput(void)
{
	delwin(this->window_input);
	this->window_input = NULL;
	redrawwin(this->window_command);
	redrawwin(this->window_status);
	redrawwin(this->window_left);
	redrawwin(this->window_right);
}

void CDisplayHandler::UpdateInput(void)
{	
	wattrset(this->window_input, COLOR_PAIR(STYLE_INVERSE) | this->inverse_mono);
	mvwaddstr(this->window_input, 3, 2, "                                                        ");	
	
	if(this->input_chars <= 55) {
		// show full string
		if(!this->input_hidden)
			mvwaddstr(this->window_input, 3, 2, this->input);
		else
			mvwaddstr(this->window_input, 3, 2, this->hidden_input);
		
		// draw "cursor"
		wattrset(this->window_input, COLOR_PAIR(STYLE_WHITE) | this->inverse_mono);
		mvwaddch(this->window_input, 3, 2 + this->input_chars, ' ');	
	}
	else {
		// truncate
		if(!this->input_hidden)
			mvwaddstr(this->window_input, 3, 2, this->input + this->input_chars - 55);
		else
			mvwaddstr(this->window_input, 3, 2, this->hidden_input + this->input_chars - 55);
		
		// draw "cursor"
		wattrset(this->window_input, COLOR_PAIR(STYLE_WHITE) | this->inverse_mono);
		mvwaddch(this->window_input, 3, 57, ' ');	
	}

	
	wnoutrefresh(window_input);
	doupdate();
}

void CDisplayHandler::InputAppendChar(char c)
{
	if(this->input_chars < this->input_maxchars) {
		// we are allowed to append a char
		*(this->input + this->input_chars) = c;
		*(this->input + this->input_chars + 1) = '\0';

		if(this->input_hidden) {
			*(this->hidden_input + this->input_chars) = '*';
			*(this->hidden_input + this->input_chars + 1) = '\0';
		}
		
		(this->input_chars)++;
		this->UpdateInput();
	}
}

void CDisplayHandler::InputBackspace(void)
{
	if(this->input_chars > 0) {
		*(this->input + this->input_chars - 1) = '\0';
		
		if(this->input_hidden)
			*(this->hidden_input + this->input_chars - 1) = '\0';
		
		(this->input_chars)--;
		this->UpdateInput();
	}
}
