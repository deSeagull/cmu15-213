#include "csapp.h"
#include <stdbool.h>
#include <errno.h>
#include <stdint.h>
/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define SBUFSIZE 100
#define NTHREADS 100

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

typedef struct {
    /* 修改：保存目标地址、浏览器的 Host 和需要原样转发的请求头。 */
    char host[MAXLINE];
    char port[16];
    char path[MAXLINE];
    char host_header[MAXLINE];
    size_t host_header_len;
    char other_headers[MAXLINE];
    size_t other_headers_len;
} request_t;

/* 每个缓存项沿用原来的字段；只有响应数据按实际长度分配。 */
typedef struct cache_block {
    char host[MAXLINE];
    char port[16];
    char path[MAXLINE];
    char *buf;
    size_t len;              /* 响应的实际字节数。 */
    uint64_t lru;           /* 数值越小，越久没有使用。 */
    struct cache_block *next;
} cache_block;

typedef struct {
    int *buf;       /* Buffer array */
    int n;          /* Maximum number of slots */
    int front;      /* buf[(front + 1) % n] is first item */
    int rear;       /* buf[rear % n] is last item */
    sem_t mutex;    /* Protects accesses to buf/front/rear */
    sem_t slots;    /* Number of available slots */
    sem_t items;    /* Number of available items */
} sbuf_t;

void sbuf_init(sbuf_t *sp, int n);
void sbuf_deinit(sbuf_t *sp);
void sbuf_insert(sbuf_t *sp, int item);
int sbuf_remove(sbuf_t *sp);
bool isValidHttp(rio_t *rio, int fd, request_t *req);
bool read_requesthdrs(rio_t *rio, int fd, request_t *req);
bool write_requesthdrs(int fd, request_t *req);
void read_responses(int clientfd, int serverfd, request_t *req);
bool parse_uri(const char *uri, char *host, size_t host_size, char *port, size_t port_size,
        char *path, size_t path_size);
void clienterror(int fd, char *cause, char *errnum, 
		char *shortmsg, char *longmsg);
void *thread(void *vargp);
bool reader(int clientfd, request_t *req);
void writer(const char *cache_data, const request_t *req, size_t len);

sbuf_t sbuf;  /* Shared queue of connected descriptors. */
cache_block *cache_head = NULL;
size_t cache_bytes = 0;    /* 只统计已缓存响应的实际字节数。 */
uint64_t cache_clock = 0;   /* 命中和插入时递增；未命中不修改。 */
int readcnt = 0;
sem_t mutex_cache;  /* Initially 1; protects readcnt. */
sem_t w;      /* Initially 1; protects the shared resource. */
sem_t lru_mutex;
sem_t turnstile;    /* 有写者等待时，不让后来的读者一直插队。 */
int main(int argc, char *argv[]) 
{
    Sem_init(&mutex_cache, 0, 1);
    Sem_init(&w, 0, 1);
    Sem_init(&lru_mutex, 0, 1);
    Sem_init(&turnstile, 0, 1);
    int listenfd, clientfd;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    pthread_t tid;
    sbuf_init(&sbuf, SBUFSIZE);
    Signal(SIGPIPE, SIG_IGN);

    /* Check command line args */
    if (argc != 2) {
	fprintf(stderr, "usage: %s <port>\n", argv[0]);
	exit(1);
    }

    listenfd = open_listenfd(argv[1]);
    if (listenfd < 0) {
        fprintf(stderr, "Could not listen on port %s\n", argv[1]);
        return 1;
    }
    
    for (int i = 0; i < NTHREADS; i++) {
        Pthread_create(&tid, NULL, thread, NULL);
    }
    while (1) {
        clientlen = sizeof(clientaddr);
        /* 修改 4：accept 失败时不要把无效的 -1 放入工作队列。 */
        clientfd = accept(listenfd, (SA *)&clientaddr, &clientlen);
        if (clientfd < 0) {
            if (errno != EINTR && errno != ECONNABORTED)
                fprintf(stderr, "accept error: %s\n", strerror(errno));
            continue;
        }

        sbuf_insert(&sbuf, clientfd);
    }
}

static bool same_object(const cache_block *entry, const request_t *req)
{
    return strcmp(entry->host, req->host) == 0 &&
           strcmp(entry->port, req->port) == 0 &&
           strcmp(entry->path, req->path) == 0;
}

static void free_cache_block(cache_block *entry)
{
    free(entry->buf);
    free(entry);
}

/* 写锁内淘汰最久没用的项，减去其实际响应字节数。 */
static void evict_lru(void)
{
    if (cache_head == NULL)
        return;
    cache_block *victim = cache_head;
    cache_block *victim_prev = NULL;
    cache_block *prev = NULL;

    for (cache_block *link = cache_head; link != NULL;
         prev = link, link = link->next) {
        if (link->lru < victim->lru) {
            victim = link;
            victim_prev = prev;
        }
    }

    /* 头节点没有前驱，需要单独修改 cache_head。 */
    if (victim_prev == NULL)
        cache_head = victim->next;
    else
        victim_prev->next = victim->next;

    cache_bytes -= victim->len;
    free_cache_block(victim);
}

bool reader(int clientfd, request_t *req)
{
    char cached_data[MAX_OBJECT_SIZE];
    size_t len = 0;
    bool hit = false;

    P(&turnstile);
    V(&turnstile);
    P(&mutex_cache);
    readcnt++;
    if (readcnt == 1)
        P(&w);               /* 首个读者阻止缓存写入和释放节点。 */
    V(&mutex_cache);

    for (cache_block *entry = cache_head; entry != NULL; entry = entry->next) {
        if (same_object(entry, req)) {
            /* 修改 2：仅在读锁内复制，随后可安全地释放缓存节点。 */
            len = entry->len;
            memcpy(cached_data, entry->buf, len);
            P(&lru_mutex);   /* 多个读者同时命中时，串行更新 LRU。 */
            entry->lru = ++cache_clock;
            V(&lru_mutex);
            hit = true;
            break;
        }
    }

    P(&mutex_cache);
    readcnt--;
    if (readcnt == 0)
        V(&w);
    V(&mutex_cache);

    /* 慢客户端只拖慢自己的线程，不再占用缓存读锁。 */
    if (hit)
        rio_writen(clientfd, cached_data, len);
    return hit;
}

void writer(const char *cache_data, const request_t *req, size_t len)
{
    if (len == 0 || len > MAX_OBJECT_SIZE || len > MAX_CACHE_SIZE)
        return;

    P(&turnstile);
    P(&w);

    /* 并发未命中可能让两个线程取回同一 URL；先删掉旧项。 */
    cache_block *prev = NULL;
    cache_block *link = cache_head;
    while (link != NULL) {
        if (same_object(link, req)) {
            if (prev == NULL)
                cache_head = link->next;
            else
                prev->next = link->next;
            cache_bytes -= link->len;
            free_cache_block(link);
            break;
        }
        prev = link;
        link = link->next;
    }

    /* 没有固定的 10 个槽；只在实际字节总量超限时淘汰。 */
    while (cache_bytes + len > MAX_CACHE_SIZE)
        evict_lru();

    cache_block *entry = malloc(sizeof(*entry));
    if (entry == NULL) {
        V(&w);
        V(&turnstile);
        return;
    }
    entry->buf = malloc(len);
    if (entry->buf == NULL) {
        free(entry);
        V(&w);
        V(&turnstile);
        return;
    }
    strcpy(entry->host, req->host);
    strcpy(entry->port, req->port);
    strcpy(entry->path, req->path);
    entry->len = len;
    memcpy(entry->buf, cache_data, len);
    entry->lru = ++cache_clock;
    entry->next = cache_head;
    cache_head = entry;
    cache_bytes += len;
    V(&w);
    V(&turnstile);
}

void *thread(void *vargp)
{
    Pthread_detach(pthread_self());

    while (1) {
        rio_t rio;
        request_t req;
        int serverfd;
        memset(&req, 0, sizeof(req));
        int clientfd = sbuf_remove(&sbuf);
        Rio_readinitb(&rio, clientfd);
        if (!isValidHttp(&rio, clientfd, &req)) {
            Close(clientfd);
            continue;
        }
        if (reader(clientfd, &req)) {
            Close(clientfd);
            continue;
        }
        serverfd = open_clientfd(req.host, req.port);
        if (serverfd < 0) {
            clienterror(clientfd, req.host, "502", "Bad Gateway",
                        "Could not connect to the target server");
            close(clientfd);
            continue;
        }
        if (write_requesthdrs(serverfd, &req)) {
            read_responses(clientfd, serverfd, &req);
        }
        Close(clientfd);
        Close(serverfd);
    }
}
/* $end tinymain */

/*
 * doit - handle one HTTP request/response transaction
 */
/* $begin doit */
bool isValidHttp(rio_t *rio, int fd, request_t *req) 
{
    char buf[MAXLINE];
    char request_line[MAXLINE];
    char method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char extra;
    ssize_t n;

    /* 修改 3：小写版本返回 -1，不会因单个连接读失败而退出进程。 */
    n = rio_readlineb(rio, buf, sizeof(buf));
    if (n <= 0)             /* 0：客户端关闭；-1：读取错误 */
        return false;

    /* 修改 5：先确认请求行完整，且没有内嵌 NUL 字节。 */
    if (n < 2 || buf[n - 2] != '\r' || buf[n - 1] != '\n' ||
        memchr(buf, '\0', (size_t)n) != NULL) {
        clienterror(fd, "request line", "400", "Bad Request",
                    "Invalid or incomplete request line");
        return false;
    }

    /* 第四个转换用于发现多余字段；恰好三个字段时返回值仍为 3。 */
    if (sscanf(buf, "%s %s %s %c", method, uri, version, &extra) != 3) {
        clienterror(fd, buf, "400", "Bad Request",
                "Proxy could not parse the request line");
        return false;
    }
    if (strcasecmp(method, "GET")) {                     
        clienterror(fd, method, "501", "Not Implemented",
                    "Tiny does not implement this method");
        return false;
    }
    if (strcasecmp(version, "HTTP/1.1") && (strcasecmp(version, "HTTP/1.0"))) {
        clienterror(fd, version, "505", "HTTP Version Not Supported",
                "Proxy does not support this HTTP version");
        return false;
    }                                                  
    if (!parse_uri(uri, req->host, sizeof(req->host), req->port, 
                    sizeof(req->port), req->path, sizeof(req->path))) {
        clienterror(fd, uri, "400", "Bad Request", "Invalid URI");
        return false;
    }
    /* 在连接服务器前检查转发请求行的长度，避免稍后静默断开。 */
    int request_len = snprintf(request_line, sizeof(request_line),
                               "GET %s HTTP/1.0\r\n", req->path);
    if (request_len < 0 || (size_t)request_len >= sizeof(request_line)) {
        clienterror(fd, uri, "400", "Bad Request", "Request line is too long");
        return false;
    }
    return read_requesthdrs(rio, fd, req);
}
/* $end doit */

/*
 * read_requesthdrs - read HTTP request headers
 */
/* $begin read_requesthdrs */

bool read_requesthdrs(rio_t *rio, int fd, request_t *req) 
{
    char line[MAXLINE];
    while(1) {   
        ssize_t n = rio_readlineb(rio, line, sizeof(line));
        if (n <= 0 || n < 2 || line[n - 2] != '\r' ||
            line[n - 1] != '\n' || memchr(line, '\0', n) != NULL) {
            clienterror(fd, "request headers", "400", "Bad Request",
                        "Invalid or incomplete request headers");
            return false;
        }       
        if (n == 2)
            return true; 

        char *colon = memchr(line, ':', n);
        if (colon == NULL || colon == line) {
            clienterror(fd, "request header", "400", "Bad Request",
                        "Invalid request header");
            return false;
        }

        size_t name_len = (size_t)(colon - line);
        if (!strncasecmp(line, "Host", name_len) && name_len == strlen("Host")) {
            if (req->host_header_len != 0) {
                clienterror(fd, "Host", "400", "Bad Request",
                            "Duplicate Host header");
                return false;
            }
            memcpy(req->host_header, line, n);
            req->host_header_len = (size_t)n;
        } else if ((!strncasecmp(line, "User-Agent", name_len) && name_len == strlen("User-Agent")) ||
                   (!strncasecmp(line, "Connection", name_len) && name_len == strlen("Connection")) ||
                   (!strncasecmp(line, "Proxy-Connection", name_len) && name_len == strlen("Proxy-Connection"))) {
            /* These are replaced by the proxy's own headers. */
        } else {
            if ((size_t)n > sizeof(req->other_headers) - req->other_headers_len) {
                clienterror(fd, "request headers", "431",
                            "Request Header Fields Too Large",
                            "Request headers are too large");
                return false;
            }
            memcpy(req->other_headers + req->other_headers_len, line, n);
            req->other_headers_len += (size_t)n;
        }
    }
    return true;
}

bool write_requesthdrs(int fd, request_t *req) 
{
    char line[MAXLINE];
    int n = snprintf(line, sizeof(line),
                 "GET %s HTTP/1.0\r\n", req->path);

    if (n < 0 || (size_t)n >= sizeof(line))
        return false;
    if (rio_writen(fd, line, strlen(line)) < 0) {
        return false;
    } 
    if (req->host_header_len != 0) {
        if (rio_writen(fd, (void *)req->host_header, req->host_header_len) < 0) {
            return false;
        } 
    } else {
        if (strcmp(req->port, "80") == 0)
            n = snprintf(line, sizeof(line), "Host: %s\r\n", req->host);
        else
            n = snprintf(line, sizeof(line), "Host: %s:%s\r\n", req->host, req->port);
        if (n < 0 || (size_t)n >= sizeof(line) || rio_writen(fd, line, n) < 0)
            return false;
    }

    if (rio_writen(fd, (void *)user_agent_hdr, strlen(user_agent_hdr)) < 0 ||
        rio_writen(fd, "Connection: close\r\n", 19) < 0 ||
        rio_writen(fd, "Proxy-Connection: close\r\n", 25) < 0)
        return false;

    if (req->other_headers_len != 0 &&
        rio_writen(fd, (void *)req->other_headers,
                req->other_headers_len) < 0)
    return false;

    return rio_writen(fd, "\r\n", 2) >= 0;
}

void read_responses(int clientfd, int serverfd, request_t *req) {
    rio_t rio;
    char buf[MAXLINE];
    char cache_data[MAX_OBJECT_SIZE];
    ssize_t n;
    size_t len = 0;
    
    Rio_readinitb(&rio, serverfd);


    /* 修改 1：只记录实际暂存字节；超限后不再计算剩余空间。 */
    bool cacheable = true;

    while ((n = rio_readnb(&rio, buf, sizeof(buf))) > 0) {
        if (cacheable) {
            if ((size_t)n <= sizeof(cache_data) - len) {
                memcpy(cache_data + len, buf, (size_t)n);
                len += (size_t)n;
            } else {
                cacheable = false;
            }
        }

        if (rio_writen(clientfd, buf, (size_t)n) < 0)
            return;
    }

    /* 只有正常到达 EOF 才缓存；读取出错不能缓存残缺响应。 */
    if (n == 0 && cacheable && len > 0)
        writer(cache_data, req, len);
}

/* $end read_requesthdrs */

/*
 * parse_uri - parse URI into filename and CGI args
 *             return 0 if dynamic content, 1 if static
 */
/* $begin parse_uri */
bool parse_uri(const char *uri,
               char *host, size_t host_size,
               char *port, size_t port_size,
               char *path, size_t path_size)
{
    // 本实验这里只接受 http://
    if (strncmp(uri, "http://", 7) != 0)
        return false;

    const char *start = uri + 7;         // 主机名的起点
    const char *slash = strchr(start, '/');    // 路径的起点
    const char *question = strchr(start, '?'); // 查询串的起点
    const char *end = start + strlen(start);
    if (slash && slash < end)
        end = slash;
    if (question && question < end)
        end = question;

    // 只在主机部分查找端口，路径和查询串中的冒号不算。
    const char *colon = memchr(start, ':', (size_t)(end - start));

    // 复制主机名
    size_t len = (size_t)((colon ? colon : end) - start);
    if (len == 0 || len >= host_size)
        return false;
    memcpy(host, start, len);
    host[len] = '\0';

    // 复制端口；没有指定时使用 80
    if (colon) {
        len = (size_t)(end - (colon + 1));
        if (len == 0 || len >= port_size)
            return false;
        // 端口必须是 1～65535 的十进制数字。
        unsigned int port_number = 0;
        for (const char *p = colon + 1; p < end; ++p) {
            if (*p < '0' || *p > '9')
                return false;
            port_number = port_number * 10 + (unsigned int)(*p - '0');
            if (port_number > 65535)
                return false;
        }
        if (port_number == 0)
            return false;
        memcpy(port, colon + 1, len);
        port[len] = '\0';
    } else {
        if (port_size < 3)
            return false;
        strcpy(port, "80");
    }

    // 只有查询串时补一个 /，例如 ?x=1 变成 /?x=1。
    if (*end == '?') {
        if (strlen(end) + 2 > path_size)
            return false;
        path[0] = '/';
        strcpy(path + 1, end);
    } else {
        const char *request_path = (*end == '/') ? end : "/";
        if (strlen(request_path) >= path_size)
            return false;
        strcpy(path, request_path);
    }

    return true;
}
/* $end parse_uri */

/*
 * clienterror - returns an error message to the client
 */
/* $begin clienterror */
static bool write_text(int fd, const char *text)
{
    return rio_writen(fd, (void *)text, strlen(text)) >= 0;
}

/* 修改 2：将来自请求的文字转义为 HTML，分段发送，不占用固定大缓冲区。 */
static bool write_html_escaped(int fd, const char *text)
{
    const char *segment = text;
    for (const char *p = text; *p != '\0'; ++p) {
        const char *escaped = NULL;
        switch (*p) {
        case '&':  escaped = "&amp;";  break;
        case '<':  escaped = "&lt;";   break;
        case '>':  escaped = "&gt;";   break;
        case '"': escaped = "&quot;"; break;
        case '\'': escaped = "&#39;";  break;
        }
        if (escaped != NULL) {
            if (p > segment &&
                rio_writen(fd, (void *)segment, (size_t)(p - segment)) < 0)
                return false;
            if (!write_text(fd, escaped))
                return false;
            segment = p + 1;
        }
    }
    return write_text(fd, segment);
}

void clienterror(int fd, char *cause, char *errnum, 
		 char *shortmsg, char *longmsg) 
{
    char buf[MAXLINE];
    int n;

    /* Print the HTTP response headers */
    n = snprintf(buf, sizeof(buf), "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    if (n < 0 || (size_t)n >= sizeof(buf) ||
        rio_writen(fd, buf, (size_t)n) < 0)
        return;
    if (!write_text(fd, "Content-type: text/html\r\n\r\n"))
        return;

    /* Print the HTTP response body */
    if (!write_text(fd, "<html><title>Tiny Error</title>"))
        return;
    if (!write_text(fd, "<body bgcolor=\"ffffff\">\r\n"))
        return;
    /* 所有格式化输出均检查长度；不再把超长 cause 放入 buf。 */
    n = snprintf(buf, sizeof(buf), "%s: %s\r\n<p>%s: ",
                 errnum, shortmsg, longmsg);
    if (n < 0 || (size_t)n >= sizeof(buf) ||
        rio_writen(fd, buf, (size_t)n) < 0)
        return;
    if (!write_html_escaped(fd, cause))
        return;
    write_text(fd, "\r\n<hr><em>The Tiny Web server</em>\r\n");
}
/* $end clienterror */

void sbuf_init(sbuf_t *sp, int n)
{
    sp->buf = Calloc(n, sizeof(int));
    sp->n = n;
    sp->front = sp->rear = 0;

    Sem_init(&sp->mutex, 0, 1);
    Sem_init(&sp->slots, 0, n);
    Sem_init(&sp->items, 0, 0);
}

void sbuf_deinit(sbuf_t *sp)
{
    Free(sp->buf);
}

void sbuf_insert(sbuf_t *sp, int item)
{
    P(&sp->slots);                         /* Wait for an empty slot. */
    P(&sp->mutex);                         /* Lock the buffer. */
    sp->buf[(++sp->rear) % sp->n] = item;
    V(&sp->mutex);                         /* Unlock the buffer. */
    V(&sp->items);                         /* Announce a new item. */
}

int sbuf_remove(sbuf_t *sp)
{
    int item;

    P(&sp->items);                         /* Wait for an item. */
    P(&sp->mutex);                         /* Lock the buffer. */
    item = sp->buf[(++sp->front) % sp->n];
    V(&sp->mutex);                         /* Unlock the buffer. */
    V(&sp->slots);                         /* Announce an empty slot. */

    return item;
}
