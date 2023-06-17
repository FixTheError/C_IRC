#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <string.h>
#include <regex.h>
#include <pthread.h>
#include <semaphore.h>
#include <poll.h>

#define MESSAGE_SIZE 512
int q;
sem_t mutex;

struct arg {
	int sk;
};

void listener(void* s) {
	struct arg* s_arg = (struct arg*) s;
	int sock = s_arg->sk;
	
	while (1) {
		char buf[MESSAGE_SIZE];
		//struct pollfd psock;
		//psock.fd = sock;
		//psock.events = POLLIN;
		//int r = poll(&psock, 1, 0);
		//if (psock.revents & POLLIN) {
			//sem_wait(&mutex);
			if (recv(sock, buf, MESSAGE_SIZE - 1, 0) == -1) {
				perror("client: recv");
			}

			printf("%s\n", buf);
			sem_wait(&mutex);
			if (q == 1) {
				break;
			}
			sem_post(&mutex);
		//}
	}
}

void main(int argc, char* argv[]) {
	int sock, num; //fd's for sockets
	struct addrinfo clientinfo, * result, * pointer;
	struct sockaddr_storage client_addr;
	socklen_t addr_size;
	char IPchar[INET6_ADDRSTRLEN];
	int getaddr_ret;
	int dumb;
	q = 0;
	pthread_t thred;
	sem_init(&mutex, 0, 1);

	regex_t quit_cmd;
	if (regcomp(&quit_cmd, "^QUIT.*$", REG_EXTENDED) != 0) {
		perror("err regcomp: QUIT");
	}

	if (argc != 3) { //make sure to include server ip and port
		fprintf(stderr, "usage: ./<client> <server-ip> <server-port>\n");
		exit(2);
	}

	//allocate and set up address parameters
	memset(&clientinfo, 0, sizeof clientinfo);	clientinfo.ai_family = AF_UNSPEC;
	clientinfo.ai_socktype = SOCK_STREAM;
	clientinfo.ai_flags = AI_PASSIVE;

	//get a linked-list of possible addresses that match given parameters	if ((getaddr_ret = getaddrinfo(argv[1], argv[2], &clientinfo, &result)) != 0) {		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(getaddr_ret));
		exit(1);	}	//loop through results until one works	for (pointer = result; pointer != NULL; pointer = pointer->ai_next) {
		if ((sock = socket(pointer->ai_family, pointer->ai_socktype, pointer->ai_protocol)) == -1) {
			perror("client: socket");
			continue;
		}
		if (connect(sock, pointer->ai_addr, pointer->ai_addrlen) == -1) {
			close(sock);
			perror("client: connect");
			continue;		}
		break;	}

	freeaddrinfo(result);
	//make sure connection was found
	if (pointer == NULL) { 
		fprintf(stderr, "client: failed to connect\n");
		exit(1);
	}

	struct arg t_arg;
	t_arg.sk = sock;
	pthread_create(&thred, NULL, listener, &t_arg);
	
	char* stupid = malloc(MESSAGE_SIZE);
	while (1) {
		struct pollfd infd;
		infd.fd = fileno(stdin);
		infd.events = POLLIN;
		int r = poll(&infd, 1, 0);
		if (infd.revents & POLLIN) {
			char* fgr = fgets(stupid, MESSAGE_SIZE, stdin);
			if (fgr != NULL) {
				//sem_wait(&mutex);
				if ((dumb = send(sock, stupid, MESSAGE_SIZE - 1, 0)) == -1) {
					perror("client: send");
				}
				//printf("%s\n", stupid);
				//sem_post(&mutex);
				if (regexec(&quit_cmd, stupid, 0, NULL, 0) == 0) {
					sem_wait(&mutex);
					q = 1;
					sem_post(&mutex);
					break;
				}
			}
		}
		
		
	}
	pthread_join(&thred, NULL);
	close(sock);
	exit(0);
}