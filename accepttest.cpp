#include <sys/socket.h>
#include <sys/un.h>
#include <sys/types.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>
#include <string>
#include <stdexcept>

using namespace std;

#define NTHREADS 10

static int server;
static pthread_t threads[NTHREADS];
static const char response[] =
	"HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\nhello world\n";


void
makeNonBlock(int fd) {
	int flags = fcntl(fd, F_GETFL);
	fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int
createUnixServer(const string &filename, unsigned int backlogSize = 0, bool autoDelete = true) {
	struct sockaddr_un addr;
	int fd, ret;
	
	if (filename.size() > sizeof(addr.sun_path) - 1) {
		string message = "Cannot create Unix socket '";
		message.append(filename);
		message.append("': filename is too long.");
		throw runtime_error(message);
	}
	
	fd = socket(PF_LOCAL, SOCK_STREAM, 0);
	if (fd == -1) {
		throw runtime_error("Cannot create a Unix socket file descriptor");
	}
	
	addr.sun_family = AF_LOCAL;
	strncpy(addr.sun_path, filename.c_str(), filename.size());
	addr.sun_path[filename.size()] = '\0';
	
	if (autoDelete) {
		do {
			ret = unlink(filename.c_str());
		} while (ret == -1 && errno == EINTR);
	}
	
	try {
		ret = bind(fd, (const struct sockaddr *) &addr, sizeof(addr));
	} catch (...) {
		do {
			ret = close(fd);
		} while (ret == -1 && errno == EINTR);
		throw;
	}
	if (ret == -1) {
		string message = "Cannot bind Unix socket '";
		message.append(filename);
		message.append("'");
		do {
			ret = close(fd);
		} while (ret == -1 && errno == EINTR);
		throw runtime_error(message);
	}
	
	if (backlogSize == 0) {
		backlogSize = 1024;
	}
	try {
		ret = listen(fd, backlogSize);
	} catch (...) {
		do {
			ret = close(fd);
		} while (ret == -1 && errno == EINTR);
		throw;
	}
	if (ret == -1) {
		string message = "Cannot listen on Unix socket '";
		message.append(filename);
		message.append("'");
		do {
			ret = close(fd);
		} while (ret == -1 && errno == EINTR);
		throw runtime_error(message);
	}
	
	return fd;
}

int
createTcpServer(const char *address, unsigned short port, unsigned int backlogSize = 0) {
	struct sockaddr_in addr;
	int fd, ret, optval;
	
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	ret = inet_pton(AF_INET, address, &addr.sin_addr.s_addr);
	if (ret < 0) {
		string message = "Cannot parse the IP address '";
		message.append(address);
		message.append("'");
		throw runtime_error(message);
	} else if (ret == 0) {
		string message = "Cannot parse the IP address '";
		message.append(address);
		message.append("'");
		throw runtime_error(message);
	}
	addr.sin_port = htons(port);
	
	fd = socket(PF_INET, SOCK_STREAM, 0);
	if (fd == -1) {
		throw runtime_error("Cannot create a TCP socket file descriptor");
	}
	
	try {
		ret = bind(fd, (const struct sockaddr *) &addr, sizeof(addr));
	} catch (...) {
		do {
			ret = close(fd);
		} while (ret == -1 && errno == EINTR);
		throw;
	}
	if (ret == -1) {
		do {
			ret = close(fd);
		} while (ret == -1 && errno == EINTR);
		throw runtime_error("Cannot bind a TCP socket");
	}
	
	optval = 1;
	try {
		if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR,
			&optval, sizeof(optval)) == -1) {
				printf("so_reuseaddr failed: %s\n", strerror(errno));
			}
	} catch (...) {
		do {
			ret = close(fd);
		} while (ret == -1 && errno == EINTR);
		throw;
	}
	// Ignore SO_REUSEPORT error, it's not fatal.
	
	if (backlogSize == 0) {
		backlogSize = 1024;
	}
	try {
		ret = listen(fd, backlogSize);
	} catch (...) {
		do {
			ret = close(fd);
		} while (ret == -1 && errno == EINTR);
		throw;
	}
	if (ret == -1) {
		do {
			ret = close(fd);
		} while (ret == -1 && errno == EINTR);
		throw runtime_error("Cannot listen on TCP socket");
	}
	
	return fd;
}

static void
processClient(int fd) {
	char buf[1024 * 4];
	bool eof = false;
	while (!eof) {
		eof = read(fd, buf, sizeof(buf)) <= 0;
	}
	write(fd, response, sizeof(response) - 1);
	close(fd);
}

void *
nonBlockingAccept(void *arg) {
	makeNonBlock(server);
	while (true) {
		fd_set fds;
		FD_ZERO(&fds);
		FD_SET(server, &fds);
		
		if (select(server + 1, &fds, NULL, NULL, NULL) > 0) {
			struct sockaddr_in addr;
			socklen_t addrlen = sizeof(addr);
			int fd = accept(server, (struct sockaddr *) &addr, &addrlen);
			if (fd != -1) {
				processClient(fd);
			}
		}
	}
	return NULL;
}

void *
nonBlockingAcceptWithFallback(void *arg) {
	makeNonBlock(server);
	while (true) {
		fd_set fds;
		FD_ZERO(&fds);
		FD_SET(server, &fds);
		
		if (select(server + 1, &fds, NULL, NULL, NULL) > 0) {
			struct sockaddr_in addr;
			socklen_t addrlen = sizeof(addr);
			int fd = accept(server, (struct sockaddr *) &addr, &addrlen);
			if (fd != -1) {
				processClient(fd);
			} else {
				usleep(rand() % 15000);
			}
		}
	}
	return NULL;
}

void *
blockingAccept(void *arg) {
	while (true) {
		struct sockaddr_in addr;
		socklen_t addrlen = sizeof(addr);
		int fd = accept(server, (struct sockaddr *) &addr, &addrlen);
		processClient(fd);
	}
	return NULL;
}

int
main(int argc, char *argv[]) {
	int i;
	void *(*threadMain)(void *) = blockingAccept;
	
	signal(SIGPIPE, SIG_IGN);
	server = createUnixServer("server.sock");
	
	if (argc > 1) {
		if (strcmp(argv[0], "1") == 0) {
			threadMain = blockingAccept;
		} else if (strcmp(argv[0], "2") == 0) {
			threadMain = nonBlockingAccept;
		} else if (strcmp(argv[0], "3") == 0) {
			threadMain = nonBlockingAcceptWithFallback;
		}
	}
	
	for (i = 0; i < NTHREADS; i++) {
		pthread_create(&threads[i], NULL, threadMain, NULL);
	}
	for (i = 0; i < NTHREADS; i++) {
		pthread_join(threads[i], NULL);
	}
	return 0;
}
