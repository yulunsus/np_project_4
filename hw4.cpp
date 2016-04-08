#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/signal.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <netdb.h>
#include <iostream>
#include <sstream>
#include <fstream>
#include <string>
#include <cstring>
#include <errno.h>

using namespace std;

#define buffer_size 2048
#define F_CONNECTING 0
#define F_READING 1
#define F_WRITING 2
#define F_DONE 3

#define msg_size 200
#define CONNECT 0x01
#define BIND 0x02
#define GRANTED 0x5A
#define REJECTED 0x5B
#define REQUEST 0x04
#define REPLY 0x00

class Host
{
public:
	string ip;   
	string port;
	string batch;

	string ip_str;
	int port_int;
	string sip;
	string sip_str;
	string sport;
	int sport_int;
	bool is_on;
	bool is_done;
	int sockfd;
	ifstream batch_stream;
	string msg_str;
	int status;
	
	Host()
	{
		ip = "No Server";
		is_on = false;
		status = F_CONNECTING;
		is_done = false;
	}

	void init(char* in_ip, char* in_port,char* in_batch, char* in_sip, char* in_usport)
	{
		ip = in_ip;
		port = in_port;
		port_int = atoi(port.c_str());
		batch = in_batch;
		sip = in_sip;
		sport = in_usport;
		sport_int = atoi(sport.c_str());
		is_on = true;	
		is_done = false;
		msg_str = "";
	}

};

void err_dump(const char* msg)
{
	write(3,msg,strlen(msg));
	exit(1);
}

int main()
{
	int originIn = dup(0);
	Host* host = new Host[5];

	printf("Content-type: text/html\n\n");
	char* query = getenv("QUERY_STRING");
	char* tmp = strtok(query,"&");
	char* ptr;
	char* tmp_ip;
	char* tmp_port;
	char* tmp_batch;
	char* tmp_sip;
	char* tmp_sport;

	for(int i=0; i<5; i++){

		ptr = strchr(tmp, '=');
		if(*(ptr+1) != '\0'){

			tmp_ip = ptr+1;
			tmp = strtok(NULL,"&");
			ptr = strchr(tmp, '=');
			tmp_port = ptr+1;
			tmp = strtok(NULL,"&");
			ptr = strchr(tmp, '=');
			tmp_batch = ptr+1;
			tmp = strtok(NULL,"&");
			ptr = strchr(tmp,'=');
			tmp_sip = ptr+1;
			tmp = strtok(NULL,"&");
			ptr = strchr(tmp,'=');
			tmp_sport = ptr+1;

			host[i].init(tmp_ip, tmp_port, tmp_batch, tmp_sip, tmp_sport);
		}
		else{

			for(int j = 0; j < 4; j++)
				tmp = strtok(NULL,"&");
		}
		if (i!=4)
			tmp = strtok(NULL,"&");
	}

	printf("<html>");
	printf("<head>");
	printf("<meta http-equiv=\"Content-Type\" content=\"text/html; charset=big5\" />");
	printf("<title>Network Programming Homework 3</title>");
	printf("</head>");
	printf("<style type='text/css'> xmp {display: inline;} </style>");	
	printf("<body bgcolor=#336699>");
	printf("<font face=\"Courier New\" size=2 color=#FFFF99>");

	for(int i=0; i<5; i++)
		printf("<xmp>Host%d:\tip=%s\tport=%s\tbatch=%s\tproxy ip=%s\tproxy port=%s</xmp><br>", i+1,host[i].ip.c_str(), host[i].port.c_str(), host[i].batch.c_str(), host[i].sip.c_str(), host[i].sport.c_str());

	printf("<table width=\"800\" border=\"1\">");
	printf("<tr>");
	
	for(int i=0; i<5; i++)
		printf("<td><xmp>%s</xmp></td>", host[i].ip.c_str());
	
	printf("</tr>");

	printf("<tr>");
	printf("<td valign=\"top\" id=\"m0\"></td><td valign=\"top\" id=\"m1\"></td><td valign=\"top\" id=\"m2\"><td valign=\"top\" id=\"m3\"><td valign=\"top\" id=\"m4\"></td></tr>");
 	printf("</table>");
	
	for(int i=0; i<5; i++){

		if(!host[i].is_on)
			printf("<script>document.all['m%d'].innerHTML += \"<br>\";</script>", i);
	}
 
 	fflush(stdout);

 	int connection_num = 0;
	int nfds = 1024;
	fd_set rfds;
	fd_set rs;
	fd_set wfds;
	fd_set ws;
	FD_ZERO(&rs);
	FD_ZERO(&ws);

	for(int i=0; i < 5; i++){

		if(host[i].is_on){

			struct sockaddr_in client;
			struct hostent *he;
			unsigned int clilen = sizeof(client);
			int flag;

			host[i].sockfd = socket(AF_INET, SOCK_STREAM, 0);
 			bzero(&client, clilen);
 			client.sin_family = AF_INET;

			if((he = gethostbyname(host[i].sip.c_str())) != NULL)
				client.sin_addr = *((struct in_addr *)he->h_addr);
			else if ((client.sin_addr.s_addr = inet_addr(host[i].sip.c_str())) == INADDR_NONE)
				err_dump("Usage : client <server ip> <port> <testfile>");

			host[i].sip_str = inet_ntoa(client.sin_addr);
			client.sin_port = htons((u_short) (host[i].sport_int));
			flag = fcntl(host[i].sockfd, F_GETFL, 0);
			fcntl(host[i].sockfd, F_SETFL, flag | O_NONBLOCK);
			
			if(connect(host[i].sockfd, (struct sockaddr *)&client, clilen) == -1)
				if(errno != EINPROGRESS) 
					return -1;

			if(host[i].sockfd != -1){

				connection_num++;
				FD_SET(host[i].sockfd, &rs);
				FD_SET(host[i].sockfd, &ws);
				host[i].batch_stream.open(host[i].batch.c_str(), ios::in);
				
				//send request to sock server
				struct sockaddr_in sclient;
				unsigned int sclilen = sizeof(sclient);
				struct hostent *she;
				bool is_domain = false;

				bzero(&sclient,sclilen);
				if((she = gethostbyname(host[i].ip.c_str())) != NULL)
					sclient.sin_addr = *((struct in_addr *)she->h_addr);
				else if((sclient.sin_addr.s_addr = inet_addr(host[i].ip.c_str())) == INADDR_NONE)
					err_dump("Usage : client <server ip> <port> <test file>");
				
				unsigned char request[9];
				int host_port[2];
				unsigned char host_ip[4];
				host[i].ip_str = inet_ntoa(sclient.sin_addr);
				host_port[0] = host[i].port_int / 256;
				host_port[1] = host[i].port_int % 256;

				sscanf(host[i].ip_str.c_str(), "%hhu.%hhu.%hhu.%hhu", &host_ip[0], &host_ip[1], &host_ip[2], &host_ip[3]);

				request[0] = REQUEST;       // VN
				request[1] = CONNECT;       // CD
				request[2] = host_port[0];  // port
				request[3] = host_port[1];

				for(int j = 4; j < 8; j++)
					request[j] = host_ip[j-4];

				request[8] = '\0';

				int write_result = write(host[i].sockfd, request, 9);

			}
		}
	}

	int wait = connection_num;
	while(wait > 0)
	{
		memcpy(&rfds, &rs, sizeof(rfds));
		if(select(nfds, &rfds, (fd_set*) 0, (fd_set*)0, (struct timeval*)0) < 0)
			exit(0);
		for(int i = 0; i < 5; i++)
		{
			if(FD_ISSET(host[i].sockfd, &rfds))
			{
				unsigned char reply[8];
				memset(reply, 0, 8);
				int read_result = read(host[i].sockfd, reply, 8);
				if(read_result == -1 && errno == EAGAIN)
					break;

				wait--;
	
				if(reply[1] == REJECTED)
				{
					printf("<script>document.all['m%d'].innerHTML += \"<b>-------- REJECTED --------</b><br>\";</script>\n", i);
					FD_CLR(host[i].sockfd, &rs);
					FD_CLR(host[i].sockfd, &ws);
					host[i].status = F_DONE;
					connection_num--;
				}
				else if(reply[1] == GRANTED)
					printf("<script>document.all['m%d'].innerHTML += \"<b>-------- GRANTED --------</b><br>\";</script>\n", i);
				
				break;
			}
		}
	}

	while(connection_num > 0)
	{
		memcpy(&rfds,&rs,sizeof(rfds));
		memcpy(&wfds,&ws,sizeof(wfds));
		if(select(nfds, &rfds, &wfds, (fd_set*)0, (struct timeval*)0) < 0) 
			exit(1);

		for(int i=0; i<5; i++){

			if(FD_ISSET(host[i].sockfd, &rfds) || FD_ISSET(host[i].sockfd, &wfds)){

				if(host[i].status == F_CONNECTING){

					int error;
					socklen_t n = sizeof(int);
					if(getsockopt(host[i].sockfd, SOL_SOCKET, SO_ERROR, &error, &n) < 0 || error != 0)
						return (-1); // non-blocking connect failed
					host[i].status = F_READING;
					FD_CLR(host[i].sockfd, &ws);
				}
				else if(host[i].status == F_WRITING && FD_ISSET(host[i].sockfd, &wfds)){

					if(host[i].is_done){

						FD_CLR(host[i].sockfd, &ws);
						host[i].status = F_READING;
						FD_SET(host[i].sockfd, &rs);	// after read command, go to read response
						continue;
					}

					string line;
					getline(host[i].batch_stream, line);
					unsigned pos_r;
					while((pos_r = line.find('\r')) != -1 )
						line.erase(pos_r,1);

					printf("<script>document.all['m%d'].innerHTML += \"<b><xmp>%s</xmp></b><br>\";</script>\n",i,line.c_str());
					fflush(stdout);
							
					if (line == "exit")
						host[i].is_done = true;
					line = line + '\n';

					write(host[i].sockfd,line.c_str(), line.size());
					FD_CLR(host[i].sockfd, &ws);
					host[i].status = F_READING;
					FD_SET(host[i].sockfd, &rs);	// after read command, go to read response
				}
				else if(host[i].status == F_READING && FD_ISSET(host[i].sockfd, &rfds)){

					char buffer[buffer_size];
					int read_status = 0;
					// read from server
					while(true)
					{

						if (host[i].msg_str.size() !=0){

							unsigned pos_r;
							while((pos_r = host[i].msg_str.find('\r')) != -1)
								host[i].msg_str.erase(pos_r, 1);

							unsigned pos_end;
							bool exec_cmd = false;		

							while((host[i].msg_str.size() !=0) && ((pos_end = host[i].msg_str.find('\n')) != -1)){
								
								if(host[i].msg_str[0] == '%')
									exec_cmd = true;

								string line = host[i].msg_str.substr(0,pos_end);// no \n
								unsigned p = -2;

								while((p = line.find('\"',p+2)) != -1)
									line.insert(p,1,'\\');

								printf("<script>document.all['m%d'].innerHTML += \"<xmp>%s</xmp><br>\";</script>\n",i,line.c_str());
								fflush(stdout);
								host[i].msg_str.erase(0, pos_end+1);
							}

							if(exec_cmd){

								FD_CLR(host[i].sockfd, &rs);
								host[i].status = F_WRITING;
								FD_SET(host[i].sockfd, &ws);
								break;
							}

							if((host[i].msg_str.size() !=0) && (host[i].msg_str[0] == '%') ){

								string sub = host[i].msg_str.substr(0,2);
								host[i].msg_str.erase(0,2);
								printf("<script>document.all['m%d'].innerHTML += \"<xmp>%s</xmp>\";</script>\n", i,sub.c_str());
								fflush(stdout);
								FD_CLR(host[i].sockfd, &rs);
								host[i].status = F_WRITING;
								FD_SET(host[i].sockfd, &ws);
								break;
							}
						}			

						memset(buffer, 0, buffer_size);
						read_status = read(host[i].sockfd, buffer, buffer_size);

						if(read_status == -1 && errno == EAGAIN)
							break;
						else if(read_status == -1 && errno != EAGAIN)
							exit(1);
						else if(read_status == 0){

							FD_CLR(host[i].sockfd, &rs);
							host[i].status = F_DONE;
							connection_num--;
							break;
						}

						host[i].msg_str.append(buffer);
						
                    }
				}
			}
		}
	}
	printf("</font>");
	printf("</body>");
	printf("</html>");
 	fflush(stdout);

	return 0;
}
