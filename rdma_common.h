#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>

#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include <rdma/rdma_cma.h>
#include <infiniband/verbs.h>
#include <sys/time.h>
#include <time.h>
#include <openssl/md5.h>

#define EOK         (0)
#define SRVR_ADDR   "10.1.0.2"
#define SRVR_PORT   10000
#define CQ_CAPACITY (16)

typedef struct ctx_ {
    struct rdma_event_channel *channel;
    struct rdma_cm_id *id;
    struct ibv_pd *pd;
    struct ibv_comp_channel *cc;
    struct ibv_cq *cq;
    struct ibv_qp_init_attr qp_attr;
    struct ibv_qp *qp;
    struct ibv_recv_wr recv_wr;
    struct ibv_recv_wr *bad_recv_wr;
    struct ibv_send_wr send_wr;
    struct ibv_send_wr *bad_send_wr;
    struct ibv_sge sge;
} ctx_t;

typedef struct buf_info_ {
    uint64_t addr;
    uint32_t length;
    uint32_t key;
} buf_info_t __attribute((packed));

int
proc_cm_event (struct rdma_event_channel *channel, struct rdma_cm_event **evt,
               enum rdma_cm_event_type exp)
{
    int rc = EOK, lrc = EOK;
    struct rdma_cm_event *event = *evt;

    rc = rdma_get_cm_event(channel, &event);
    if (rc != EOK) {
        printf("error:%d in get cm events\n", errno);
        rc = errno;
    } else {
        printf("received event: %s\n", rdma_event_str(event->event));
        if (event->status != 0) {
            printf("error:%d in event status\n", event->status);
            rc = event->status;
        }
        if (event->event == exp) {
            printf("received expected event: %s\n", rdma_event_str(exp));
        } else {
            printf("received unexpected event: %s\n",
                    rdma_event_str(event->event));
            rc = EINVAL;
        }
        lrc = rdma_ack_cm_event(event);
        if (lrc != EOK) {
            printf("error:%d in acking event\n", errno);
        }
    }

    return (rc);
}

int proc_work_comp_events (struct ibv_comp_channel *cc,
		                   struct ibv_wc *wc, int max_wc, int *num_wc)
{
    struct ibv_cq *cq_ptr = NULL;
    void *context = NULL;
    int rc = EOK, i, total_wc = 0;

	rc = ibv_get_cq_event(cc, &cq_ptr, &context);
    if (rc) {
       printf("error: %d in getting cq events\n", errno);
       return rc;
    }

    rc = ibv_req_notify_cq(cq_ptr, 0);
    if (rc != EOK) {
       printf("error: %d in req notify cq\n", errno);
       goto quit;
    }

    total_wc = 0;
    do {
        rc = ibv_poll_cq(cq_ptr, max_wc - total_wc, wc + total_wc);
        if (rc < 0) {
            printf("error: %d in polling cq for wc\n", errno);
            goto quit;
        }
        total_wc += rc;
        rc = EOK;
    } while (total_wc < max_wc);

    for (i = 0 ; i < total_wc ; i++) {
        if (wc[i].status != IBV_WC_SUCCESS) {
            printf("error: %s at WC idx %d\n", ibv_wc_status_str(wc[i].status), i);
            rc = EIO;
            goto quit;
        } else {
            printf("received success for opcode %d at WC idx %d\n",
                    wc[i].opcode, i);
        }
    }

quit:
    ibv_ack_cq_events(cq_ptr, 1);

    *num_wc = total_wc;

    return rc;
}

void
randomize (char *buf, uint32_t len)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);

    srandom(tv.tv_usec);

    memset(buf, random(), len);
}

void
comp_md5 (char *buf, uint32_t len, unsigned char *c)
{
    int i;

    MD5_CTX mdContext;
    MD5_Init(&mdContext);
    MD5_Update(&mdContext, buf, len);
    MD5_Final(c, &mdContext);

    printf("MD5: ");
    for (i = 0; i < MD5_DIGEST_LENGTH; i++) {
        printf("%02x", c[i]);
    }
    printf("\n");
}