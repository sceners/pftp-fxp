#ifndef	__TCP_H
#define	__TCP_H

#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/time.h>
#include <unistd.h>

#define	MSG_STACK		200
#define	CONTROL_BUFFER_SIZE	65536
#define	WRITE_SIZE		8192
#define	CONTROL_TIMEOUT		120
#define	DATA_TIMEOUT		60

class CTCP {
private:
	int			msg_stack[MSG_STACK];
	bool			control_connected, have_accepted, haveIP;
	int			error, control_sock_fd, data_sock_fd, real_data_sock_fd;
	float			speed;
	char			*log[LOG_LINES], control_buffer[CONTROL_BUFFER_SIZE], temp_string[512], stealth_file[256];
	struct in_addr		stored_ip_address;
	struct sockaddr_in	data_sock_in;
	unsigned short int	data_port;
	struct timeval		tv_before, tv_after;
	long			seconds, micros, size;
	bool			stealth, in_stealth;
	FILE			*stealth_fd;
			
	pthread_mutex_t		log_lock;
	
	bool	GetIP(char *, struct in_addr *);
	int	SearchStack(void);
	void	UpdateStack(void);
	int	WaitForDataAndRead(bool, int);
	void	FlushSocket(void);
	void	AddLogLine(char *);
		
public:
	CTCP();
	~CTCP();
	
	bool	OpenControl(BOOKMARK *bm);
	bool	SendData(char *msg);
	bool	WaitForMessage(void);
	bool	WaitForMessage(int);
	int	GetError(void) {return(this->error);};
	void	CloseControl(void);
	void	ObtainLog(char *log[LOG_LINES]);
	char	*GetControlBuffer(void) {return(this->control_buffer);};

	float	GetSpeed(void) {return(this->speed);};
	void	FlushStack(void);
	bool	OpenData(char *, bool);
	bool	AcceptData(void);
	void	CloseData(void);
	bool	ReadFile(char *, long);
	bool	WriteFile(char *, bool);
	void	GetTimevals(struct timeval *before, struct timeval *after, long *size) {*before = this->tv_before; *after = this->tv_after; *size = this->size;};
	void	StripANSI(char *);

	void	SetStealth(bool stealth_set, char *file) {this->stealth = stealth_set; strcpy(this->stealth_file, file);};
};

#endif
