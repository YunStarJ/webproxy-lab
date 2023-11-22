#include <stdio.h>
#include <signal.h>

#include "csapp.h"
#include "cache.h"

void *thread(void *vargp);
void doit(int clientfd);
void read_requesthdrs(rio_t *rp, void *buf, int serverfd, char *hostname, char *port);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);
void parse_uri(char *uri, char *hostname, char *port, char *path);

// 테스트 환경에 따른 도메인&포트 지정을 위한 상수 (0 할당 시 도메인&포트가 고정되어 외부에서 접속 가능)
static const int is_local_test = 1; 
static const char *user_agent_hdr =
"User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
"Firefox/10.0.3\r\n";

int main(int argc, char **argv)
{
	// int listenfd, connfd;: 두 개의 파일 디스크립터입니다. 
	// listenfd는 서버가 클라이언트의 연결 요청을 듣기 위해 사용하는 "listening" 소켓, connfd는 연결된 클라이언트와 통신을 위한 "connected" 소켓입니다.
	int listenfd, *clientfd;
	// char hostname[MAXLINE], port[MAXLINE];: 클라이언트의 호스트 이름과 포트 번호를 저장하기 위한 문자 배열입니다.		
	char hostname[MAXLINE], port[MAXLINE];
	// socklen_t clientlen;: clientaddr 구조체의 크기를 저장하는 데 사용됩니다.
	socklen_t clientlen;
	// struct sockaddr_storage clientaddr;: 클라이언트의 소켓 주소를 저장하는 구조체입니다. sockaddr_storage는 다양한 소켓 주소 구조체를 수용할 수 있도록 충분히 크게 정의된 구조체입니다.
	struct sockaddr_storage clientaddr;

	pthread_t tid;
	// signal(SIGPIPE, SIG_IGN): SIGPIPE 시그널을 무시하도록 설정합니다. 
	// 이는 클라이언트가 연결을 끊었을 때 발생하는 시그널입니다.
	signal(SIGPIPE, SIG_IGN);

	// rootp와 lastp는 캐시 구현의 일부로 사용됩니다. 
	// rootp는 캐시된 객체 목록의 시작점을 나타내고, lastp는 목록의 끝점을 나타낼 수 있습니다. 
	// 이렇게 하면 캐시에 새로운 객체를 추가하거나 기존 객체를 조회할 때 효율적으로 작업을 수행할 수 있습니다.
	// cache.h 참고
	rootp = (web_object_t *)calloc(1, sizeof(web_object_t));
	lastp = (web_object_t *)calloc(1, sizeof(web_object_t));

	if (argc != 2)
	{
		fprintf(stderr, "usage: %s <port>\n", argv[0]);
		exit(1);
	}


	// Open_listenfd(argv[1]): 사용자가 제공한 포트 번호(argv[1])로 소켓을 열고, 이 소켓을 클라이언트의 연결 요청을 듣기 위해 바인딩합니다.
	listenfd = Open_listenfd(argv[1]);   

	while (1) // 서버는 무한 루프를 돌면서 계속해서 연결 요청을 처리합니다. 
	{
		// clientlen = sizeof(clientaddr);: 클라이언트 주소 구조체의 크기를 clientlen에 저장합니다.
		clientlen = sizeof(clientaddr);
		clientfd = Malloc(sizeof(int));
		// clientfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);: 클라이언트의 연결 요청을 받아들이고, 연결된 소켓을 connfd에 저장합니다.
		*clientfd = Accept(listenfd, (SA *)&clientaddr, &clientlen); 

		// Getnameinfo(...): 연결된 클라이언트의 정보를 얻어와 hostname과 port에 저장합니다.
		Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
		printf("Accepted connection from (%s, %s)\n", hostname, port);

		// Pthread_create를 호출하여 각 클라이언트 연결에 대해 thread 함수를 실행하는 새로운 스레드를 생성합니다.
		Pthread_create(&tid, NULL, thread, clientfd); 
	}
}

void *thread(void *vargp)
{
	int clientfd = *((int *)vargp);
	Pthread_detach(pthread_self());
	Free(vargp);
	doit(clientfd);
	Close(clientfd);
	return NULL;
}

void doit(int clientfd)
{
	int serverfd, content_length;
	char request_buf[MAXLINE], response_buf[MAXLINE];
	char method[MAXLINE], uri[MAXLINE], path[MAXLINE], hostname[MAXLINE], port[MAXLINE];
	char *response_ptr, filename[MAXLINE], cgiargs[MAXLINE];
	rio_t request_rio, response_rio;

	Rio_readinitb(&request_rio, clientfd);
	Rio_readlineb(&request_rio, request_buf, MAXLINE);
	printf("Request headers:\n %s\n", request_buf);

	// 요청 라인 parsing을 통해 `method, uri, hostname, port, path` 찾기
	sscanf(request_buf, "%s %s", method, uri);
	parse_uri(uri, hostname, port, path);

	// Server에 전송하기 위해 요청 라인의 형식 변경: `method uri version` -> `method path HTTP/1.0`
	sprintf(request_buf, "%s %s %s\r\n", method, path, "HTTP/1.0");

	// 지원하지 않는 method인 경우 예외 처리
	if (strcasecmp(method, "GET") && strcasecmp(method, "HEAD"))
	{
		clienterror(clientfd, method, "501", "Not implemented", "Tiny does not implement this method");
		return;
	}

	// 현재 요청이 캐싱된 요청(path)인지 확인
	web_object_t *cached_object = find_cache(path);
	if (cached_object) // 캐싱 되어있다면
	{
		send_cache(cached_object, clientfd); // 캐싱된 객체를 Client에 전송
		read_cache(cached_object);           // 사용한 웹 객체의 순서를 맨 앞으로 갱신
		return;                              // Server로 요청을 보내지 않고 통신 종료
	}

	// Request Line 전송 [ Proxy -> Server ] 
	// Server 소켓 생성
	serverfd = is_local_test ? Open_clientfd(hostname, port) : Open_clientfd("52.79.150.85", port);
	if (serverfd < 0)
	{
		clienterror(serverfd, method, "502", "Bad Gateway", "📍 Failed to establish connection with the end server");
		return;
	}
	Rio_writen(serverfd, request_buf, strlen(request_buf));

	// Request Header 읽기 & 전송 [ Client -> Proxy -> Server ] 
	read_requesthdrs(&request_rio, request_buf, serverfd, hostname, port);

	// Response Header 읽기 & 전송 [ Server -> Proxy -> Client ] 
	Rio_readinitb(&response_rio, serverfd);
	while (strcmp(response_buf, "\r\n"))
	{
		Rio_readlineb(&response_rio, response_buf, MAXLINE);
		if (strstr(response_buf, "Content-length")) // Response Body 수신에 사용하기 위해 Content-length 저장
			content_length = atoi(strchr(response_buf, ':') + 1); // atoi 함수는 char형 정수를 int형으로 캐스팅 해준다.
		Rio_writen(clientfd, response_buf, strlen(response_buf));
	}

	// Response Body 읽기 & 전송 [ Server -> Proxy -> Client ] 
	response_ptr = malloc(content_length);
	Rio_readnb(&response_rio, response_ptr, content_length);
	Rio_writen(clientfd, response_ptr, content_length); // Client에 Response Body 전송

	if (content_length <= MAX_OBJECT_SIZE) // 캐싱 가능한 크기인 경우
	{
		// web_object 구조체 생성
		web_object_t *web_object = (web_object_t *)calloc(1, sizeof(web_object_t));
		web_object->response_ptr = response_ptr;
		web_object->content_length = content_length;
		strcpy(web_object->path, path);
		write_cache(web_object); // 캐시 연결 리스트에 추가
	}
	else
		free(response_ptr); // 캐싱하지 않은 경우만 메모리 반환

	Close(serverfd);
}

// 클라이언트에 에러를 전송하는 함수
// cause: 오류 원인, errnum: 오류 번호, shortmsg: 짧은 오류 메시지, longmsg: 긴 오류 메시지
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg)
{
	char buf[MAXLINE], body[MAXBUF];

	// 에러 Bdoy 생성
	sprintf(body, "<html><title>Tiny Error</title>");
	sprintf(body, "%s<body bgcolor="
			"ffffff"
			">\r\n",
			body);
	sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
	sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
	sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

	// 에러 Header 생성 & 전송
	sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
	Rio_writen(fd, buf, strlen(buf));
	sprintf(buf, "Content-type: text/html\r\n");
	Rio_writen(fd, buf, strlen(buf));
	sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
	Rio_writen(fd, buf, strlen(buf));

	// 에러 Body 전송
	Rio_writen(fd, body, strlen(body));
}

// uri를 `hostname`, `port`, `path`로 파싱하는 함수
// uri 형태: `http://hostname:port/path` 혹은 `http://hostname/path` (port는 optional)
void parse_uri(char *uri, char *hostname, char *port, char *path)
{
	// host_name의 시작 위치 포인터: '//'가 있으면 //뒤(ptr+2)부터, 없으면 uri 처음부터
	char *hostname_ptr = strstr(uri, "//") ? strstr(uri, "//") + 2 : uri;
	char *port_ptr = strchr(hostname_ptr, ':'); // port 시작 위치 (없으면 NULL)
	char *path_ptr = strchr(hostname_ptr, '/'); // path 시작 위치 (없으면 NULL)
	strcpy(path, path_ptr);

	if (port_ptr) // port 있는 경우
	{
		strncpy(port, port_ptr + 1, path_ptr - port_ptr - 1); 
		strncpy(hostname, hostname_ptr, port_ptr - hostname_ptr);
	}
	else // port 없는 경우
	{
		if (is_local_test)
			strcpy(port, "80"); // port의 기본 값인 80으로 설정
		else
			strcpy(port, "8000");
		strncpy(hostname, hostname_ptr, path_ptr - hostname_ptr);
	}
}

// Request Header를 읽고 Server에 전송하는 함수
// 필수 헤더가 없는 경우에는 필수 헤더를 추가로 전송
void read_requesthdrs(rio_t *request_rio, void *request_buf, int serverfd, char *hostname, char *port)
{
	int is_host_exist;
	int is_connection_exist;
	int is_proxy_connection_exist;
	int is_user_agent_exist;

	Rio_readlineb(request_rio, request_buf, MAXLINE); // 첫번째 줄 읽기
	while (strcmp(request_buf, "\r\n"))
	{
		if (strstr(request_buf, "Proxy-Connection") != NULL)
		{
			sprintf(request_buf, "Proxy-Connection: close\r\n");
			is_proxy_connection_exist = 1;
		}
		else if (strstr(request_buf, "Connection") != NULL)
		{
			sprintf(request_buf, "Connection: close\r\n");
			is_connection_exist = 1;
		}
		else if (strstr(request_buf, "User-Agent") != NULL)
		{
			sprintf(request_buf, user_agent_hdr);
			is_user_agent_exist = 1;
		}
		else if (strstr(request_buf, "Host") != NULL)
		{
			is_host_exist = 1;
		}

		Rio_writen(serverfd, request_buf, strlen(request_buf)); // Server에 전송
		Rio_readlineb(request_rio, request_buf, MAXLINE);       // 다음 줄 읽기
	}

	// 필수 헤더 미포함 시 추가로 전송
	if (!is_proxy_connection_exist)
	{
		sprintf(request_buf, "Proxy-Connection: close\r\n");
		Rio_writen(serverfd, request_buf, strlen(request_buf));
	}
	if (!is_connection_exist)
	{
		sprintf(request_buf, "Connection: close\r\n");
		Rio_writen(serverfd, request_buf, strlen(request_buf));
	}
	if (!is_host_exist)
	{
		if (!is_local_test)
			hostname = "172.29.156.80";
		sprintf(request_buf, "Host: %s:%s\r\n", hostname, port);
		Rio_writen(serverfd, request_buf, strlen(request_buf));
	}
	if (!is_user_agent_exist)
	{
		sprintf(request_buf, user_agent_hdr);
		Rio_writen(serverfd, request_buf, strlen(request_buf));
	}

	sprintf(request_buf, "\r\n"); // 종료문
	Rio_writen(serverfd, request_buf, strlen(request_buf));
	return;
}