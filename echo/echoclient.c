#include "csapp.h"

int main(int argc, char **argv) //void 가 아닌 int 에서는 인자를 받을 수 있음.
{
    int clientfd;
    char *host, *port, buf[MAXLINE];
    rio_t rio;

    if (argc != 3) //인자가 3개가 아니면 오류나게 (클라이언트 실행 인자, 호스트 주소 인자, 포트 번호 인자 ) 
	{
        fprintf(stderr, "usage: %s <host> <port>\n", argv[0]);
        exit(0);
    }
    host = argv[1]; 
    port = argv[2];

    clientfd = open_clientfd(host, port);
    Rio_readinitb(&rio, clientfd);

    while (fgets(buf, MAXLINE, stdin) != NULL) 
	{
        Rio_writen(clientfd, buf, strlen(buf));
        Rio_readlineb(&rio, buf, MAXLINE);
        fputs(buf, stdout);
    }
    Close(clientfd);
    exit(0);
}