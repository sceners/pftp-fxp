#include <curses.h>
#include <pthread.h>
#include <string.h>

#include "defines.h"
#include "tcp.h"
#include "server.h"
#include "displayhandler.h"

extern	SERVERLIST	*global_server;

void CDisplayHandler::OpenSwitchWindow(void)
{
	SERVERLIST	*sv_temp = global_server;
	
	this->window_switch = newwin(20, 61, this->terminal_max_y / 2 - 10, this->terminal_max_x / 2 - 30);
	leaveok(window_switch, TRUE);

	wattrset(this->window_switch, COLOR_PAIR(STYLE_NORMAL) | A_NORMAL);
	wbkgdset(this->window_switch, ' ' | COLOR_PAIR(STYLE_NORMAL));
	werase(this->window_switch);
	wbkgdset(this->window_switch, ' ');
	wborder(this->window_switch, 0, 0, 0, 0, 0, 0, 0, 0);
	mvwaddstr(this->window_switch, 0, 2, "select server");

	this->switch_count = 0;
	while(sv_temp) {
		this->switch_count++;
		sv_temp = sv_temp->next;
	}
	
	this->switch_pos = 0;
	this->RedrawSwitchWindow();

}

void CDisplayHandler::ScrollSwitch(bool dir_up)
{
	bool	redraw = FALSE;
	
	if(!dir_up) {
		if(this->switch_pos < (this->switch_count-1)) {
			this->switch_pos++;
			redraw = TRUE;
		}
	}
	else {
		if(this->switch_pos > 0) {
			this->switch_pos--;
			redraw = TRUE;
		}
	}
	
	if(redraw)
		this->RedrawSwitchWindow();
}

void CDisplayHandler::PageMoveSwitch(bool dir_up)
{
	bool	redraw = FALSE;
	
	if(dir_up) {
		if(this->switch_pos < (this->switch_count-1)) {
			this->switch_pos += 9;
			if(this->switch_pos > (this->switch_count-1))
				this->switch_pos = this->switch_count-1;
				
			redraw = TRUE;
		}
	}
	else {
		if(this->switch_pos > 0) {
			this->switch_pos -= 9;
			if(this->switch_pos < 0)
				this->switch_pos = 0;
				
			redraw = TRUE;
		}
	}
	
	if(redraw)
		this->RedrawSwitchWindow();
}

void CDisplayHandler::RedrawSwitchWindow(void)
{
	SERVERLIST	*sv_temp = global_server;
	int		ypos, n = 0, magic;
	char		cwd[SERVER_WORKINGDIR_SIZE];
	
	if(sv_temp) {
		// see if we have to correct start
		if((switch_start + 17) < switch_pos)
			switch_start = switch_pos - 17;
		else if(switch_start > switch_pos)
			switch_start = switch_pos;
			
		// now walk to pos and from there display a maximum of 18 servers
		while(sv_temp && (n < this->switch_start)) {
			n++;
			sv_temp = sv_temp->next;
		}
		
		if(sv_temp) {
			// k, now draw all entries
			ypos = 0;
			while(sv_temp && (ypos < 18)) {
				sprintf(this->temp_string, "                                                          ");
				strcpy(this->temp_string, sv_temp->server->GetAlias());
				if(strlen(this->temp_string) < 19)
					this->temp_string[strlen(this->temp_string)] = ' ';
				this->temp_string[19] = ' ';
				this->temp_string[20] = '\0';
				
				sv_temp->server->ObtainWorkingDir(cwd);
				if(strlen(cwd) < 40) {
					strcat(this->temp_string, cwd);
					this->temp_string[20 + strlen(cwd)] = ' ';
					this->temp_string[59] = '\0';
				}
				else {
					strcat(this->temp_string, "...");
					strcat(this->temp_string, cwd + strlen(cwd) - 36);
				}
				
				magic = sv_temp->server->GetMagic();
				if(n != this->switch_pos) {
					if((magic == this->leftwindow_magic) || (magic == this->rightwindow_magic))
						wattrset(this->window_switch, COLOR_PAIR(STYLE_MARKED) | A_BOLD);
					else
						wattrset(this->window_switch, COLOR_PAIR(STYLE_NORMAL) | A_NORMAL);
				}
				else {
					if((magic == this->leftwindow_magic) || (magic == this->rightwindow_magic))
						wattrset(this->window_switch, COLOR_PAIR(STYLE_MARKED_INVERSE) | A_BOLD | this->inverse_mono);
					else
						wattrset(this->window_switch, COLOR_PAIR(STYLE_INVERSE) | this->inverse_mono);
				}

				mvwaddnstr(this->window_switch, ypos+1, 1, this->temp_string, 59);
				
				n++;
				ypos++;
				sv_temp = sv_temp->next;
			}
			
			wnoutrefresh(this->window_switch);
			doupdate();
		}
	}
}

void CDisplayHandler::CloseSwitchWindow(void)
{
	delwin(this->window_switch);
	redrawwin(this->window_command);
	redrawwin(this->window_status);
	redrawwin(this->window_left);
	redrawwin(this->window_right);
}

