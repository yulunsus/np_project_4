#include <iostream>
#include <string>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/signal.h>
#include <sys/uio.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sstream>
#include <fstream>
#include <cstring>
#include <errno.h>
#include <dirent.h>
#include <vector>
#include <set>
#define MAXLINE 512

using namespace std;



class CMD
{
	public:
		int argc;
		char** argv;
		int out_type;  // 0: no pipe; 
					   // 1: | 
					   // 2: ! 
					   // 3: > 
		int line_shift; // 0: cmd | cmd
					    // else: cmd |line_shift
		string file_name;

		CMD(){
			argc = 1;
			out_type = 0;
			line_shift = 0;
		}
};

class PIPE
{
	public:
		int pi[2];
		bool in_use;
		int counter;

		PIPE()
		{
			pi[0] = -1;
			pi[1] = -1;
			in_use = false;
			counter = 0;
		}
};

void str_echo(int sockfd);
int readline(int fd,char *ptr, int maxlen);
void server_function(int newsockfd);
void init();
void print_welcome_message();
std::set<string> env_vars;

void sig_fork(int signo)
{
    pid_t pid;
    int stat;
    pid=waitpid(0,&stat,WNOHANG);
    
    return;
}

int main(int argc , char *argv[])
{
	int sockfd , client_sockfd , c , read_size, childpid;
	struct sockaddr_in serv_addr, client;
	char client_message[2000];
	int port_num = 6633;
	
	
	//Create socket
	sockfd = socket(AF_INET , SOCK_STREAM , 0);
	if (sockfd == -1)
	{
		printf("Could not create socket");
	}
	puts("Socket created");
	
	//Prepare the serv_addr_in structure
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = INADDR_ANY;
	serv_addr.sin_port = htons(port_num);
	
	//Bind
	if( bind(sockfd,(struct sockaddr *) &serv_addr , sizeof(serv_addr)) < 0)
	{
		//print the error message
		perror("bind failed. Error");
		return 1;
	}
	puts("bind done");
	
	//Listen
	listen(sockfd , 3);
	
	// Accept and incoming connection
	puts("Waiting for incoming connections...");
	c = sizeof(struct sockaddr_in);

	// prevent child zombie process
	(void) signal( SIGCHLD, sig_fork); 
	
	//accept connection from an incoming client
	while(1)
	{
		client_sockfd = accept(sockfd, (struct sockaddr *) &client, (socklen_t*) &c);
		if (client_sockfd < 0)
		{
			perror("accept failed");
			return 1;
		}
		puts("Connection accepted.");
		
		//Receive a message from client
		childpid = fork();

		if(childpid < 0)
			perror("server: fork error");
		else if(childpid == 0){

    		close(sockfd);

    		server_function(client_sockfd);

    		close(client_sockfd);
    		
    		exit(0);
    		
		}
		close(client_sockfd);
	}
	
	return 0;
}





void server_function(int newsockfd)
{
	//Reset the sinal proceesing method to default
	(void) signal(SIGCHLD,SIG_DFL);
	// close stdin stdout stderr in fd table
	// and set fd[0] fd[1] fd[2] to newsockfd
	for(int i=0; i<3; i++)
	{
		close(i);
		dup(newsockfd);
	}
	
	init();
	print_welcome_message();
	int num_line = -1;
	std::vector<PIPE> pipe_vector;
	int child_pid;
	int original_input = dup(0);
	int original_output = dup(1);
	bool *hop_record = new bool[1001];


	PIPE pipes[1000];

	for(int i=0;i<1001;i++)
		hop_record[i] = false;

	bool do_exit = false;

	while( !do_exit )
	{
		cout << "% ";
		string line;
		stringstream ss_line;
		stringstream ss_for_unknown;
		getline(cin,line);
		num_line++;
	
		num_line = num_line % 1000;

		ss_line << line;
		ss_for_unknown << line;
		int i_th_cmd = 0;

		string un_str;
		ss_for_unknown >> un_str;
		if(un_str == "exit")
			break;
		// check if command is unknown by examing filename in environment variables
        DIR *dir;
        struct dirent *ent;
        string* cmd_list;
        bool is_known = false;
        int num_file = 0;

        for(set<string>::iterator it = env_vars.begin(); it != env_vars.end(); ++it){
	        
	        if ((dir = opendir ((*it).c_str())) != NULL) {
	          	
	            while ((ent = readdir (dir)) != NULL) {
	                string filestr(ent->d_name);
	                if(un_str == filestr || un_str == "printenv" || un_str == "setenv"){
						is_known = true;
	            		break;
	            	}
	            }
	            closedir(dir);   
	        } else {
	 
	                cout << "Can't open directory" << endl;
	        }
	    }

        if(is_known)
       	{
       		for(vector<PIPE>::iterator it=pipe_vector.begin(); it != pipe_vector.end(); ++it){
       			if(it->counter > 0){
	       			hop_record[it->counter] = false;
	       			it->counter--;
	       			hop_record[it->counter] = true;
	       		}
	       		else if(it->counter == 0){
	       			hop_record[it->counter] = false;

	       		}
       		}
       	}
		
       	
		string cmd_str;

		while(ss_line >> cmd_str) {

			if(cmd_str == "exit") {
				do_exit = true;
				break;
			}

			DIR *dir1;
        	struct dirent *ent1;
       		bool is_known1 = false;
       		for(set<string>::iterator it = env_vars.begin(); it != env_vars.end(); ++it){
		      
		        if (( dir1 = opendir((*it).c_str()) ) != NULL) {
		          	
		            while ((ent1 = readdir (dir1)) != NULL) {
		                string filestr(ent1->d_name);
		                if(un_str == filestr || un_str == "printenv" || un_str == "setenv"){
							is_known1 = true;
		            		break;
		            	}
		            }
		            closedir(dir1);

		        } else {

		                cout << "Can't open directory" << endl;
		        }
		    }

	        if(is_known1 == false)
	        {
	        	PIPE temp;
        	
        	
        		temp.pi[0] = pipes[999].pi[0];
        		temp.pi[1] = pipes[999].pi[1];
        		temp.in_use = true;
		
 	        	for(int i=999; i > 0; i--){
	        	
        			pipes[(i)%1000].pi[0] = pipes[i-1].pi[0];
	        		pipes[(i)%1000].pi[1] = pipes[i-1].pi[1];
	        		pipes[(i)%1000].in_use = true;
	        		pipes[i-1].in_use = false;
	        		
	        		
	        	}

        		pipes[0].pi[0] = temp.pi[0];
        		pipes[0].pi[1] = temp.pi[1];
        		pipes[0].in_use = temp.in_use;
	        	
	        	cout << "Unknown command : [" << cmd_str << "]." << endl;
	        	break;
	        }

			stringstream ss_cmd;
			ss_cmd << cmd_str << "\n";
			i_th_cmd++;
			CMD cmd;
			bool is_append = false;

			while(ss_line >> cmd_str)
			{
				if(cmd_str == "|"){
					cmd.out_type = 1;
					cmd.line_shift = 0;
					break;
				}
				else if(cmd_str == "!"){
					cmd.out_type = 2;
					cmd.line_shift = 0;
					break;
				}
				else if(cmd_str == ">"){
					cmd.out_type = 3;
					ss_line >> cmd.file_name;
					is_append = false;
					break;
				}
				else if(cmd_str == ">>"){
					cmd.out_type = 3;
					ss_line >> cmd.file_name;
					is_append = true;
					break;
					
				}
				else if(cmd_str[0] == '|' || cmd_str[0] == '!'){
					stringstream eat_c;
					char c;
					eat_c << cmd_str;
					eat_c >> c >> cmd.line_shift;
					cmd.out_type = (cmd_str[0] == '|') ? 1 : 2;

				}
				else{
					ss_cmd << cmd_str << '\n';
					cmd.argc++;
				}
			}

			cmd.argv = new char* [cmd.argc+1];
			for(int i=0; i < cmd.argc; i++)
			{
				string arg;
				ss_cmd >> arg;
				// if(arg != "printenv" && arg != "setenv" && i == 0 )
				// 	arg = "bin/" + arg;

				char* char_str = new char[arg.size()+1];
				strcpy(char_str,arg.c_str());
				cmd.argv[i] = char_str;
			}
			cmd.argv[cmd.argc] = NULL;
			
			if( !strcmp(cmd.argv[0],"setenv") )
			{
				setenv(cmd.argv[1],cmd.argv[2],1);
				// update environment variable path (only valid for setting single variable)
				env_vars.clear();
				string env_str(cmd.argv[2]);
				env_vars.insert(env_str);
				//clear both stringstream
				ss_line.str("");
				ss_line.clear();
				ss_cmd.str("");
				ss_cmd.clear();
				continue;
			}
			else if( !strcmp(cmd.argv[0],"printenv") )
			{
				for(int i=1; i < cmd.argc; i++)
				{
					char* path = getenv(cmd.argv[i]);
					if(path != NULL)
						cout << cmd.argv[i] << "=" << path << endl;
					else
						cout << "Can't find " << cmd.argv[1] << endl;

				}
				ss_line.str("");
				ss_line.clear();
				ss_cmd.str("");
				ss_cmd.clear();
				continue;
			}
			int file_fd;
			PIPE inter_cmd_pipe;
			PIPE new_inter_line_pipe;
			
			bool many_to_dest = false;

			
			if(cmd.out_type == 3)   //  >  case
			{  
				if(is_append == false){	
					if((file_fd = open(cmd.file_name.c_str(), O_CREAT|O_WRONLY, 0777)) < 0)
						cerr << "Can't open file: " << cmd.file_name << endl;
				}
				else{  // is_append is true => open file with append config
					if((file_fd = open(cmd.file_name.c_str(), O_APPEND|O_WRONLY, 0777)) < 0)
						cerr << "Can't open file: " << cmd.file_name << endl;
				}	
			}
			if(cmd.out_type != 0 && cmd.line_shift == 0) //  cmd1 | cmd2  (the condition with pipe(|, !, >))
			{
				if(pipe(inter_cmd_pipe.pi) < 0)
					cerr << "Can't create pipes." << endl;
				else
					inter_cmd_pipe.in_use = true;

			}
			else if(cmd.out_type != 0 && cmd.line_shift != 0) // cmd |N   or cmd !N (N is cmd.line_shift)
			{
				// check if there is pipe which has the same # of hops to go
				if(hop_record[cmd.line_shift] == true)
					many_to_dest = true; //there are same hops to go, no need to create new pipe
				else{
					// create new pipe
					if(pipe(new_inter_line_pipe.pi) < 0)
						cerr << "Can't create pipes.\n";
					else{
						new_inter_line_pipe.counter = cmd.line_shift;
						new_inter_line_pipe.in_use = true;
						hop_record[cmd.line_shift] = true;
						pipe_vector.push_back(new_inter_line_pipe);
						close(new_inter_line_pipe.pi[0]);
						close(new_inter_line_pipe.pi[1]);
					}
				}	
				if ( pipes[(num_line+cmd.line_shift)%1000].in_use == false )
				{
					many_to_dest = false;
					if ( pipe(pipes[(num_line+cmd.line_shift)%1000].pi) < 0)
						cerr << "Can't create pipes.\r\n";
					
					pipes[(num_line+cmd.line_shift)%1000].in_use = true;
				}
				else
					many_to_dest = true;	
					
					
			}
			
			// fork child process for exec
			if((child_pid = fork()) < 0)
				cerr << "Can't fork\r\n";
			else if(child_pid == 0)  // child
			{
				close(0);
				

				if(pipes[num_line].in_use)
					dup(pipes[num_line].pi[0]);
				else
					dup(original_input);				
			
				// set output: write to pipeIn
				close(1);
				
				if ( cmd.out_type == 0){
					dup(original_output);
				}
				else if ( cmd.out_type == 3)	// >
				{
					dup(file_fd);
					close(file_fd);
				}
				else if ( cmd.line_shift == 0 )  // cmd1 | cmd2
				{							
					dup(inter_cmd_pipe.pi[1]);
					close(inter_cmd_pipe.pi[0]);
					close(inter_cmd_pipe.pi[1]);
				}
				else if ( cmd.line_shift != 0 && cmd.out_type == 1){		// cmd |N   (excluding cmd !N)

					dup(pipes[(cmd.line_shift+num_line)%1000].pi[1]);
					
				}
				else{    //   cmd !N
					dup(original_output);
				}
				// set error	
				close(2);
			
				if(cmd.out_type == 2){

						if(cmd.line_shift > 0)
							dup(pipes[(cmd.line_shift+num_line)%1000].pi[1]);
						else
							dup(1);
				}
				else
					dup(original_output);

				for(int i=0; i < 1000; i++)
				{
					close(pipes[i].pi[0]);
					close(pipes[i].pi[1]);
				}

				// find where the command execution file is in
				DIR *dir;
        		struct dirent *ent;
        		bool is_known = false;
        		string cmd_name(cmd.argv[0]);
        		string path_to_append;
        		string concat_cmd_path;   // put it in the first argument of execvp function

        		for(set<string>::iterator it = env_vars.begin(); it != env_vars.end(); ++it){
	        
	        		bool dir_found = false;
		        	if (( dir = opendir((*it).c_str()) ) != NULL) {
		          	
		            	while ((ent = readdir (dir)) != NULL) {
		                	string filestr(ent->d_name);
		                	if(cmd_name == filestr){
								path_to_append = *it;
								dir_found = true;
								concat_cmd_path = path_to_append + "/" + filestr;
		            			break;
		            		}
		            	}
		            	closedir(dir);   
		        	} else {

		                cout << "Can't open directory" << endl;
		        	}
		        	if(dir_found == true)
		        		break;
	    		}
	    		

				// command execution
				int int_execvp;
				if ( (int_execvp = execvp(concat_cmd_path.c_str(),cmd.argv)) < 0)
				{

					close(1);
					dup(original_output);

					// stringstream eat_bin;
					// eat_bin << cmd.argv[0];
					// char b;
					// eat_bin >> b; eat_bin >> b; eat_bin >> b; eat_bin >> b;
					// string trimmed_cmd_name;
					// eat_bin >> trimmed_cmd_name;
					// strcpy(cmd.argv[0],trimmed_cmd_name.c_str());
					cout << "Unknown command: [" << cmd.argv[0] << "]." << endl;

					close(0); 
					close(1);
					close(2);
					exit(-1);
				}
			}
			else   // parent
			{				
				close(pipes[num_line].pi[0]);
				close(pipes[num_line].pi[1]);
				pipes[num_line].in_use = false;

				if ( cmd.out_type == 3)	// >
					close(file_fd);	
				else if( cmd.out_type != 0 && cmd.line_shift == 0){  // cmd1 | cmd2 , cmd1 ! cmd2

						pipes[num_line].pi[0] = inter_cmd_pipe.pi[0];
						pipes[num_line].pi[1] = inter_cmd_pipe.pi[1];
						inter_cmd_pipe.in_use = true;
						pipes[num_line].in_use = true;

				}
				int status;

				while(waitpid(child_pid, &status, WNOHANG) != child_pid)  ;

				if(WIFEXITED(status) == false){

					close(pipes[num_line].pi[0]);
					close(pipes[num_line].pi[1]);
					pipes[num_line].in_use = false;

					if(cmd.out_type !=0 && cmd.line_shift != 0)
					{
						if(many_to_dest == false)  // only one previous command is piped to dest_line
						{
							close(pipes[(num_line+cmd.line_shift)%1000].pi[0]);
							close(pipes[(num_line+cmd.line_shift)%1000].pi[1]);
							pipes[(num_line+cmd.line_shift)%1000].in_use = false;
						}
					}
					ss_line.str("");
					ss_line.clear();
					continue;					
				}				
			}
		}
		if(do_exit)
			break;
	}
}

void init()
{
    setenv("PATH","bin:.",1);
    env_vars.insert("bin");
    env_vars.insert(".");
	chdir("ras");
}

void print_welcome_message()
{
	cout << "****************************************" << endl;
	cout << "** Welcome to the information server. **" << endl;
	cout << "****************************************" << endl;
}
