
#include "csapp.h"
FILE *log_file;

struct thread_args
{
    int fd;
    int ID;
    char* haddrp;
    char* hp;
    struct sockaddr_in sock;
};

sem_t mutex;
sem_t log_mutex;

int parse_uri(char *uri, char *target_addr, char *path, int  *port);
void format_log_entry(char *logstring, struct sockaddr_in *sockaddr, char *uri, int size);
void client_error(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);
void *doit(void *);
ssize_t Rio_readlineb_w(rio_t *rp, void *usrbuf, size_t maxlen);
void Rio_writen_w(int fd, void *usrbuf, size_t n);
ssize_t Rio_readnb_w(rio_t *rp, void *usrbuf, size_t n);

/*Thread safe version of open_clientfd*/
int open_clientfd_ts(char *hostname, int port) {
    int clientfd;
    struct hostent *hp;
    struct hostent *priv_hp;
    struct sockaddr_in serveraddr;
    int error = 0;
    if ((clientfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        return -1; /* Check errno for cause of error */
    P(&mutex);
    /* Fill in the server.s IP address and port */
    if ((hp = gethostbyname(hostname)) == NULL) {
        printf("ERROR: open_clientfd, hostname not found: %s!\n", hostname);
        error = -2; /* Check h_errno for cause of error */
        }
    memcpy(&priv_hp, &hp, sizeof(hp));
    V(&mutex);
    if(error != 0)
        return error;
    bzero((char *) &serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    bcopy((char *)priv_hp->h_addr_list[0],
          (char *)&serveraddr.sin_addr.s_addr, priv_hp->h_length);
    serveraddr.sin_port = htons(port);
    /* Establish a connection with the server */
    if (connect(clientfd, (SA *) &serveraddr, sizeof(serveraddr)) < 0) {
        printf("ERROR: open_clientfd, could not connect to server: %s!\n", hostname);
        return -1;
        }
    return clientfd;
    }

int main(int argc, char **argv) 
{
    int listenfd, connfd, port, ID;
    socklen_t clientlen;
    struct sockaddr_in clientaddr;
    struct hostent *hp;
    char *haddrp;
    pthread_t tid;

    if (argc != 2) {
	fprintf(stderr, "usage: %s <port>\n", argv[0]);
	exit(0);
    }
    port = atoi(argv[1]);
    Signal(SIGPIPE, SIG_IGN);

    sem_init(&mutex, 0, 1);
    sem_init(&log_mutex, 0, 1);

    log_file = fopen("./proxy.log", "a");

    listenfd = Open_listenfd(port);
    ID = 0;
    while (1) {
	clientlen = sizeof(clientaddr);
	struct thread_args *argsp = (struct thread_args *) malloc(sizeof(struct thread_args));	
	connfd = Accept(listenfd, (SA *)&(argsp->sock), &clientlen);
	argsp->fd = connfd;
	argsp->ID = ID;
	
	/* Determine the domain name and IP address of the client */
	hp = Gethostbyaddr((const char *)&(argsp->sock).sin_addr.s_addr, 
			   sizeof((argsp->sock).sin_addr.s_addr), AF_INET);
	haddrp = inet_ntoa((argsp->sock).sin_addr);
	argsp->hp = hp->h_name;
	argsp->haddrp = haddrp;
	Pthread_create(&tid, NULL, doit, (void *)argsp);
	ID++;
    }
    exit(0);
}

void *doit(void *vargs)
{

    Pthread_detach(pthread_self());

    struct thread_args *args = (struct thread_args *)vargs;
    int fd = args->fd;
    int ID = args->ID;
    char* hp = args->hp;
    char* haddrp = args->haddrp;

    struct sockaddr_in sock;
    memcpy(&sock, &(args->sock), sizeof(struct sockaddr_in));
    free(args);
    
    char buf[MAXLINE];
    rio_t rio;
    Rio_readinitb(&rio, fd);
    Rio_readlineb_w(&rio, buf, MAXLINE);
  
    char method[MAXLINE], uri[MAXLINE], version[MAXLINE];

    sscanf(buf, "%s %s %s", method, uri, version);
    if (strcmp(method, "GET"))
    {
        client_error(fd, method, "501", "Not Implemented", "Not Implemented");
        close(fd);
        return NULL;
    }

    char hostname[MAXLINE], pathname[MAXLINE];
    int port;
    if (parse_uri(uri, hostname, pathname, &port))
    {
        client_error(fd, method, "400", "Bad Request", "parse_uri error");
        close(fd);
        return NULL;
    }

    /*read rest header*/
    char line[MAXLINE];
    sprintf(line, "%s %s %s\r\n", method, pathname, version);


    /*client part*/
    rio_t rio_client;
    int client_fd = open_clientfd_ts(hostname, port);

    Rio_readinitb(&rio_client, client_fd);

    Rio_writen_w(client_fd, line, strlen(line));


    while (Rio_readlineb_w(&rio, line, MAXLINE) > 2)
    {
        if (strstr(line, "Proxy-Connection"))
            strcpy(line, "Proxy-Connection: close\r\n");
        else if (strstr(line, "Connection"))
            strcpy(line, "Connection: close\r\n");
        
        Rio_writen_w(client_fd, line, strlen(line));

    }
    char request[MAXLINE];
    sprintf(request, "%s %s %s\nHost: %s", method, uri, version, hostname);
    
    printf("Thread %d: Received request from %s (%s)\n",ID, hp, haddrp);
    printf("%s\n", request);
    printf("\n**End of Request**\n");
    Rio_writen_w(client_fd, "\r\n", 2);

    printf("Thread %d: Forwarding request to end server:\n", ID);
    printf("Get: %s   %s\n", pathname, version);
    printf("Host: %s\n", hostname);
    printf("\n**End of Request**\n");

    /* read content from server*/

    int len = 0;
    int n = 0;

    while ((n = Rio_readnb_w(&rio_client, buf, MAXLINE)) > 0){
	printf("Thread %d:server received %d bytes\n", ID, n);
	len += n;
	Rio_writen_w(fd, buf, n);
	bzero(buf, MAXLINE);
	}

    P(&log_mutex);
    format_log_entry(buf, &sock, uri, len);
    fprintf(log_file, "%s\n", buf);
    fflush(log_file);
    V(&log_mutex);

    close(client_fd);    
    close(fd);   
    return NULL;
}

void client_error(int fd, char *cause, char *errnum, 
                 char *shortmsg, char *longmsg) 
{
    char buf[MAXLINE], body[MAXBUF];

    /* Build the HTTP response body */
    sprintf(body, "%s: %s\r\n", errnum, shortmsg);
    sprintf(body, "%s%s: %s", body, longmsg, cause);

    /* Print the HTTP response */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen_w(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    Rio_writen_w(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    Rio_writen_w(fd, buf, strlen(buf));
    Rio_writen_w(fd, body, strlen(body));
}

/*
 * parse_uri - URI parser
 * 
 * Given a URI from an HTTP proxy GET request (i.e., a URL), extract
 * the host name, path name, and port.  The memory for hostname and
 * pathname must already be allocated and should be at least MAXLINE
 * bytes. Return -1 if there are any problems.
 */
int parse_uri(char *uri, char *hostname, char *pathname, int *port)
{
    char *hostbegin;
    char *hostend;
    char *pathbegin;
    int len;

    if (strncasecmp(uri, "http://", 7) != 0) {
	hostname[0] = '\0';
	return -1;
    }
       
    /* Extract the host name */
    hostbegin = uri + 7;
    hostend = strpbrk(hostbegin, " :/\r\n\0");
    if (hostend == 0)
    {
        fprintf(stderr, "error: hostend = 0\n");
        return -1;
    }
    len = hostend - hostbegin;
    strncpy(hostname, hostbegin, len);
    hostname[len] = '\0';
    
    /* Extract the port number */
    *port = 80; /* default */
    if (*hostend == ':')   
	*port = atoi(hostend + 1);
    
    /* Extract the path */
    pathbegin = strchr(hostbegin, '/');
    if (pathbegin == NULL) {
	pathname[0] = '\0';
    }
    else {
	pathbegin++;
	strcpy(pathname, pathbegin);
    }

    char tmp[MAXLINE];
    sprintf(tmp, "/%s", pathname);
    strcpy(pathname, tmp);
    return 0;
}

/*
 * format_log_entry - Create a formatted log entry in logstring. 
 * 
 * The inputs are the socket address of the requesting client
 * (sockaddr), the URI from the request (uri), and the size in bytes
 * of the response from the server (size).
 */
void format_log_entry(char *logstring, struct sockaddr_in *sockaddr, 
		      char *uri, int size)
{
    time_t now;
    char time_str[MAXLINE];
    unsigned long host;
    unsigned char a, b, c, d;

    /* Get a formatted time string */
    now = time(NULL);
    strftime(time_str, MAXLINE, "%a %d %b %Y %H:%M:%S %Z", localtime(&now));

    /* 
     * Convert the IP address in network byte order to dotted decimal
     * form. Note that we could have used inet_ntoa, but chose not to
     * because inet_ntoa is a Class 3 thread unsafe function that
     * returns a pointer to a static variable (Ch 13, CS:APP).
     */
    host = ntohl(sockaddr->sin_addr.s_addr);
    a = host >> 24;
    b = (host >> 16) & 0xff;
    c = (host >> 8) & 0xff;
    d = host & 0xff;


    /* Return the formatted log entry string */
    sprintf(logstring, "%s: %d.%d.%d.%d %s %d", time_str, a, b, c, d, uri, size);
}

void Rio_writen_w(int fd, void *usrbuf, size_t n)
{
  if (rio_writen(fd, usrbuf, n) != n)
    printf("Warning: Rio_writen error");
}

ssize_t Rio_readlineb_w(rio_t *rp, void *usrbuf, size_t maxlen)
{
  ssize_t rc;

  if ((rc = rio_readlineb(rp, usrbuf, maxlen)) < 0)
    printf("Warning: Rio_readlineb error");
  return rc;
}

ssize_t Rio_readnb_w(rio_t *rp, void *usrbuf, size_t n)
{
    ssize_t rc;

    if ((rc = rio_readnb(rp, usrbuf, n)) < 0)
    printf("Rio_readnb error");
    return rc;
}
