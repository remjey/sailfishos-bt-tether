/*
 * Copyright (c) 2018 Jérémy Farnaud
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <poll.h>
#include <string.h>
#include <time.h>

#define MAX_REQ_SIZE 2048
#define MAX_SOCKETS 256
#define REQ_TIMEOUT 10

struct ret_info_t {
  struct sockaddr_in addr;
  time_t timeout;
};

struct sockaddr_in relay_addr;
struct sockaddr_in server_addr;

struct pollfd fds[MAX_SOCKETS];
struct ret_info_t ret_info[MAX_SOCKETS];
int nfds = 0;

void usage(char *name) {
  printf("Usage: %s [-f] <ip address>\n", name);
  exit(1);
}

int fds_add(int fd, short events) {
  if (nfds == MAX_SOCKETS) return -1;

  fds[nfds].fd = fd;
  fds[nfds].events = events;
  fds[nfds].revents = 0;
  return nfds++;
}

int fds_add_with_ri(int fd, short events, struct sockaddr_in addr, time_t timeout) {
  int i = fds_add(fd, events);
  if (i != -1) {
    ret_info[i].addr = addr;
    ret_info[i].timeout = timeout;
  }
  return i;
}

void fds_remove(int i) {
  memmove(&fds[i], &fds[i + 1], sizeof(struct pollfd) * (nfds - i - 1));
  memmove(&ret_info[i], &ret_info[i + 1], sizeof(struct ret_info_t) * (nfds - i - 1));
  --nfds;
}

int main(int argc, char **argv) {
  int ip_arg_n = 1;
  int do_fork = 0;

  if (argc > ip_arg_n && strcmp("-f", argv[1]) == 0) {
    do_fork = 1;
    ++ip_arg_n;
  }

  relay_addr.sin_family = AF_INET;
  relay_addr.sin_port = htons(53);

  if (argc <= ip_arg_n || !inet_pton(AF_INET, argv[ip_arg_n], &relay_addr.sin_addr)) {
    usage(argv[0]);
  }

  int relay_fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (relay_fd == -1) {
    perror("relay socket");
    exit(10);
  }
  if (bind(relay_fd, (struct sockaddr*)&relay_addr, sizeof(relay_addr))) {
    perror("relay bind");
    exit(11);
  }

  if (do_fork) {
    pid_t pid = fork();
    if (pid != 0) {
      printf("%d\n", pid);
      exit(0);
    }

    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
  }

  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(53);
  server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  fds_add(relay_fd, POLLIN);

  for (;;) {
    if (poll(fds, nfds, REQ_TIMEOUT * 1000) == -1) {
      if (!do_fork) perror("poll");
      exit(201);
    }

    time_t now = time(NULL);

    for (int i = 1; i < nfds;) {
      if (fds[i].revents == 0 && ret_info[i].timeout > now) {
        // We are still waiting for an answer and timeout is still later
        ++i;
        continue;
      }

      if (fds[i].revents == POLLIN) {
        char buf[MAX_REQ_SIZE];
        ssize_t len = recv(fds[i].fd, buf, sizeof(buf), MSG_WAITALL);
        if (len > 0) {
          ssize_t sent_len = sendto(
              relay_fd, buf, len, 0,
              (struct sockaddr*)&ret_info[i].addr, sizeof(struct sockaddr_in));
          if (sent_len < 0) {
            if (!do_fork) perror("relay sendto");
          }
        }
      }

      // Either we had an answer or the answer has timed out, remove fd from list
      close(fds[i].fd);
      fds_remove(i);
    }

    // Check on relay_fd if we received a request
    if ((fds[0].revents & POLLERR) != 0) exit(200);
    if ((fds[0].revents & POLLIN) != 0) {
      struct sockaddr_in from;
      socklen_t from_len = sizeof(from);
      char buf[MAX_REQ_SIZE];
      ssize_t len = recvfrom(
          relay_fd, buf, sizeof(buf), MSG_WAITALL, (struct sockaddr*)&from, &from_len);

      // We’ll ignore empty packets and packets received when the fds is full
      if (len > 0 && nfds < MAX_SOCKETS) {
        // Forward request to server
        int server_fd = socket(AF_INET, SOCK_DGRAM, 0);
        connect(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr));
        ssize_t send_len = send(server_fd, buf, len, 0);
        if (send_len < 0) {
          if (!do_fork) perror("server sendto");
          close(server_fd);
        } else {
          fds_add_with_ri(server_fd, POLLIN, from, now + REQ_TIMEOUT);
        }
      } else if (len < 0) {
        if (!do_fork) perror("recvfrom");
      }
    }
  }

  return 0;
}

