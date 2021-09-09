#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <sys/time.h>
#include <string.h>

#ifdef DEBUG
#define DPRINT(x) do {printf x;} while (0)
#else
#define DPRINT(x) do {} while (0)
#endif

enum err {argNum, argType, listenSocket, bindSocket, selectErr, acceptErr}; 

enum diaState {listening, writing};

enum gameState {waiting, started};

struct client{
    int fd;
    char *buf;
    int bufPtr;
    int bufSize;
    enum diaState state;
    int var;
};

void clientInit(struct client *sess, const int num)
{
    int i;
    for (i=0; i<num; i++){
        sess[i].fd = -1;
        sess[i].bufPtr = 0;
        sess[i].bufSize = 10;
        sess[i].var = 0;
    }
}

char *intToStr(int num)
{
    int i=0, len, n=num;
    char *str;
    if (n == 0)
        len = 1;
    else{
        while (n != 0){
            i++;
            n /= 10;
        }
        len = i;
    }
    str = malloc(len+1);
    str[len] = 0;
    n = num;
    for (i=len-1; i>=0; i--){
        str[i] = (n % 10) + '0';
        n /= 10;
    } 
    return str;
}
int strToInt(const char *str, int *num)
{
    int i;
    *num=0;
    for (i=0; str[i]!=0; i++){
        if (str[i]<'0' || str[i]>'9')
            return -1;
        *num = 10*(*num)+(str[i]-'0');
    }
    return 0;
}

void errorReport(int err)
{
    printf("Error: ");
    switch (err){
        case argNum:
            printf("incorrect number of args\n");
            break;
        case argType:
            printf("incorrect args type: int required\n");
            break;
        case listenSocket:
            printf("can't make a listen socket\n");
            break;
        case bindSocket:
            printf("can't bind a listen socket\n");
            break;
        case selectErr:
            printf("select\n");
            break;
        case acceptErr:
            printf("accept\n");
            break;
    }
}
    
int makeListenSocket(int port)
{
    int ls, opt=1;
    struct sockaddr_in addr;
    ls = socket(AF_INET, SOCK_STREAM, 0);
    if (ls == -1){
        errorReport(listenSocket);
        return -1;
    } 
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(ls, (struct sockaddr*)&addr, sizeof(addr)) == -1){
        errorReport(bindSocket);
        return -1;
    }  
    listen(ls, 5); 
    return ls;
}

#if 0
void sessInc(struct client **sess, int *size)
{
    int i;
    struct client *tmp = malloc(2*(*size) * sizeof(*tmp));
    for (i=0; i<*size; i++)
        tmp[i] = *sess[i];
    free(*sess);
    *size = 2 * (*size);
    *sess = tmp;
}    
#endif

void newAccept(const int ls, const int num, struct  client *sess, 
               int *curSess, int *st)
{
    int d, cur = *curSess;
    char *str={"game has already started\n"};
    socklen_t len;
    struct sockaddr_in addr;
    d = accept(ls, (struct sockaddr*) &addr, &len);
    if (d == -1){
        errorReport(acceptErr);
        return;
    }
    if (*st == started){
        write(d, str, strlen(str)); 
        close(d);
        return;
    }
    sess[cur].buf = malloc(sess[cur].bufSize);
    sess[cur].fd = d;
    (*curSess)++;
    if (*curSess == num)
        *st = started;
} 

void makeCommand(struct client *cl, char *com, const int st)
{
    int num;
    char *str, *str1={"game has not started yet\n"};
    if (st == started){
        strToInt(com, &num);
        (*cl).var += num;
        str = intToStr((*cl).var);
        write((*cl).fd, str, strlen(str));
    } else 
        write((*cl).fd, str1, strlen(str1));
    DPRINT(("check for makeCommand %d\n",(*cl).var));
}

void bufCheckCom(struct client *cl, const int st) 
{
    int i, j;
    char *com;
    for (i=0; i<(*cl).bufSize; i++)
        if ((*cl).buf[i] == '\n')
            break;
    if (i == (*cl).bufSize)
        return;
    com = malloc(i * sizeof(*com)); 
    for (j=0; j<i; j++)
        com[j] = (*cl).buf[j];
    com[i] = 0;
    for (j=0; j<((*cl).bufPtr - i); j++) 
        (*cl).buf[j] = (*cl).buf[j+i+1];
    (*cl).bufPtr -= i+1;
    makeCommand(cl, com, st); 
}        


void incBuf(char **buf, int *bufSize)
{
    char *tmp;
    int i;
    tmp = malloc(2 * (*bufSize) * sizeof(*tmp));
    for (i=0; i< *bufSize; i++)
        *(tmp+i) = *(*buf+i);
    free(*buf);
    *buf = tmp;
    *bufSize *= 2;
}

int readFromClient(struct client *cl, const int st)
{
    int r;
    r = read((*cl).fd, (*cl).buf + (*cl).bufPtr, (*cl).bufSize - (*cl).bufPtr);
    if (r <= 0)
        return 0;
    (*cl).bufPtr += r;
    bufCheckCom(cl, st);
    if ((*cl).bufPtr >= (*cl).bufSize){
        incBuf(&((*cl).buf), &((*cl).bufSize));
    }
    return 1;
}    

int checkForClients(struct client *sess, const int num)
{
    int i, ctr;
    for (i=0; i<num; i++)
        ctr += (sess[i].fd >= 0);
    DPRINT(("CTR %d\n",ctr));
    return (ctr > 1);
}     

int closeClient(struct client *sess, const int i, const int num, int *curSess)
{
    struct client cl = sess[i];
    close(cl.fd);
    sess[i].fd = -2;
    free(cl.buf);
    sess[i].buf = NULL;
    if (*curSess < num)
        (*curSess)--;
    return checkForClients(sess, num);
}

void gameOver(struct client *sess, const int num, const int ls)
{
    int i;
    char *str={"Congrats, you won!"};
    for (i=0; i<num; i++)
        if (sess[i].fd >= 0)
            break;
    write(sess[i].fd, str, sizeof(str));
    close(sess[i].fd);
    free(sess[i].buf);
    close(ls); 
    free(sess);
}

void server(const int ls, const int num)
{
    struct client *sess;
    fd_set fds;
    int i, s, rd, curSess = 0, maxfd, st=waiting;
    sess = malloc(num * sizeof(*sess));
    clientInit(sess, num);
    for (;;){
        DPRINT(("first step in cycle\n"));
        maxfd = ls;
        FD_ZERO(&fds);
        FD_SET(ls, &fds);
        for (i=0; sess[i].fd != -1 && i<num; i++){
            FD_SET(sess[i].fd, &fds);
            if (sess[i].fd > maxfd)
                maxfd = sess[i].fd;
        }
        s = select(maxfd+1, &fds, NULL, NULL, NULL);
        if (s == -1) {
            errorReport(selectErr);
            return;
        }
        #if 0
        if (curSess >= sessSize)
            sessInc(&sess, &sessSize);
        #endif
        if (FD_ISSET(ls, &fds)){
            DPRINT(("before accept\n"));
            newAccept(ls, num, sess, &curSess, &st);             
            DPRINT(("after accept\n"));
        }
        DPRINT(("before check sess\n"));
        for (i=0; sess[i].fd != -1 && i<num; i++){
            DPRINT(("in for (check sess)\n"));
            if (FD_ISSET(sess[i].fd, &fds)){
                rd = readFromClient(sess+i, st);
                DPRINT(("RD %d\n",rd));
                if (!rd && !closeClient(sess, i, num, &curSess) && 
                   (st == started)){
                        gameOver(sess, num, ls);
                        return;
                }
            }
        }
        DPRINT(("after check sess\n"));
    }
}
                                        
int main(int argc, char **argv)
{
    int port, num, ls;
    if (argc != 3){
        errorReport(argNum);
        return 0;
    }
    if (strToInt(argv[1],&num) == -1 || strToInt(argv[2],&port) == -1){
        errorReport(argType);
        return 0;
    }
    ls = makeListenSocket(port);
    if (ls == -1)
        return 0;
    server(ls, num);
    return 0;
    
}