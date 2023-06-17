#include <stdio.h>
#include <pthread.h>
#include <semaphore.h>
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
#include <time.h>
#include <poll.h>

#define BACKLOG 10
#define MESSAGE_SIZE 512

sem_t mutex;
int num_clients;

struct par {
	int shoe, dumb;
	char* IPchar;
};

struct ctnode { // linked list for keeping track of sockets
	int sock;
	char* nick;
	char* user;
	char* name;
	int socknum;
	char* IPchar;
	int free;
};

struct channel{
	char* name;
	char* topic;
	struct ctnode** chan_users;
	int cap, num_users;
};

struct ctnode* dyn_socks;
struct channel* dyn_chan;
int num_chan;
int chan_cap;

void* get_in_addr(struct sockaddr* sock){
	if (sock->sa_family == AF_INET) {
		return &(((struct sockaddr_in*)sock)->sin_addr);
	}
	return &(((struct sockaddr_in6*)sock)->sin6_addr);
}

void cthandler(void* args) { //each thread will handle 1 client
	int nread;
	char buf[MESSAGE_SIZE];
	struct ctnode csock;
	regex_t nick_cmd;
	regex_t user_cmd;
	regex_t topic_cmd;
	regex_t quit_cmd;
	regex_t part_cmd;
	regex_t join_cmd;
	struct par* parameters = (struct par*) args;

	if (regcomp(&nick_cmd, "^NICK [a-z0-9A-Z]+$", REG_EXTENDED) != 0) {
		perror("err regcomp: NICK");
	}
	if (regcomp(&user_cmd, "^USER [a-z0-9A-Z]+ [0-8] \\* :.+$", REG_EXTENDED) != 0) {
		perror("err regcomp: USER");
	}
	if (regcomp(&quit_cmd, "^QUIT.*$", REG_EXTENDED) != 0) {
		perror("err regcomp: QUIT");
	}
	if (regcomp(&topic_cmd, "^TOPIC.*$", REG_EXTENDED) != 0) {
		perror("err regcomp: TOPIC");
	}
	if (regcomp(&join_cmd, "^JOIN.*$", REG_EXTENDED) != 0) {
		perror("err regcomp: JOIN");
	}
	if (regcomp(&part_cmd, "^PART.*$", REG_EXTENDED) != 0) {
		perror("err regcomp: PART");
	}
	
	csock.sock = parameters->shoe;
	csock.socknum = parameters->dumb;
	csock.IPchar = parameters->IPchar;
	csock.user = "\0";
	csock.nick = "\0";
	csock.free = 0;
	sem_wait(&mutex);
	dyn_socks[csock.socknum] = csock;
	int chan_dumb = 0;
	while (dyn_chan[0].chan_users[chan_dumb] != NULL) {
		chan_dumb = chan_dumb + 1;
	}
	dyn_chan[0].chan_users[chan_dumb] = &dyn_socks[csock.socknum];
	sem_post(&mutex);
	while (1) {
		struct pollfd psock;
		psock.fd = dyn_socks[csock.socknum].sock;
		psock.events = POLLIN;
		int r = poll(&psock, 1, 0);
		if (psock.revents & POLLIN) {
			//sem_wait(&mutex);
			if ((nread = recv(dyn_socks[csock.socknum].sock, buf, MESSAGE_SIZE - 1, 0)) == -1) {
				perror("server: recv");
			}
			//sem_post(&mutex);
			buf[nread] = '\0';
			printf("message: %s\n", buf);
			//handle NICK
			if (regexec(&nick_cmd, buf, 0, NULL, 0) == 0) {
				char* tmp = buf + 6;
				//did the user give a nickname?
				if (strcmp(tmp, "\0") == 0) {
					char* err_msg = "431 ERR_NONICKNAMEGIVEN";
					//sem_wait(&mutex);
					if ((send(dyn_socks[csock.socknum].sock, err_msg, strlen(err_msg), 0)) == -1) {
						perror("client: send");
					}
					//sem_post(&mutex);
				}
				int inuse = 0;
				//check every user to make sure the given nickname is not taken
				for (int i = 0; i < num_clients; i++) {
					sem_wait(&mutex);
					if (strcmp(tmp, dyn_socks[i].nick) == 0) {
						sem_post(&mutex);
						inuse = 1;
						char* err_msg = "433 ERR_NICKNAMEINUSE";
						if ((send(dyn_socks[csock.socknum].sock, err_msg, strlen(err_msg), 0)) == -1) {
							perror("client: send");
						}

						break;
					}
					sem_post(&mutex);
				}
				//change the nickname if user is registered and nickname is not taken
				if ((inuse == 0) && (strcmp(dyn_socks[csock.socknum].user, "\0") != 0)) {
					sem_wait(&mutex);
					dyn_socks[csock.socknum].nick = malloc(strlen(tmp));
					strcpy(dyn_socks[csock.socknum].nick, tmp);
					sem_post(&mutex);
					//printf("Welcome %s\n", dyn_socks[csock.socknum].nick);
				}
			}
			//handle USER command
			else if (regexec(&user_cmd, buf, 0, NULL, 0) == 0) {
				char* tok;
				char* save = buf;
				int tok_count = 0;
				int collision = -1;
				while (tok_count < 4) {
					tok = strtok_r(save, " ", &save);
					tok_count = tok_count + 1;
					if (tok_count == 2) {
						for (int i = 0; i < num_clients; i++) {
							if (strcmp(tok, dyn_socks[csock.socknum].user) == 0) {
								collision = i;
								break;
							}
						}
						if (collision == -1) {
							sem_wait(&mutex);
							dyn_socks[csock.socknum].user = malloc(strlen(tok));
							strcpy(dyn_socks[csock.socknum].user, tok);
							sem_post(&mutex);
						}
					}
				}

				if (collision == -1) {
					char* tmp1 = save + 1;
					sem_wait(&mutex);
					dyn_socks[csock.socknum].name = malloc(strlen(tmp1));
					strcpy(dyn_socks[csock.socknum].name, tmp1);
					sem_post(&mutex);
					char* wlcm_msg = malloc(MESSAGE_SIZE);
					char* hname = malloc(255);
					if ((gethostname(hname, 255) == -1)) {
						perror("err: host name");
					}
					sprintf(wlcm_msg, ":%s 001 %s :Welcome to the CS447 IRC %s!%s@%s", hname, dyn_socks[csock.socknum].user, dyn_socks[csock.socknum].user, dyn_socks[csock.socknum].user, dyn_socks[csock.socknum].IPchar);
					//sem_wait(&mutex);
					if (send(dyn_socks[csock.socknum].sock, wlcm_msg, strlen(wlcm_msg), 0) == -1) {
						perror("client: send");
					}
					//sem_post(&mutex);
				}

				else {
					char* err_msg = "462 ERR_ALREADYREGISTERED : that username is taken";
					//sem_wait(&mutex);
					if (send(dyn_socks[csock.socknum].sock, err_msg, strlen(err_msg), 0) == -1) {
						perror("client: send");
					}
					//sem_post(&mutex);
				}
			}
			//handle QUIT
			else if (regexec(&quit_cmd, buf, 0, NULL, 0) == 0) {

				char* quit_msg = malloc(MESSAGE_SIZE);
				char* tok;
				char* save = buf;
				tok = strtok_r(save, " ", &save);

				sprintf(quit_msg, "User %s has quit the IRC %s\n", dyn_socks[csock.socknum].user, save);

				if (send(dyn_socks[csock.socknum].sock, quit_msg, strlen(quit_msg), 0) == -1) {
					perror("client: send");
				}
				sem_wait(&mutex);

				dyn_socks[csock.socknum].free = 1;
				dyn_socks[csock.socknum].nick = "\0";
				dyn_socks[csock.socknum].IPchar = "\0";

				for (int i = 0; i < chan_cap; i++) {
					for (int j = 0; j < dyn_chan[i].cap; j++) {
						if ((dyn_chan[i].chan_users[j] != NULL) && (strcmp(dyn_socks[csock.socknum].user, dyn_chan[i].chan_users[j]->user) == 0)) {
							dyn_chan[i].chan_users[j] = NULL;
						}
					}
				}
				dyn_socks[csock.socknum].user = "\0";
				close(dyn_socks[csock.socknum].sock);
				sem_post(&mutex);
				break;
			}
			//handle topic
			else if (regexec(&topic_cmd, buf, 0, NULL, 0) == 0) {
				int found_chan = -1;
				char* topic_msg = malloc(MESSAGE_SIZE);
				char* tok;
				char* save = buf;
				tok = strtok_r(save, " ", &save);
				//did the user include a channel name?
				if ((tok = strtok_r(save, "\n", &save)) != NULL) {
					sem_wait(&mutex);
					for (int i = 0; i < num_chan; i++) {
						if (strcmp(tok, dyn_chan[i].name) == 0) {
							found_chan = i;
						}
					}

					sem_post(&mutex);
					//was the channel found?

					if (found_chan != -1) {

						int found_user = 0;
						int j = 0;
						sem_wait(&mutex);

						while (j < dyn_chan[found_chan].cap) {

							if (dyn_chan[found_chan].chan_users[j] != NULL) {
								if (strcmp(dyn_socks[csock.socknum].user, dyn_chan[found_chan].chan_users[j]->user) == 0) {
									found_user = 1;
								}
							}
							j = j + 1;
						}

						sem_post(&mutex);

						//is the user in this channel?
						if (found_user == 1) {
							//check?
							if (strcmp(save, "\0") == 0) {
								time_t ltime;
								struct tm* loc_time;
								time(&ltime);
								loc_time = localtime(&ltime);
								char* test = "time";
								sem_wait(&mutex);
								if (strcmp(test, dyn_chan[found_chan].topic) == 0) {
									sprintf(topic_msg, "332 RPL_TOPIC %s :%s", dyn_chan[found_chan].name, asctime(loc_time));
								}
								else if (strcmp(dyn_chan[found_chan].topic, "\0") != 0) {
									sprintf(topic_msg, "332 RPL_TOPIC %s :%s", dyn_chan[found_chan].name, dyn_chan[found_chan].topic);
								}
								else {
									topic_msg = "331 RPL_NOTOPIC :No topic set";
								}
								sem_post(&mutex);
								if (send(dyn_socks[csock.socknum].sock, topic_msg, strlen(topic_msg), 0) == -1) {
									perror("client: send");
								}

							}
							else {
								sem_wait(&mutex);
								//change or clear?
								if (strcmp(save, ":") == 0) {
									sprintf(topic_msg, "%s has cleared the topic for %s", dyn_chan[found_chan].chan_users[j]->user, dyn_chan[found_chan].name);
									dyn_chan[found_chan].topic = "\0";
								}
								else {
									char* top = save + 1;
									sprintf(topic_msg, "%s has changed the topic for %s to %s", dyn_chan[found_chan].chan_users[j]->user, dyn_chan[found_chan].name, save);
									dyn_chan[found_chan].topic = top;
								}
								sem_post(&mutex);
								//send message to all users on this channel
								for (int k = 0; k < dyn_chan[found_chan].cap; k++) {
									if (dyn_chan[found_chan].chan_users[k] != NULL) {
										if (send(dyn_chan[found_chan].chan_users[k]->sock, topic_msg, strlen(topic_msg), 0) == -1) {
											perror("client: send");
										}
									}
								}
							}
						}
						//user was not on channel
						else {
							sprintf(topic_msg, "442 ERR_NOTONCHANNEL %s :You're not in this channel", tok);
							if (send(dyn_socks[csock.socknum].sock, topic_msg, strlen(topic_msg), 0) == -1) {
								perror("client: send");
							}
						}
					}
					//channel not found
					else {
						sprintf(topic_msg, "403 ERR_NOSUCHCHANNEL %s :No such channel exists", tok);
						//sem_wait(&mutex);
						if (send(dyn_socks[csock.socknum].sock, topic_msg, strlen(topic_msg), 0) == -1) {
							perror("client: send");
						}
						//sem_post(&mutex);
					}
				}
				//user did not include channel name
				else {
					topic_msg = "461 ERR_NEEDMOREPARAMS :Too few parameters";
					//sem_wait(&mutex);
					if (send(dyn_socks[csock.socknum].sock, topic_msg, strlen(topic_msg), 0) == -1) {
						perror("client: send");
					}
					//sem_post(&mutex);
				}
			}
			//handle join
			else if (regexec(&join_cmd, buf, 0, NULL, 0) == 0) {
				char* join_msg = malloc(MESSAGE_SIZE);
				char* tok;
				char* save = buf;
				char* tok_chan;
				char* save_chan;
				tok = strtok_r(save, " ", &save);
				//did user include channel name/names?
				if ((tok = strtok_r(save, "\n", &save)) != NULL) {
					save_chan = tok;
					while ((tok_chan = strtok_r(save_chan, ",", &save_chan)) != NULL) {
						int i = 0;
						sem_wait(&mutex);
						//find channel
						while ((i < num_chan) && (strcmp(tok, dyn_chan[i].name) != 0)) {
							i = i + 1;
						}
						sem_post(&mutex);
						//was the channel found?
						if (i < num_chan) {
							int j = 0;
							sem_wait(&mutex);
							dyn_chan[i].num_users = dyn_chan[i].num_users + 1;
							//make room if needed
							if (dyn_chan[i].num_users > dyn_chan[i].cap) {
								dyn_chan[i].cap = dyn_chan[i].cap + 10;
								struct ctnode** temp = malloc(dyn_chan[i].cap * sizeof(struct ctnode*));
								for (int l = 0; l < (dyn_chan[i].cap - 10); l++) {
									dyn_chan[i].chan_users[l] = temp[i];
								}
								for (int k = dyn_chan[i].cap - 10; k < 10; k++) {
									dyn_chan[i].chan_users[k] = NULL;
								}
								if (dyn_chan[i].chan_users == NULL) {
									perror("err dyn_chan[i]: reallocation failed");
									exit(1);
								}
								free(dyn_chan[i].chan_users);
								dyn_chan[i].chan_users = temp;
								free(temp);
							}
							//find empty
							while (dyn_chan[i].chan_users[j] != NULL) {
								j = j + 1;
							}
							//put user in empty slot
							dyn_chan[i].chan_users[j] = &dyn_socks[csock.socknum];
							sem_post(&mutex);
							time_t ltime;
							struct tm* loc_time;
							time(&ltime);
							loc_time = localtime(&ltime);
							char* test = "#time";
							char* topic_msg = malloc(MESSAGE_SIZE);
							//check topic
							sem_wait(&mutex);
							if (strcmp(dyn_chan[i].topic, test) == 0) {
								sprintf(topic_msg, "332 RPL_TOPIC %s :%s", dyn_chan[i].name, asctime(loc_time));
							}
							else if (strcmp(dyn_chan[i].topic, "\0") != 0) {
								sprintf(topic_msg, "332 RPL_TOPIC %s :%s", dyn_chan[i].name, dyn_chan[i].topic);
							}
							else {
								topic_msg = "331 RPL_NOTOPIC :No topic set";
							}
							sem_post(&mutex);
							sprintf(join_msg, "Welcome %s to %s\n%s", dyn_socks[csock.socknum].user, dyn_chan[i].name, topic_msg);
							//send message to all users on this channel
							for (int k = 0; k < dyn_chan[i].cap; k++) {
								if (dyn_chan[i].chan_users[k] != NULL) {
									if (send(dyn_chan[i].chan_users[k]->sock, join_msg, strlen(join_msg), 0) == -1) {
										perror("client: send");
									}
								}
							}
						}
						//channel not found
						else {
							sprintf(join_msg, "403 ERR_NOSUCHCHANNEL %s :No such channel exists", tok_chan);
							//sem_wait(&mutex);
							if (send(dyn_socks[csock.socknum].sock, join_msg, strlen(join_msg), 0) == -1) {
								perror("client: send");
							}
							//sem_post(&mutex);
						}
					}
				}
				//user did not include channel name
				else {
					join_msg = "461 ERR_NEEDMOREPARAMS :Too few parameters";
					//sem_wait(&mutex);
					if (send(dyn_socks[csock.socknum].sock, join_msg, strlen(join_msg), 0) == -1) {
						perror("client: send");
					}
					//sem_post(&mutex);
				}
			}
			//Handle part
			else if (regexec(&part_cmd, buf, 0, NULL, 0) == 0) {
				char* part_msg = malloc(MESSAGE_SIZE);
				char* tok;
				char* save = buf;
				char* tok_chan;
				char* save_chan;
				tok = strtok_r(save, " ", &save);
				//did user include a channel name?
				if ((tok = strtok_r(save, " ", &save)) != NULL) {

					save_chan = tok;
					tok_chan = strtok_r(save_chan, "\n", &save_chan);
					while ((tok_chan = strtok_r(save_chan, ",", &save_chan)) != NULL) {
						int i = 0;
						sem_wait(&mutex);
						//find channel
						while ((i < num_chan) && (strcmp(tok_chan, dyn_chan[i].name) != 0)) {
							i = i + 1;
						}

						sem_post(&mutex);
						//was the channel found?
						if (i < num_chan) {

							int found_user = 0;
							int user_ind;
							int j = 0;
							sem_wait(&mutex);
							while (j < dyn_chan[i].cap) {
								if (dyn_chan[i].chan_users[j] != NULL) {
									if (strcmp(dyn_socks[csock.socknum].user, dyn_chan[i].chan_users[j]->user) == 0) {
										found_user = 1;
										user_ind = j;
									}
								}
								j = j + 1;
							}
							sem_post(&mutex);
							//is the user in this channel?
							if (found_user == 1) {

								sprintf(part_msg, " %s has left the channel %s with message %s", dyn_socks[csock.socknum].user, dyn_chan[i].name, save);
								//send message to all users on this channel
								for (int k = 0; k < dyn_chan[i].cap; k++) {
									if (dyn_chan[i].chan_users[k] != NULL) {
										if (send(dyn_chan[i].chan_users[k]->sock, part_msg, strlen(part_msg), 0) == -1) {
											perror("client: send");
										}
									}
								}
								sem_wait(&mutex);
								dyn_chan[i].chan_users[user_ind] = NULL;
								dyn_chan[i].num_users = dyn_chan[i].num_users - 1;
								sem_post(&mutex);
							}
							//user was not on channel
							else {
								//sem_wait(&mutex);
								sprintf(part_msg, "442 ERR_NOTONCHANNEL %s :You're not in this channel", tok);
								if (send(dyn_socks[csock.socknum].sock, part_msg, strlen(part_msg), 0) == -1) {
									perror("client: send");
								}
								//sem_post(&mutex);
							}
						}
						//channel not found
						else {
							sprintf(part_msg, "403 ERR_NOSUCHCHANNEL %s :No such channel exists", tok_chan);
							//sem_wait(&mutex);
							if (send(dyn_socks[csock.socknum].sock, part_msg, strlen(part_msg), 0) == -1) {
								perror("client: send");
							}
							//sem_post(&mutex);
						}
					}

				}
				//user did not include channel name
				else {
					part_msg = "461 ERR_NEEDMOREPARAMS :Too few parameters";
					//sem_wait(&mutex);
					if (send(dyn_socks[csock.socknum].sock, part_msg, strlen(part_msg), 0) == -1) {
						perror("client: send");
					}
					//sem_post(&mutex);
				}
			}
			else {
				char* pass_msg = malloc(MESSAGE_SIZE);
				int f = 0;
				int c;
				for (int i = 0; i < chan_cap; i++) {
					sem_wait(&mutex);
					for (int j = 0; j < dyn_chan[i].cap; j++) {
						if ((dyn_chan[i].chan_users[j] != NULL) && (strcmp(dyn_socks[csock.socknum].user, dyn_chan[i].chan_users[j]->user) == 0)) {
							f = 1;
						}
					}
					sem_post(&mutex);
					if (f == 1) {
						c = i;
						break;
					}
				}
				//sem_wait(&mutex);
				for (int k = 0; k < dyn_chan[c].cap; k++) {
					if ((dyn_chan[c].chan_users[k] != NULL) && (strcmp(dyn_chan[c].chan_users[k]->user, dyn_socks[csock.socknum].user) != 0)) {
						char* tmp_buf = malloc(strlen(buf));
						sprintf(pass_msg, "%s to %s :%s", dyn_socks[csock.socknum].user, dyn_chan[c].name, tmp_buf);
						if (send(dyn_chan[c].chan_users[k]->sock, pass_msg, strlen(pass_msg), 0) == -1) {
							perror("client: send");
						}
					}
				}
				//sem_post(&mutex);
			}
		}
	}

}

int main(int argc, char* argv[]) {
	int sock, shoe; //fd's for sockets
	struct addrinfo servinfo, * result, * pointer;
	struct sockaddr_storage client_addr;
	socklen_t addr_size;
	char IPchar[INET6_ADDRSTRLEN];
	int getaddr_ret;
	int dumb = 0;

	sem_init(&mutex, 0, 1);

	if (argc != 2) { //make sure to include port
		fprintf(stderr, "usage: ./<server> <port>\n");
		exit(2);
	}
	//initially allocate space for 10 client connections and 10 channels, create default
	num_clients = 10;
	chan_cap = 10;
	dyn_socks = malloc(num_clients * sizeof(struct ctnode));
	dyn_chan = malloc(num_clients * sizeof(struct channel));
	dyn_chan[0].name = "#time";
	dyn_chan[0].topic = "time";
	dyn_chan[0].cap = 10;
	dyn_chan[0].chan_users = malloc(num_clients * sizeof(struct ctnode*));
	num_chan = 1;
	for (int j = 0; j < dyn_chan[0].cap; j++) {
		dyn_chan[0].chan_users[j] = NULL;
	}
	for (int h = 1; h < chan_cap; h++) {
		dyn_chan[h].name = "\0";
	}

	//allocate and set up address parameters
	memset(&servinfo, 0, sizeof servinfo);	servinfo.ai_family = AF_UNSPEC;
	servinfo.ai_socktype = SOCK_STREAM;
	servinfo.ai_flags = AI_PASSIVE;	//get a linked-list of possible addresses that match given parameters	if ((getaddr_ret = getaddrinfo(NULL, argv[1], &servinfo, &result)) != 0) {		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(getaddr_ret));
		return 1;	}	//loop through results until one works	for (pointer = result; pointer != NULL; pointer = pointer->ai_next) {
		if ((sock = socket(pointer->ai_family, pointer->ai_socktype,
			pointer->ai_protocol)) == -1) {
			perror("server: socket");
			continue;
		}
		if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &dumb,
			sizeof(int)) == -1) {
			perror("setsockopt");
			exit(1);
		}
		if (bind(sock, pointer->ai_addr, pointer->ai_addrlen) == -1) {
			close(sock);
			perror("server: bind");
			continue;
		}
			break;	}

	freeaddrinfo(result);
	//make sure an address was found
	if (pointer == NULL) {
		fprintf(stderr, "server: failed to bind\n");
		exit(1);
	}
	//set new socket to passive for listening
	if (listen(sock, BACKLOG) == -1) {
		perror("listen");
		exit(1);	}	//connect to clients	while (1) {
		int caddr_size = sizeof (client_addr);
		shoe = accept(sock, (struct sockaddr*) &client_addr, &caddr_size);
		if (shoe == -1) {
			perror("accept"); // the shoe does not fit
			continue;
		}
		inet_ntop(client_addr.ss_family, get_in_addr((struct sockaddr*) &client_addr), IPchar, sizeof (IPchar));
		printf("server: got connection from %s\n", IPchar);
		dumb = dumb + 1;
		//reallocate memory for more connections if needed
		if (dumb > num_clients) {
			num_clients = num_clients + 10;
			sem_wait(&mutex);
			struct ctnode* temp = malloc(num_clients * sizeof(struct ctnode));
			for (int i = 0; i < (num_clients - 10); i++) {
				dyn_socks[i] = temp[i];
			}
			if (dyn_socks == NULL) {
				perror("err dyn_socks: reallocation failed");
				exit(1);
			}
			free(dyn_socks);
			dyn_socks = temp;
			free(temp);
			sem_post(&mutex);
		}
		sem_wait(&mutex);
		//reallocate user array in default channel if needed
		if (dumb > dyn_chan[0].cap) {
			dyn_chan[0].cap = dyn_chan[0].cap + 10;
			struct ctnode** temp = malloc(num_clients * sizeof(struct ctnode*));
			for (int i = 0; i < (dyn_chan[0].cap - 10); i++) {
				dyn_chan[0].chan_users[i] = temp[i];
			}
			for (int k = dyn_chan[0].cap - 10; k < 10; k++) {
				dyn_chan[0].chan_users[k] = NULL;
			}
			if (dyn_chan[0].chan_users == NULL) {
				perror("err dyn_chan[0]: reallocation failed");
				exit(1);
			}
			free(dyn_chan[0].chan_users);
			dyn_chan[0].chan_users = temp;
			free(temp);
		}
		sem_post(&mutex);
		struct par args;
		args.shoe = shoe;
		int foundempty = -1;
		sem_wait(&mutex);
		//check for slots where users quit
		for (int a = 0; a < dumb; a++) {
			if (dyn_socks[a].free == 1) {
				foundempty = a;
				break;
			}
		}
		sem_post(&mutex);
		if (foundempty != -1) {
			args.dumb = foundempty;
		}
		else {
			args.dumb = dumb;
		}
		args.IPchar = IPchar;
		pthread_t thread;
		pthread_create(&thread, NULL, cthandler, &args);

	}
}