/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/*
 * Copyright (c) 2018 David Jackson
 */

#ifdef __linux__
#define _GNU_SOURCE
#endif /* __linux__ */

#include <sys/socket.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <sys/stat.h>
#include <limits.h>

#define MAX_CONNECTIONS 5
#define BUFSIZE 512
#define CRLF "\r\n"

static void
handle_request(int sockfd);

static void
send_response(int sockfd, const char *path);

void
http_server(int port)
{
	int sockfd;
	int status;
	struct sockaddr_in addr;
	int c; /* client socket */
	socklen_t addrlen;
	int opt_true;

	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd < 0) {
		perror("socket");
		abort();
	}
	opt_true = 1;
	setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt_true, sizeof(int));

	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	status = bind(sockfd, (struct sockaddr *) &addr, sizeof(addr));
	if (status < 0) {
		perror("bind");
		abort();
	}

	status = listen(sockfd, MAX_CONNECTIONS);
	if (status < 0) {
		perror("listen");
		abort();
	}

	addrlen = sizeof(addr);
	while (1) {
		c = accept(sockfd, (struct sockaddr *) &addr, &addrlen);
		if (c < 0) {
			perror("accept");
			abort();
		}
		handle_request(c);
		close(c);
	}

	close(sockfd);
}

static char
*extract_path(const char *req, ssize_t size, char *path, size_t path_size)
{
	char method[10];
	int i;
	char ch;
	int path_len;

	for (i = 0; i < 10 - 1; i++) {
		ch = req[i];
		if (ch == ' ') {
			break;
		}
		method[i] = ch;
	}
	method[i] = '\0';
	i++;

	if (strcmp(method, "GET") != 0) {
		fprintf(stderr, "Only GET requests are supported (%s)\n", method);
		abort();
	}
 
	path_len = 0;
	for (; i < size; i++) {
		ch = req[i];
		if (path_len >= path_size) {
			fprintf(stderr, "Path length too long\n");
			abort();
		}
		if (ch == ' ') {
			break;
		}
		path[path_len++] = ch;
	}
	path[path_len] = '\0';
}

static void
handle_request(int sockfd)
{
	char buf[BUFSIZE];
	ssize_t size;
	char path[100];

	size = recv(sockfd, buf, BUFSIZE, 0);
	extract_path(buf, size, path, 100);
	if (strstr(path, "favicon.ico") != NULL) {
		/* Ignore favicon requests */
		return;
	}
	send_response(sockfd, path);
}

static void
send_response(int sockfd, const char *path)
{
	char file_name[] = "index.html";
	off_t file_size;
	FILE *fp;
	struct stat statbuf;
	time_t now;
	struct tm now_tm;
	char timebuf[100];
	char buf_fmt[] = "HTTP/1.1 200 OK" CRLF
		"Date: %s" CRLF
		"Server: cyhttp" CRLF
		"Content-Length: %d" CRLF
		"Content-Type: text/html; charset=utf-8" CRLF CRLF
		"%s" CRLF CRLF;
	char *buf;
	char *content;
	int content_len;
	char topdir[PATH_MAX - 1];

	getcwd(topdir, PATH_MAX - 1);
	chdir(topdir);
	if (!(strlen(path) == 1 && path[0] == '/')) {
		if (chdir(path + 1) < 0) { /* +1 to skip initial "/" */
			fprintf(stderr, "path: %s\n", path);
			perror("chdir");
			abort();
		}
	}

	if (stat(file_name, &statbuf) < 0) {
		perror("stat");
		abort();
	}
	file_size = statbuf.st_size;

	content = malloc(file_size + 1);
	fp = fopen(file_name, "r");
	fread(content, 1, file_size, fp);
	content[file_size] = '\0';
	content_len = (int)file_size + 1;
	if (content_len < 0) {
		fprintf(stderr, "Negative content length\n");
		abort();
	}

	now = time(NULL);
	localtime_r(&now, &now_tm);
	strftime(timebuf, 100, "%a, %d %b %Y %H:%M:%S %Z", &now_tm);
	asprintf(&buf, buf_fmt, timebuf, content_len, content);
	send(sockfd, buf, strlen(buf), 0);

	free(buf);
	free(content);

	chdir(topdir);
}