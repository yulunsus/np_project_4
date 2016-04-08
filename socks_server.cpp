#include <iostream>
#include <sstream>
#include <fstream>
#include <string>
#include <cstring>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/signal.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <netdb.h>

using namespace std;

#define QLEN 100
#define msg_size 200
#define buffer_size 2048
#define CONNECT 0x01
#define BIND 0x02
#define GRANTED 0x5A
#define REJECTED 0x5B
#define REQUEST 0x04
#define REPLY 0x00

class Request;
class SERVER_MSG;

void err_dump(const char* msg);
void reaper(int sig);
int passiveTCP(const char* service, int qlen);
int passive_sock(const char* service, const char* protocol, int qlen);
void read_request(int ssock, Request* request);
void check_firewall(SERVER_MSG* msg);
void connect_function(int ssock, SERVER_MSG* msg);
int connectTCP(char* ip, int port);
void forward(int ssock, int rsock);
void forward_write(int sockfd, char* msg, int write_num);
void bind_function(int ssock, SERVER_MSG* msg);

class Request
{
	public:
		unsigned char VN;
		unsigned char CD;
		unsigned char port[2];
		unsigned char ip[4];
		char* id;
		char* domain;
		unsigned int port_int;
		char ip_str[16];
		bool has_domain;

		Request()
		{
			memset(ip_str, 0, 16);
			id = NULL;
			domain = NULL;
			has_domain = false;
		}

		void set_first_8(unsigned char first_8[])
		{
			VN = first_8[0];
			CD = first_8[1];
			port[0] = first_8[2];
			port[1] = first_8[3];
			ip[0] = first_8[4];
			ip[1] = first_8[5];
			ip[2] = first_8[6];
			ip[3] = first_8[7];

			port_int = port[0] << 8 | port[1];
			sprintf(ip_str, "%hu.%hu.%hu.%hu", ip[0], ip[1], ip[2], ip[3]);

		}

		void set_id(char* user_id)
		{
			id = new char[strlen(user_id)+1];
			memset(id, 0 , strlen(id));
			strcpy(id, user_id);
		}

		void set_domain(char* user_domain)
		{
			has_domain = true;
			domain = new char[strlen(user_domain) + 1];
			memset(domain, 0, strlen(domain));
			strcpy(domain, user_domain);
		}

		void show_on_server()
		{
			printf("(%d) -------- REQUEST --------\n",getpid());
			printf("(%d) VN: %hu, CD: %hu, DST IP: %s, DST Port: %d, USERID: %s", getpid(), VN, CD, ip_str, port_int, id);
			
			if(has_domain)
				printf("Domain name: %s\n",domain);
			else
				printf("\n");
			
			fflush(stdout);
		}
};

class SERVER_MSG
{
	public:
		Request* request;
		char src_ip[16];
		unsigned int src_port;
		bool passed;
		unsigned char reply;

		SERVER_MSG()
		{
			memset(src_ip, 0, 16);
		}

		void set_request(Request* req)
		{
				request = req;
		}

		void set_src(struct sockaddr_in* cli_addr)
		{
			strcpy(this->src_ip, inet_ntoa(cli_addr->sin_addr));
			this->src_port = cli_addr->sin_port;
		}

		void set_pass(bool p)
		{
			passed = p;
		}

		void set_reply(unsigned char in_reply)
		{
			this->reply = in_reply;
		}
};

int main(int argc, char** argv)
{
	if(argc != 2){

		char buffer[100];
		memset(buffer, 0, 100);
		sprintf(buffer, "Usage: %s [port]", argv[0]);
		err_dump(buffer);
	}

	int msock, ssock;
	struct sockaddr_in cli_addr;
	unsigned int clilen;
	int child_pid;

	msock = passiveTCP(argv[1], QLEN);

	(void) signal(SIGCHLD, reaper);

	while(1)
	{
		SERVER_MSG* msg = new SERVER_MSG();
		clilen = sizeof(cli_addr);
		ssock = -1;
		child_pid = -1;	
		bzero((char *)&cli_addr, clilen);
	
		ssock = accept(msock, (struct sockaddr *)&cli_addr, (socklen_t*)&clilen);
		msg->set_src(&cli_addr);

		if(ssock < 0)
			err_dump("Accept error");

		child_pid = fork();

		if(child_pid < 0)
			err_dump("Fork error");
		else if(child_pid == 0){

			close(msock);

			Request* request = new Request();
			read_request(ssock, request);
			msg->set_request(request);

			check_firewall(msg);

			if(msg->passed){

				if(request->CD == CONNECT)
					connect_function(ssock, msg);
				else if(request->CD == BIND)
					bind_function(ssock, msg);
				exit(0);
			}
			else{

				unsigned char reply[8];
				reply[0] = REPLY;
				reply[1] = REJECTED;

				for(int i = 2; i < 4; i++)
					reply[i] = request->port[i-2];
				for(int i = 4; i < 8; i++)
					reply[i] = request->ip[i-4];

				if(msg->request->CD == CONNECT)
					printf("(%d) SOCKS Connect Rejected\n", getpid());
				else
					printf("(%d) SOCKS Bind Rejected\n", getpid());

				write(ssock, reply, 8);
				close(ssock);
				exit(0);
			}
		}
		else 
			close(ssock);
	}
	return 0;
}

void err_dump(const char* msg)
{
	perror(msg);
	exit(1);
}

void reaper(int sig)
{
	wait3(NULL, WNOHANG, (struct rusage *)0);
}

int passiveTCP(const char* service, int qlen)
{
	return passive_sock(service, "tcp", qlen);
}

int passive_sock(const char* service, const char* protocol, int qlen)
{
	struct servent *pse;
	struct protoent *ppe;
	struct sockaddr_in sin;
	int s, type, port_base;
	port_base = 1024;

	bzero((char*)&sin, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = INADDR_ANY;

	/* Map service name to port number */
	if(strcmp(service, "0") == 0)
		sin.sin_port = htons(0);
	else if(pse = getservbyname(service, protocol))
		sin.sin_port = htons(ntohs((u_short)pse->s_port)+port_base);
	else if((sin.sin_port = htons((u_short)atoi(service))) == 0){

		char buffer[msg_size];
		memset(buffer, 0, msg_size);
		sprintf(buffer, "(%d) Can't get [%s] port entry", getpid(), service);
		err_dump(buffer);
	}

	/* Map protocol name to protocol number */
	if((ppe = getprotobyname(protocol)) == 0){

		char buffer[msg_size];
		memset(buffer, 0, msg_size);
		sprintf(buffer, "(%d) Can't get [%s] port entry", getpid(), protocol);
		err_dump(buffer);
	}
	
	/* Use protocol to choose a socket type */
	if(strcmp(protocol, "udp") == 0)
		type = SOCK_DGRAM;
	else
		type = SOCK_STREAM;

	/* Allocate a socket */
	s = socket(PF_INET, type, ppe->p_proto);

	if(s < 0){

		char buffer[msg_size];
		memset(buffer, 0, msg_size);
		sprintf(buffer, "(%d) can't get [%s] port", getpid(), service);
		err_dump(buffer);
	}

	int sock_set = 1;
	setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &sock_set, sizeof(int));

	/* Bind the socket */
	if(bind(s, (struct sockaddr *)&sin, sizeof(sin)) < 0){

		char buffer[msg_size];
		memset(buffer, 0, msg_size);
		sprintf(buffer, "(%d) Can't get [%s] port", getpid(), service);
		err_dump(buffer);
	}

	if(type == SOCK_STREAM && listen(s, qlen) < 0){

		char buffer[msg_size];
		memset(buffer, 0, msg_size);
		sprintf(buffer, "(%d) Can't get [%s] port", getpid(), service);
		err_dump(buffer);
	}
	
	return s;
}

void read_request(int ssock, Request* request)
{
	unsigned char first_8[8];
	char buffer[buffer_size], user_id[buffer_size], user_domain[buffer_size];

	memset(user_id, 0, buffer_size);
	memset(user_domain, 0 ,buffer_size);
	memset(first_8, 0, 8);

	read(ssock, first_8, 8);

	// NOT request
	if(((unsigned char)first_8[0] != REQUEST) || ( ((unsigned char)first_8[1] != CONNECT) && ((unsigned char)first_8[1] != BIND))){
		
		close(ssock);
		exit(0);
	}

	request->set_first_8(first_8);

	memset(buffer, 0, buffer_size);
	int read_result = read(ssock, buffer, buffer_size);

	sprintf(user_id, "%s", &buffer[0]);
	request->set_id(user_id);

	if((request->ip[0] == 0) && (request->ip[1]) == 0 && (request->ip[2] == 0)){

		if(read_result > (strlen(user_id)+1)){

			sprintf(user_domain, "%s", &buffer[strlen(user_id)] + 1);
			request->set_domain(user_domain);
		}
	}
	request->show_on_server();

	//convert domain name to ip
	if(request->has_domain){

		struct hostent *he;
		struct sockaddr_in client;
		int clilen = sizeof(client);
		bzero((char *)&client, clilen);

		if((he = gethostbyname(user_domain)) != NULL){

			client.sin_addr = *((struct in_addr *)he->h_addr);
			memset(request->ip_str, 0, 16);
			strcpy(request->ip_str, inet_ntoa(client.sin_addr));
		}
		else{

			unsigned char reply[8];
			reply[0] = REPLY;
			reply[1] = REJECTED;

			for(int i = 2; i < 4; i++)
				reply[i] = request->port[i-2];
			for(int i = 4; i < 8; i++)
				reply[i] = request->ip[i-4];

			write(ssock, reply, 8);
			close(ssock);
			char buffer[msg_size];
			memset(buffer, 0, msg_size);
			sprintf(buffer, "(%d) Failed to find ip by domain name [%s]", getpid(), user_domain);
			err_dump(buffer);
		}
	}
}

void check_firewall(SERVER_MSG* msg)
{
	bool pass = false;
	ifstream firewall;
	string line;
	char request_type;
	int request_ip[4];

	if(msg->request->CD == CONNECT)
		request_type = 'c';
	else
		request_type = 'b';

	sscanf(msg->request->ip_str,"%d.%d.%d.%d", &request_ip[0], &request_ip[1], &request_ip[2], &request_ip[3]);

	firewall.open("socks.conf", ios::in);

	while(getline(firewall, line)){

		char type;
		bool permit;
		int ip[4];

		stringstream ss_line(line);
		ss_line >> permit >> type >> ip[0] >> ip[1] >> ip[2] >> ip[3];

		if(permit)
			printf("permit = true, type = %c, ip[0] = %d, ip[1] = %d, ip[2] = %d, ip[3] = %d", type, ip[0], ip[1], ip[2], ip[3]);
		else
			printf("permit = false, type = %c, ip[0] = %d, ip[1] = %d, ip[2] = %d, ip[3] = %d", type, ip[0], ip[1], ip[2], ip[3]);

		if(request_type == type){

			int match_num= 0;

			for(int i = 0; i < 4; i++){

				if((ip[i] == -1) || (ip[i] == request_ip[i]))
					match_num++;
			}

			if(match_num == 4){

				pass = permit;
				break;
			}

		}
	}

	printf("(%d) ----------   FIREWALL   ----------\n",getpid());

	if(pass)
		printf("(%d) Permitted ", getpid());
	else
		printf("(%d) Denied ", getpid());

	printf("Src = %s(%d), Dst = %s(%d)\r\n",msg->src_ip, msg->src_port, msg->request->ip_str, msg->request->port_int);
	
	fflush(stdout);
	msg->set_pass(pass);	
}

int connectTCP(char* ip, int port)
{
	struct hostent *phe;
	struct servent *pse;
	struct protoent *ppe;
	struct sockaddr_in sin;
	int s, type;

	bzero((char *)&sin, sizeof(sin));
	sin.sin_family = AF_INET;

	/* Map service name to port number */
	if((sin.sin_port = htons((u_short)port)) == 0){

		char buffer[buffer_size];
		memset(buffer, 0, buffer_size);
		sprintf(buffer, "(%d) Can't get [%d] port entry",getpid(),port);
		err_dump(buffer);
	}

	/* Map host name to IP address, allowing for dotted decimal */
	if(phe = gethostbyname(ip))
		bcopy(phe->h_addr, (char *)&sin.sin_addr, phe->h_length);
	else if((sin.sin_addr.s_addr = inet_addr(ip)) == INADDR_NONE ){

		char buffer[buffer_size];
		memset(buffer, 0, buffer_size);
		sprintf(buffer,"(%d) Can't get [%s] ip entry", getpid(), ip);
		err_dump(buffer);
	}

    /* Map protocol name to protocol number */
	if((ppe = getprotobyname("tcp")) == 0)
		err_dump("Can't get \"tcp\" protocol entry");

    /* Use protocol to choose a socket type */
	type = SOCK_STREAM;

	/* Allocate a socket */
	s = socket(PF_INET, type, ppe->p_proto);
	if(s < 0)
		err_dump("Can't create socket:");

	/* Connect the socket */
	if(connect(s, (struct sockaddr *)&sin, sizeof(sin)) < 0){

		char buffer[buffer_size];
		memset(buffer, 0, buffer_size);
		sprintf(buffer, "(%d) Can't connect to %s(%d)", getpid(), ip, port);
		err_dump(buffer);
	}
	return s;
}

void connect_function(int ssock, SERVER_MSG* msg)
{
	unsigned char reply[8];
	int rsock;

	printf("(%d) -------- CONNECT, Dst = %s(%d) --------\n", getpid(), msg->request->ip_str, msg->request->port_int);	
	rsock = connectTCP(msg->request->ip_str, msg->request->port_int);

	reply[0] = 0;

	if(rsock > -1)
		reply[1] = GRANTED;
	else 
		reply[1] = REJECTED;

	for(int i = 2; i < 4; i++)
		reply[i] = msg->request->port[i-2];

	for(int i = 4; i < 8; i++)
		reply[i] = msg->request->ip[i-4];

	msg->set_reply(reply[1]);

	write(ssock, reply, 8);

	printf("(%d) -------- REPLY --------\n", getpid());

	if(reply[1] == REJECTED){

		printf("(%d) SOCKS_CONNECT_REJECTED\n", getpid());
		close(ssock);
		exit(0);
	}

	printf("(%d) SOCKS_CONNECT_GRANTED\n", getpid());
	printf("(%d) -------- TRANSFER --------\n", getpid());

	forward(ssock, rsock);
	exit(0);
}

void forward_write(int sockfd, char* msg, int write_num)
{
	char* write_buffer = new char[write_num + 1];
	memcpy(write_buffer, msg, write_num);
	write_buffer[write_num] = 0;
	int write_result;
	int write_idx = 0;

	while(write_idx != write_num)
	{
		write_result = write(sockfd, write_buffer+write_idx, write_num-write_idx);
		if(write_result == -1)
			err_dump("forward_write Error");
		else
			write_idx += write_result;
	}

	delete[] write_buffer;
}

void forward(int ssock, int rsock)
{
	char buffer[buffer_size];
	int nfds, rc;
	int read_result;

	if(ssock > rsock)
		nfds = ssock + 1;
	else
		nfds = rsock + 1;

	fd_set rfds;
	fd_set afds;
	FD_ZERO(&afds);
	FD_SET(ssock, &afds);
	FD_SET(rsock, &afds);

	bool s_closed, r_closed;
	s_closed = false;
	r_closed = false;

	while(1)
	{
		memcpy(&rfds, &afds, sizeof(rfds));

		if(select(nfds, &rfds, (fd_set*)0, (fd_set*)0, (struct timeval*)0) < 0)
			err_dump("-------- select Error --------");

		if(FD_ISSET(ssock, &rfds)){
			
			read_result = read(ssock, buffer, buffer_size);
			
			if(read_result == -1){

				char msg[msg_size];
				memset(msg, 0, msg_size);
				sprintf(msg, "(%d) ssock read Error", getpid());
				err_dump(msg);
			}
			else if(read_result == 0){
				
				printf("(%d) ssock read End\n", getpid());
				FD_CLR(ssock, &afds);
				shutdown(ssock,SHUT_RD);
				shutdown(rsock, SHUT_WR);

				s_closed = true;
				if(s_closed && r_closed)
					break;
			}
			else{
				printf("(%d) ssock sends %d\t bytes to rsock\n", getpid(), read_result);
				forward_write(rsock, buffer, read_result);
			}
		}

		if(FD_ISSET(rsock, &rfds)){

			read_result = read(rsock, buffer, buffer_size);

			if(read_result == -1){

				char msg[msg_size];
				memset(msg, 0, msg_size);
				sprintf(msg, "(%d), ssock read Error", getpid());
				err_dump(msg);
			}
			else if(read_result == 0){

				printf("(%d) rsock read End\n", getpid());
				
				FD_CLR(rsock, &afds);
				shutdown(rsock, SHUT_RD);
				shutdown(ssock, SHUT_WR);
				r_closed = true;

				if(s_closed && r_closed)
					break;
			}
			else{
				printf("(%d) ssock sends %d\t bytes to rsock\n", getpid(), read_result);
				forward_write(ssock, buffer, read_result);
			}
		}
	}
	close(ssock);
	close(rsock);
	printf("(%d) -------- EXIT --------\n", getpid());
	exit(0);
}

void bind_function(int ssock, SERVER_MSG* msg)
{
	unsigned char reply[8];
	int psock, rsock;
	struct sockaddr_in sa_in;
	socklen_t sin_len;

	printf("(%d) -------- BIND, Dst = %s(%d) --------\n", getpid(), msg->request->ip_str, msg->request->port_int);
	psock = passiveTCP("0", QLEN);
	sin_len = sizeof(sa_in);
	bzero((char*)&sa_in, sin_len);

	int result = getsockname(psock, (struct sockaddr *)&sa_in, &sin_len);
	if(result < 0)
		err_dump("Bind getsockname Error");

	unsigned short p = sa_in.sin_port;
	printf("(%d) -------- Bind psock on port %hu --------\n", getpid(), ntohs(p));
	
	reply[0] = REPLY;
	if(psock > -1)
		reply[1] = GRANTED;
	else
		reply[1] = REJECTED;
	

	memcpy(reply+2, &p, 2);
	memset(reply+4, 0, 4);
	
	
	msg->set_reply(reply[1]);
	
	write(ssock, reply, 8);
	

	struct sockaddr_in cli_addr;
	unsigned int clilen;

	clilen = sizeof(cli_addr);
	bzero((char *)&cli_addr, clilen);
	
	rsock = accept(psock, (struct sockaddr *)&cli_addr, (socklen_t*)&clilen);
	

	reply[0] = REPLY;
	if(rsock > -1)
		reply[1] = GRANTED;
	else
		reply[1] = REJECTED;

	for(int i = 2; i < 4; i++)
		reply[i] = msg->request->port[i-2];

	for(int i = 4; i < 8; i++)
		reply[i] = msg->request->ip[i-4];

	msg->set_reply(reply[1]);

	cout << "e" << endl;
	write(ssock, reply, 8);
	cout << "f" << endl;

	printf("(%d) -------- REPLY --------\n", getpid());

	if(reply[1] == REJECTED){

		printf("(%d) SOCKS Bind Rejected\n", getpid());
		close(ssock);
		exit(0);
	}

	printf("(%d) SOCKS Bind Granted\n", getpid());
	printf("(%d) -------- TRANSFER --------\n", getpid());

	forward(ssock, rsock);
	exit(0);	
}


