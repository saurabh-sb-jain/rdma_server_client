#include "rdma_common.h"

static ctx_t ctx;

void proc_client_req (struct rdma_cm_id *client)
{
    int rc = 0, num_wc = 0;
    struct rdma_conn_param conn_param;
    struct rdma_cm_event *event;
    struct sockaddr_in addr;
    buf_info_t mdata;
    struct ibv_wc wc;
    uint64_t *buf = NULL;
    struct ibv_mr *mdata_mr = NULL;
    struct ibv_mr *mr = NULL;
    struct ibv_sge sge;
    unsigned char c[MD5_DIGEST_LENGTH];

    ctx.pd = ibv_alloc_pd(client->verbs);
    if (!ctx.pd) {
        printf("error:%d in allocating PD\n", errno);
        goto proc_client_quit;
    }
    ctx.cc = ibv_create_comp_channel(client->verbs);
    if (!ctx.cc) {
        printf("error:%d in creating comp channel\n", errno);
        goto proc_client_quit;
    }
    ctx.cq = ibv_create_cq(client->verbs, CQ_CAPACITY, NULL, ctx.cc, 0);
    if (!ctx.cq) {
        printf("error:%d in cq create\n", errno);
        goto proc_client_quit;
    }
    rc = ibv_req_notify_cq(ctx.cq, 0);
    if (rc != EOK) {
        printf("error:%d in request notification on CQ\n", errno);
        goto proc_client_quit;
    }
    bzero(&(ctx.qp_attr), sizeof(struct ibv_qp_init_attr));
    ctx.qp_attr.cap.max_recv_sge = 2;
    ctx.qp_attr.cap.max_recv_wr = 8;
    ctx.qp_attr.cap.max_send_sge = 2;
    ctx.qp_attr.cap.max_send_wr = 8;
    ctx.qp_attr.qp_type = IBV_QPT_RC;
    ctx.qp_attr.recv_cq = ctx.cq;
    ctx.qp_attr.send_cq = ctx.cq;

    rc = rdma_create_qp(client, ctx.pd, &(ctx.qp_attr));
    if (rc != EOK) {
        printf("error:%d in rdma create qp\n", errno);
        goto proc_client_quit;
    }

    ctx.qp = client->qp;

    mdata_mr = ibv_reg_mr(ctx.pd, &mdata, sizeof(buf_info_t),
                          IBV_ACCESS_LOCAL_WRITE);
    if (!(mdata_mr)) {
        printf("error:%d in mdata reg mr\n", errno);
        goto proc_client_quit;
    }

    sge.addr = mdata_mr->addr;
    sge.length = mdata_mr->length;
    sge.lkey = mdata_mr->lkey;
    bzero(&(ctx.recv_wr), sizeof(struct ibv_recv_wr));
    ctx.recv_wr.sg_list = &sge;
    ctx.recv_wr.num_sge = 1;

    rc = ibv_post_recv(ctx.qp, &(ctx.recv_wr), &(ctx.bad_recv_wr));
    if (rc != EOK) {
        printf("error:%d in mdata post_recv\n", errno);
        goto proc_client_quit;
    }

    memset(&conn_param, 0, sizeof(conn_param));
    conn_param.initiator_depth = 3;
    conn_param.responder_resources = 3;

    rc = rdma_accept(client, &conn_param);
    if (rc != EOK) {
        printf("error:%d in rdma conn accept\n", errno);
        goto proc_client_quit;
    }

    rc = proc_cm_event(ctx.channel, &event, RDMA_CM_EVENT_ESTABLISHED);
    if (rc != EOK) {
        goto proc_client_quit;
    }
    memcpy(&addr, rdma_get_peer_addr(client), sizeof(struct sockaddr_in));
    printf("Connection accepted from %s\n", inet_ntoa(addr.sin_addr));

    rc = proc_work_comp_events(ctx.cc, &wc, 1, &num_wc);
    if (rc != EOK) {
        printf("error in processing work completion\n");
        goto proc_client_quit;
    }

    printf("Client requesting buffer of length: %d\n", mdata.length);
    ibv_dereg_mr(mdata_mr);

    buf = (uint64_t*)malloc(mdata.length);
    if (!buf) {
        printf("error: %d allocating buf\n", errno);
        goto proc_client_quit;
    } else {
        randomize(buf, mdata.length);
    }

    comp_md5((char*)buf, mdata.length, c);

    mr = ibv_reg_mr(ctx.pd, buf, mdata.length, (IBV_ACCESS_LOCAL_WRITE|
		            IBV_ACCESS_REMOTE_READ|
		            IBV_ACCESS_REMOTE_WRITE));
    if (!mr) {
        printf("error: %d registering buf mr\n", errno);
        goto proc_client_quit;
    }

    mdata.addr = mr->addr;
    mdata.key = mr->lkey;

    mdata_mr = ibv_reg_mr(ctx.pd, &mdata, sizeof(buf_info_t),
                          IBV_ACCESS_LOCAL_WRITE);
    if (!(mdata_mr)) {
        printf("error:%d in mdata reg mr\n", errno);
        goto proc_client_quit;
    }

    sge.addr = mdata_mr->addr;
    sge.length = mdata_mr->length;
    sge.lkey = mdata_mr->lkey;

    bzero(&(ctx.send_wr), sizeof(struct ibv_send_wr));

    ctx.send_wr.sg_list = &sge;
    ctx.send_wr.num_sge = 1;
    ctx.send_wr.opcode = IBV_WR_SEND;
    ctx.send_wr.send_flags = IBV_SEND_SIGNALED;

    rc = ibv_post_send(ctx.qp, &(ctx.send_wr), &(ctx.bad_send_wr));
    if (rc != EOK) {
        printf("error:%d in mdata post_send\n", errno);
        goto proc_client_quit;
    }

    rc = proc_work_comp_events(ctx.cc, &wc, 1, &num_wc);
    if (rc != EOK) {
        printf("error in processing work completion\n");
        goto proc_client_quit;
    }

    /* Wait for disconnect from client */
    rc = proc_cm_event(ctx.channel, &event, RDMA_CM_EVENT_DISCONNECTED);

    comp_md5((char*)buf, mdata.length, c);

proc_client_quit:
    if (buf) {
        free(buf);
    }
    if (mdata_mr) {
        ibv_dereg_mr(mdata_mr);
    }
    if (mr) {
        ibv_dereg_mr(mr);
    }
    rdma_destroy_qp(client);
    rc = rdma_destroy_id(client);
    if (rc != EOK) {
        printf("error:%d in rdma destroy id\n", errno);
    }
    if (ctx.cq) {
        ibv_destroy_cq(ctx.cq);
    }
    if (ctx.cc) {
        ibv_destroy_comp_channel(ctx.cc);
    }
    if (ctx.pd) {
        ibv_dealloc_pd(ctx.pd);
    }
}

int main (int argc, char **argv)
{
    struct sockaddr_in addr;
    int rc = 0;
    struct rdma_cm_event *event;

    bzero(&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    if (argc == 1) {
        addr.sin_addr.s_addr = inet_addr(SRVR_ADDR);
    } else {
        addr.sin_addr.s_addr = inet_addr(argv[1]);
    }
    addr.sin_port = htons(SRVR_PORT);

    ctx.channel = rdma_create_event_channel();
    if (!ctx.channel) {
        printf("error:%d in creating rdma event channel\n", errno);
        goto quit;
    }

    rc = rdma_create_id(ctx.channel, &(ctx.id), NULL, RDMA_PS_TCP);
    if (rc != EOK) {
        printf("error:%d in creating rdma id\n", errno);
        goto quit;
    }

    rc = rdma_bind_addr(ctx.id, (struct sockaddr*) &addr);
    if (rc != EOK) {
        printf("error:%d in binding addr\n", errno);
        goto quit;
    }

    rc = rdma_listen(ctx.id, 1);
    if (rc != EOK) {
        printf("error:%d in listening over id\n", errno);
        goto quit;
    }

    while (1) {
        printf("Waiting for connection from client...\n");
        rc = rdma_get_cm_event(ctx.channel, &event);
        if (rc != EOK) {
            printf("error:%d in get cm events\n", errno);
        } else {
            printf("received event: %d\n", event->event);
            if (event->status != 0) {
                printf("error:%d in event status\n", event->status);
            }
            if (event->event == RDMA_CM_EVENT_CONNECT_REQUEST) {
                if (event->id > 0) {
                    proc_client_req(event->id);
                } else {
                    printf("invalid client id:%d\n", event->id);
                }
            }
            rc = rdma_ack_cm_event(event);
            if (rc != EOK) {
                printf("error:%d in acking event\n", errno);
            }
        }
    }

quit:
    if (ctx.id) {
        rdma_destroy_id(ctx.id);
    }
    if (ctx.channel) {
        rdma_destroy_event_channel(ctx.channel);
    }
    return (0);
}
