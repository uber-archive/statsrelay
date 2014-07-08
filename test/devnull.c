#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <string.h>
#include <stdio.h>

void devnull(int fd) {
	char buf[65536];
	size_t len;

	while(1) {
		len = read(fd, buf, 65536);
		if(len < 1) {
			fprintf(stderr, "Child exiting\n");
			return;
		}
	}
}

int main(int argc, char *argv[]) {
	int sd;

	if(argc < 2) {
		fprintf(stderr, "Usage: %s <port>\n", argv[0]);
		return 1;
	}

	struct sockaddr_storage their_addr;
	socklen_t addr_size;
	struct addrinfo hints, *res;
	int sockfd, acceptfd;
	pid_t pid;

	// first, load up address structs with getaddrinfo():

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;  // use IPv4 or IPv6, whichever
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;     // fill in my IP for me

	getaddrinfo(NULL, argv[1], &hints, &res);

	// make a socket:

	sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);

	// bind it to the port we passed in to getaddrinfo():

	bind(sockfd, res->ai_addr, res->ai_addrlen);

	listen(sockfd, 1);

	while(1) {
		acceptfd = accept(sockfd, (struct sockaddr *)&their_addr, &addr_size);
		pid = fork();
		if(pid == 0) {
			// child
			devnull(acceptfd);
			return 0;
		}else{
			fprintf(stderr, "Forked pid %i\n", pid);
		}
	}

	return 0;
}
