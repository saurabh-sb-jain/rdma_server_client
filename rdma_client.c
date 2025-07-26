#include "rdma_common.h"

static ctx_t ctx;

#define BUF_LEN (1024*1024*1024)

void proc_client_resp ()
{
    int rc = 0, num_wc = 0;
    struct rdma_conn_param conn_param;
    struct rdma_cm_event *event;
    struct sockaddr_in addr;
    buf_info_t c_mdata;
    buf_info_t s_mdata;
    struct ibv_wc wc[2];
    uint64_t *buf = NULL;
    struct ibv_mr *c_mr = NULL;
    struct ibv_mr *s_mr = NULL;
    struct ibv_mr *mr = NULL;
    struct ibv_sge recv_sge, send_sge;
    unsigned char c[MD5_DIGEST_LENGTH];
    struct timespec start, end;
    double delta = 0;

    ctx.pd = ibv_alloc_pd(ctx.id->verbs);
    if (!ctx.pd) {
        printf("error:%d in allocating PD\n", errno);
        goto proc_client_quit;
    }
    ctx.cc = ibv_create_comp_channel(ctx.id->verbs);
    if (!ctx.cc) {
        printf("error:%d in creating comp channel\n", errno);
        goto proc_client_quit;
    }
    ctx.cq = ibv_create_cq(ctx.id->verbs, CQ_CAPACITY, NULL, ctx.cc, 0);
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

    rc = rdma_create_qp(ctx.id, ctx.pd, &(ctx.qp_attr));
    if (rc != EOK) {
        printf("error:%d in rdma create qp\n", errno);
    }

    ctx.qp = ctx.id->qp;

    s_mr = ibv_reg_mr(ctx.pd, &s_mdata, sizeof(buf_info_t),
                          IBV_ACCESS_LOCAL_WRITE);
    if (!(s_mr)) {
        printf("error:%d in mdata reg mr\n", errno);
        goto proc_client_quit;
    }

    recv_sge.addr = s_mr->addr;
    recv_sge.length = s_mr->length;
    recv_sge.lkey = s_mr->lkey;
    bzero(&(ctx.recv_wr), sizeof(struct ibv_recv_wr));
    ctx.recv_wr.sg_list = &recv_sge;
    ctx.recv_wr.num_sge = 1;

    rc = ibv_post_recv(ctx.qp, &(ctx.recv_wr), &(ctx.bad_recv_wr));
    if (rc != EOK) {
        printf("error:%d in mdata post_recv\n", errno);
        goto proc_client_quit;
    }

    memset(&conn_param, 0, sizeof(conn_param));
    conn_param.initiator_depth = 3;
    conn_param.responder_resources = 3;

    rc = rdma_connect(ctx.id, &conn_param);
    if (rc != EOK) {
        printf("error:%d in rdma connect\n", errno);
    }

    rc = proc_cm_event(ctx.channel, &event, RDMA_CM_EVENT_ESTABLISHED);
    if (rc != EOK) {
        goto proc_client_quit;
    }

	memcpy(&addr, rdma_get_peer_addr(ctx.id), sizeof(struct sockaddr_in));
	printf("Connection accepted by %s\n", inet_ntoa(addr.sin_addr));

    buf = (uint64_t*)malloc(BUF_LEN);
    if (!buf) {
        printf("error: %d allocating buf\n", errno);
        goto proc_client_quit;
    } else {
        randomize(buf, BUF_LEN);
    }

    comp_md5((char*)buf, BUF_LEN, c);

    mr = ibv_reg_mr(ctx.pd, buf, BUF_LEN, (IBV_ACCESS_LOCAL_WRITE|
		            IBV_ACCESS_REMOTE_READ|
		            IBV_ACCESS_REMOTE_WRITE));
    if (!mr) {
        printf("error: %d registering buf mr\n", errno);
        goto proc_client_quit;
    }

    c_mdata.addr = mr->addr;
    c_mdata.length = mr->length;
    c_mdata.key = mr->lkey;

    c_mr = ibv_reg_mr(ctx.pd, &c_mdata, sizeof(buf_info_t),
                          IBV_ACCESS_LOCAL_WRITE);
    if (!(c_mr)) {
        printf("error:%d in mdata reg mr\n", errno);
        goto proc_client_quit;
    }

    send_sge.addr = c_mr->addr;
    send_sge.length = c_mr->length;
    send_sge.lkey = c_mr->lkey;
    bzero(&(ctx.send_wr), sizeof(struct ibv_send_wr));
    ctx.send_wr.sg_list = &send_sge;
    ctx.send_wr.num_sge = 1;
    ctx.send_wr.opcode = IBV_WR_SEND;
    ctx.send_wr.send_flags = IBV_SEND_SIGNALED;
    rc = ibv_post_send(ctx.qp, &(ctx.send_wr), &(ctx.bad_send_wr));
    if (rc != EOK) {
        printf("error:%d in mdata post_send\n", errno);
        goto proc_client_quit;
    }

    rc = proc_work_comp_events(ctx.cc, wc, 2, &num_wc);
    if (rc != EOK) {
        printf("error in processing work completion\n");
        goto proc_client_quit;
    }

    send_sge.addr = mr->addr;
    send_sge.length = mr->length;
    send_sge.lkey = mr->lkey;
    bzero(&(ctx.send_wr), sizeof(struct ibv_send_wr));
    ctx.send_wr.sg_list = &send_sge;
    ctx.send_wr.num_sge = 1;
    ctx.send_wr.opcode = IBV_WR_RDMA_WRITE;
    ctx.send_wr.send_flags = IBV_SEND_SIGNALED;
    ctx.send_wr.wr.rdma.rkey = s_mdata.key;
    ctx.send_wr.wr.rdma.remote_addr = s_mdata.addr;

    clock_gettime(CLOCK_MONOTONIC, &start);
    rc = ibv_post_send(ctx.qp, &(ctx.send_wr), &(ctx.bad_send_wr));
    if (rc != EOK) {
        printf("error:%d in posting RDMA_WRITE\n", errno);
        goto proc_client_quit;
    }

    rc = proc_work_comp_events(ctx.cc, wc, 1, &num_wc);
    if (rc != EOK) {
        printf("error in processing RDMA_WRITE work completion\n");
        goto proc_client_quit;
    }
    clock_gettime(CLOCK_MONOTONIC, &end);

    delta = (double)(end.tv_sec - start.tv_sec)*1000000000.0 +
            (double)(end.tv_nsec - start.tv_nsec);

    printf("RDMA WR BW: %f gbps\n", ((double)(BUF_LEN)*8)/delta);

    printf("Randomize Local Buffer\n");
    randomize(buf, BUF_LEN);
    comp_md5((char*)buf, BUF_LEN, c);

    send_sge.addr = mr->addr;
    send_sge.length = mr->length;
    send_sge.lkey = mr->lkey;
    bzero(&(ctx.send_wr), sizeof(struct ibv_send_wr));
    ctx.send_wr.sg_list = &send_sge;
    ctx.send_wr.num_sge = 1;
    ctx.send_wr.opcode = IBV_WR_RDMA_READ;
    ctx.send_wr.send_flags = IBV_SEND_SIGNALED;
    ctx.send_wr.wr.rdma.rkey = s_mdata.key;
    ctx.send_wr.wr.rdma.remote_addr = s_mdata.addr;
    clock_gettime(CLOCK_MONOTONIC, &start);
    rc = ibv_post_send(ctx.qp, &(ctx.send_wr), &(ctx.bad_send_wr));
    if (rc != EOK) {
        printf("error:%d in posting RDMA_WRITE\n", errno);
        goto proc_client_quit;
    }

    rc = proc_work_comp_events(ctx.cc, wc, 1, &num_wc);
    if (rc != EOK) {
        printf("error in processing RDMA_RECV work completion\n");
        goto proc_client_quit;
    }
    clock_gettime(CLOCK_MONOTONIC, &end);

    delta = (double)(end.tv_sec - start.tv_sec)*1000000000.0 +
            (double)(end.tv_nsec - start.tv_nsec);

    printf("RDMA RD BW: %f gbps\n", ((double)(BUF_LEN)*8)/delta);


    comp_md5((char*)buf, BUF_LEN, c);

    rc = rdma_disconnect(ctx.id);
    if (rc != EOK) {
        printf("error:%d in rdma disconnect\n", errno);
    }

    rc = proc_cm_event(ctx.channel, &event, RDMA_CM_EVENT_DISCONNECTED);

proc_client_quit:
    if (buf) {
        free(buf);
    }
    if (c_mr) {
        ibv_dereg_mr(c_mr);
    }
    if (s_mr) {
        ibv_dereg_mr(s_mr);
    }
    if (mr) {
        ibv_dereg_mr(mr);
    }
    rdma_destroy_qp(ctx.id);
    rc = rdma_destroy_id(ctx.id);
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

    rc = rdma_resolve_addr(ctx.id, NULL, (struct sockaddr*)&addr, 2000);
    if (rc != EOK) {
        printf("error:%d resolving addr", errno);
        goto quit;
    }

    rc = proc_cm_event(ctx.channel, &event, RDMA_CM_EVENT_ADDR_RESOLVED);
    if (rc != EOK) {
        goto quit;
    }

    rc = rdma_resolve_route(ctx.id, 2000);
    if (rc != EOK) {
        printf("error:%d resolving route", errno);
        goto quit;
    }

    rc = proc_cm_event(ctx.channel, &event, RDMA_CM_EVENT_ROUTE_RESOLVED);

    if (rc == EOK) {
        proc_client_resp();
    }

quit:
    if (ctx.channel) {
        rdma_destroy_event_channel(ctx.channel);
    }
    return (0);
}
