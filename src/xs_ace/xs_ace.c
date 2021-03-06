/* MIT License. */

/* This file is taken from TF Protocol source code.
    It may have variations to adapt it to EPOCH Protocol. */

/*  programmer: luis miguel
    email: lmdelbahia@gmail.com  */

#include <xs_ace/xs_ace.h>
#include <stdio.h>
#include <stdlib.h>
#include <tfproto.h>
#include <limits.h>
#include <inttypes.h>
#include <util.h>
#include <stdarg.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <wait.h>
#include <string.h>
#include <pthread.h>

/* Auth token max length. */
#define MAXAUTHTOK 256
/* HDR signal flag. */
#define HDRSIGFLAG -1

enum cltype { CLOSE_FD, CLOSE_FP };

struct rundat {
    int pdin[2];
    int pdout[2];
    FILE *fin;
    FILE *fout;
    pthread_barrier_t barrier;
    int rdth_rc;
    int wrth_rc;
    pid_t pid;
};

/* Endpoint api info. */
struct endpt {
    char bin[PATH_MAX];
    char auth[MAXAUTHTOK];
    char *key;
    int mode;
};

static void runendpt(enum epoch_mode mode, char *endpt);
static void lstclose(enum cltype type, int argc, ...);
static int sendfunc(struct rundat *rdat);
static int rcvfunc(struct rundat *rdat);
static char **getargs(char *arg);
static void *rdthread(void *pp);
static void *wrthread(void *pp);
static int sendes(int es);
static int sec_waitpid(int pid);
static void runendpt_bk(char *endpt);

void xs_ace(enum epoch_mode mode, char *endpt)
{
    if (mode == DETACHED)
        runendpt_bk(endpt);
    else
        runendpt(mode, endpt);
}

static void runendpt(enum epoch_mode mode, char *endpt)
{
    int rs; 
    int pdin[2];
    int pdout[2];
    rs = pipe(pdin);
    rs += pipe(pdout);
    if (rs) 
        endcomm(1);
    pid_t pid = fork();
    if (pid == -1)
        endcomm(1);
    if (!pid) {
        lstclose(CLOSE_FD, 3, STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO);
        lstclose(CLOSE_FD, 2, pdin[1], pdout[0]);
        dup(pdin[0]);
        dup(pdout[1]);
        dup(pdout[1]);
        lstclose(CLOSE_FD, 2, pdin[0], pdout[1]);
        sigset_t mask;
        sigemptyset(&mask);
        pthread_sigmask(SIG_SETMASK, &mask, NULL);
        char **argl = getargs(endpt);
        execvp(*argl, argl);
        exit(EXIT_FAILURE);
    }
    lstclose(CLOSE_FD, 2, pdout[1], pdin[0]);
    struct rundat rdat;
    rdat.pdin[0] = pdin[0];
    rdat.pdin[1] = pdin[1];
    rdat.pdout[0] = pdout[0];
    rdat.pdout[1] = pdout[1];
    rdat.pid = pid;
    int64_t hdr = 0;
    int es;
    switch (mode) {
    case SINGLE_THREAD_SNDFIRST:
        if (sendfunc(&rdat) == -1)
            endcomm(1);
        if (rcvfunc(&rdat) == -1)
            endcomm(1);
        es = sec_waitpid(pid);
        if (sendes(es) == -1)
            endcomm(1);
        /* Finishing the two-way handsaheke. */
        readbuf_ex((char *) &hdr, sizeof hdr);
        break;
    case SINGLE_THREAD_RCVFIRST:
        if (rcvfunc(&rdat) == -1)
            endcomm(1);
        if (sendfunc(&rdat) == -1)
            endcomm(1);
        es = sec_waitpid(pid);
        if (sendes(es) == -1)
            endcomm(1);
        /* Finishing the two-way handsaheke. */
        writebuf_ex((char *) &hdr, sizeof hdr);
        break;
    default:;
        pthread_t rdth, wrth;
        rs = pthread_barrier_init(&rdat.barrier, NULL, 3);
        if (rs)
            endcomm(1);
        rs += pthread_create(&wrth, NULL, wrthread, &rdat);
        rs += pthread_create(&rdth, NULL, rdthread, &rdat);
        if (rs)
            endcomm(1);
        pthread_barrier_wait(&rdat.barrier);
        pthread_barrier_destroy(&rdat.barrier);
        if (rdat.wrth_rc || rdat.rdth_rc)
            endcomm(1);
        es = sec_waitpid(pid);
        if (sendes(es) == -1)
            endcomm(1);
        /* Finishing the two-way handsaheke. Reverse order at client side. */
        readbuf_ex((char *) &hdr, sizeof hdr);
        hdr = 0;
        writebuf_ex((char *) &hdr, sizeof hdr);
    }
}

static void lstclose(enum cltype type, int argc, ...)
{    
    va_list list;
    va_start(list, argc);
    int i = 0;
    for (; i < argc; i++)
        if (type == CLOSE_FD)
            close(va_arg(list, int));
        else if (type == CLOSE_FP)
            fclose(va_arg(list, FILE *));
    va_end(list);
}

static int sendfunc(struct rundat *rdat)
{
    int64_t hdr;
    while (1) {
        if (readbuf_ex((char *) &hdr, sizeof hdr) == -1) {
            kill(rdat->pid, SIGKILL);
            sec_waitpid(rdat->pid);
            return -1;
        }
        if (!isbigendian())
            swapbo64(hdr);
        if (!hdr)
            break;
        else if (hdr == HDRSIGFLAG) {
            if (readbuf_ex((char *) &hdr, sizeof hdr) == -1) {
                kill(rdat->pid, SIGKILL);
                return -1;
            }
            if (!isbigendian())
                swapbo64(hdr);
            kill(rdat->pid, hdr);
            continue;
        }
        if (readbuf_ex(comm.rxbuf, hdr) == -1) {
            kill(rdat->pid, SIGKILL);
            sec_waitpid(rdat->pid);
            return -1;
        }
        writechunk(rdat->pdin[1], comm.rxbuf, hdr);
    }
    close(rdat->pdin[1]);
    return 0;
}

static int rcvfunc(struct rundat *rdat)
{
    int64_t hdr, buflen;
    while ((buflen = readchunk(rdat->pdout[0], comm.txbuf, comm.bufsz)) > 0) {
        hdr = buflen;
        if (!isbigendian())
            swapbo64(hdr);
        if (writebuf_ex((char *) &hdr, sizeof hdr) == -1)
            return -1;
        if (writebuf_ex(comm.txbuf, buflen) == -1)
            return -1;
    }
    hdr = 0;
    if (writebuf_ex((char *) &hdr, sizeof hdr) == -1)
        return -1;
    close(rdat->pdout[0]);
    return 0;
}

char **getargs(char *endpt)
{
    char *pt = endpt;
    int arglen = 1;
    do
        if (*pt == ' ')
            arglen++;
    while (*pt++);
    char **argls = malloc(sizeof(char *) * (arglen + 2)); 
    if (!argls)
        return NULL;
    *(argls + arglen) = NULL;
    char *strpt = strtok(endpt, " ");
    int c = 0;
    for (; c < arglen; c++) {
        *(argls + c) = malloc(strlen(strpt) + 1);
        strcpy(*(argls + c), strpt);
        strpt = strtok(NULL, " ");
    }
    return argls;
}

static void *rdthread(void *pp)
{
    ((struct rundat *) pp)->rdth_rc = sendfunc(pp);
    pthread_barrier_wait(&((struct rundat *) pp)->barrier);
    return NULL;
}

static void *wrthread(void *pp)
{
    ((struct rundat *) pp)->wrth_rc = rcvfunc(pp);
    pthread_barrier_wait(&((struct rundat *) pp)->barrier);
    return NULL;
}

static int sendes(int es)
{
    if (!isbigendian())
        swapbo32(es);
    if (writebuf_ex((char *) &es, sizeof es) == -1)
        return -1;
    return 0;
}

static int sec_waitpid(int pid)
{
    int rs; 
    int32_t es = 0;
    do
        rs = waitpid(pid, &es, 0);
    while (rs == -1 && errno == EINTR);
    return es;
}

static void runendpt_bk(char *endpt)
{
    pid_t pid = fork();
    if (pid == -1)
        endcomm(1);
    if (!pid) {
        pid = fork();
        if (!pid) {
            sigset_t mask;
            sigemptyset(&mask);
            pthread_sigmask(SIG_SETMASK, &mask, NULL);
            char **argl = getargs(endpt);
            execvp(*argl, argl);
            exit(EXIT_FAILURE);
        }
        exit(EXIT_FAILURE);
    }
    sec_waitpid(pid);
}
