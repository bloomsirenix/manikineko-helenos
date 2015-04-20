/*
 * Copyright (c) 2015 Jiri Svoboda
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 * - The name of the author may not be used to endorse or promote products
 *   derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/** @addtogroup udp
 * @{
 */

/**
 * @file HelenOS service implementation
 */

#include <async.h>
#include <errno.h>
#include <inet/endpoint.h>
#include <io/log.h>
#include <ipc/services.h>
#include <ipc/udp.h>
#include <loc.h>
#include <macros.h>
#include <stdlib.h>

#include "assoc.h"
#include "msg.h"
#include "service.h"
#include "udp_type.h"

#define NAME "udp"

#define MAX_MSG_SIZE DATA_XFER_LIMIT

static void udp_cassoc_recv_msg(void *, udp_sockpair_t *, udp_msg_t *);

static udp_assoc_cb_t udp_cassoc_cb = {
	.recv_msg = udp_cassoc_recv_msg
};

static int udp_cassoc_queue_msg(udp_cassoc_t *cassoc, udp_sockpair_t *sp,
    udp_msg_t *msg)
{
	udp_crcv_queue_entry_t *rqe;

	log_msg(LOG_DEFAULT, LVL_DEBUG, "udp_cassoc_queue_msg(%p, %p, %p)",
	    cassoc, sp, msg);

	rqe = calloc(1, sizeof(udp_crcv_queue_entry_t));
	if (rqe == NULL)
		return ENOMEM;

	link_initialize(&rqe->link);
	rqe->sp = *sp;
	rqe->msg = msg;
	rqe->cassoc = cassoc;

//	fibril_mutex_lock(&assoc->lock);
	list_append(&rqe->link, &cassoc->client->crcv_queue);
//	fibril_mutex_unlock(&assoc->lock);

//	fibril_condvar_broadcast(&assoc->rcv_queue_cv);

	return EOK;
}

static void udp_ev_data(udp_client_t *client)
{
	async_exch_t *exch;

	log_msg(LOG_DEFAULT, LVL_DEBUG, "udp_ev_data()");

	exch = async_exchange_begin(client->sess);
	aid_t req = async_send_0(exch, UDP_EV_DATA, NULL);
	async_exchange_end(exch);

	async_forget(req);
}

static int udp_cassoc_create(udp_client_t *client, udp_assoc_t *assoc,
    udp_cassoc_t **rcassoc)
{
	udp_cassoc_t *cassoc;
	sysarg_t id;

	cassoc = calloc(1, sizeof(udp_cassoc_t));
	if (cassoc == NULL)
		return ENOMEM;

	/* Allocate new ID */
	id = 0;
	list_foreach (client->cassoc, lclient, udp_cassoc_t, cassoc) {
		if (cassoc->id >= id)
			id = cassoc->id + 1;
	}

	cassoc->id = id;
	cassoc->client = client;
	cassoc->assoc = assoc;

	list_append(&cassoc->lclient, &client->cassoc);
	*rcassoc = cassoc;
	return EOK;
}

static void udp_cassoc_destroy(udp_cassoc_t *cassoc)
{
	list_remove(&cassoc->lclient);
	free(cassoc);
}

static int udp_cassoc_get(udp_client_t *client, sysarg_t id,
    udp_cassoc_t **rcassoc)
{
	list_foreach (client->cassoc, lclient, udp_cassoc_t, cassoc) {
		if (cassoc->id == id) {
			*rcassoc = cassoc;
			return EOK;
		}
	}

	return ENOENT;
}

static void udp_cassoc_recv_msg(void *arg, udp_sockpair_t *sp, udp_msg_t *msg)
{
	udp_cassoc_t *cassoc = (udp_cassoc_t *) arg;

	udp_cassoc_queue_msg(cassoc, sp, msg);
	udp_ev_data(cassoc->client);
}

static int udp_assoc_create_impl(udp_client_t *client, inet_ep2_t *epp,
    sysarg_t *rassoc_id)
{
	udp_assoc_t *assoc;
	udp_cassoc_t *cassoc;
	udp_sock_t local;
	udp_sock_t remote;
	int rc;
	char *la, *ra;

	local.addr = epp->local.addr;
	local.port = epp->local.port;
	remote.addr = epp->remote.addr;
	remote.port = epp->remote.port;

	la = (char *)"xxx";
	ra = (char *)"yyy";

	(void) inet_addr_format(&epp->local.addr, &la);
	(void) inet_addr_format(&epp->remote.addr, &ra);

	log_msg(LOG_DEFAULT, LVL_DEBUG, "udp_assoc_create_impl la=%s ra=%s",
	    la, ra);

	assoc = udp_assoc_new(&local, &remote, NULL, NULL);
	if (assoc == NULL)
		return EIO;

	if (epp->local_link != 0)
		udp_assoc_set_iplink(assoc, epp->local_link);

	rc = udp_cassoc_create(client, assoc, &cassoc);
	if (rc != EOK) {
		assert(rc == ENOMEM);
		udp_assoc_delete(assoc);
		return ENOMEM;
	}

	assoc->cb = &udp_cassoc_cb;
	assoc->cb_arg = cassoc;

	udp_assoc_add(assoc);

	*rassoc_id = cassoc->id;
	return EOK;
}

static int udp_assoc_destroy_impl(udp_client_t *client, sysarg_t assoc_id)
{
	udp_cassoc_t *cassoc;
	int rc;

	rc = udp_cassoc_get(client, assoc_id, &cassoc);
	if (rc != EOK) {
		assert(rc == ENOENT);
		return ENOENT;
	}

	udp_assoc_remove(cassoc->assoc);
	udp_assoc_reset(cassoc->assoc);
	udp_assoc_delete(cassoc->assoc);
	udp_cassoc_destroy(cassoc);
	return EOK;
}

static int udp_assoc_send_msg_impl(udp_client_t *client, sysarg_t assoc_id,
    inet_ep_t *dest, void *data, size_t size)
{
	udp_msg_t msg;
	udp_sock_t remote;
	udp_cassoc_t *cassoc;
	int rc;

	rc = udp_cassoc_get(client, assoc_id, &cassoc);
	if (rc != EOK)
		return rc;

	remote.addr = dest->addr;
	remote.port = dest->port;

	msg.data = data;
	msg.data_size = size;
	rc = udp_assoc_send(cassoc->assoc, &remote, &msg);
	if (rc != EOK)
		return rc;

	return EOK;
}

static void udp_callback_create_srv(udp_client_t *client, ipc_callid_t iid,
    ipc_call_t *icall)
{
	log_msg(LOG_DEFAULT, LVL_DEBUG, "udp_callback_create_srv()");

	async_sess_t *sess = async_callback_receive(EXCHANGE_SERIALIZE);
	if (sess == NULL) {
		async_answer_0(iid, ENOMEM);
		return;
	}

	client->sess = sess;
	async_answer_0(iid, EOK);
}

static void udp_assoc_create_srv(udp_client_t *client, ipc_callid_t iid,
    ipc_call_t *icall)
{
	ipc_callid_t callid;
	size_t size;
	inet_ep2_t epp;
	sysarg_t assoc_id;
	int rc;

	log_msg(LOG_DEFAULT, LVL_DEBUG, "udp_assoc_create_srv()");

	if (!async_data_write_receive(&callid, &size)) {
		async_answer_0(callid, EREFUSED);
		async_answer_0(iid, EREFUSED);
		return;
	}

	if (size != sizeof(inet_ep2_t)) {
		async_answer_0(callid, EINVAL);
		async_answer_0(iid, EINVAL);
		return;
	}

	rc = async_data_write_finalize(callid, &epp, size);
	if (rc != EOK) {
		async_answer_0(callid, rc);
		async_answer_0(iid, rc);
		return;
	}

	rc = udp_assoc_create_impl(client, &epp, &assoc_id);
	if (rc != EOK) {
		async_answer_0(iid, rc);
		return;
	}

	async_answer_1(iid, EOK, assoc_id);
}

static void udp_assoc_destroy_srv(udp_client_t *client, ipc_callid_t iid,
    ipc_call_t *icall)
{
	sysarg_t assoc_id;
	int rc;

	log_msg(LOG_DEFAULT, LVL_DEBUG, "udp_assoc_destroy_srv()");

	assoc_id = IPC_GET_ARG1(*icall);
	rc = udp_assoc_destroy_impl(client, assoc_id);
	async_answer_0(iid, rc);
}

static void udp_assoc_send_msg_srv(udp_client_t *client, ipc_callid_t iid,
    ipc_call_t *icall)
{
	ipc_callid_t callid;
	size_t size;
	inet_ep_t dest;
	sysarg_t assoc_id;
	void *data;
	int rc;

	log_msg(LOG_DEFAULT, LVL_DEBUG, "udp_assoc_send_msg_srv()");

	/* Receive dest */

	if (!async_data_write_receive(&callid, &size)) {
		async_answer_0(callid, EREFUSED);
		async_answer_0(iid, EREFUSED);
		return;
	}

	if (size != sizeof(inet_ep_t)) {
		async_answer_0(callid, EINVAL);
		async_answer_0(iid, EINVAL);
		return;
	}

	rc = async_data_write_finalize(callid, &dest, size);
	if (rc != EOK) {
		async_answer_0(callid, rc);
		async_answer_0(iid, rc);
		return;
	}

	/* Receive message data */

	if (!async_data_write_receive(&callid, &size)) {
		async_answer_0(callid, EREFUSED);
		async_answer_0(iid, EREFUSED);
		return;
	}

	if (size > MAX_MSG_SIZE) {
		async_answer_0(callid, EINVAL);
		async_answer_0(iid, EINVAL);
		return;
	}

	data = malloc(size);
	if (data == NULL) {
		async_answer_0(callid, ENOMEM);
		async_answer_0(iid, ENOMEM);
	}

	rc = async_data_write_finalize(callid, data, size);
	if (rc != EOK) {
		async_answer_0(callid, rc);
		async_answer_0(iid, rc);
		free(data);
		return;
	}

	assoc_id = IPC_GET_ARG1(*icall);

	rc = udp_assoc_send_msg_impl(client, assoc_id, &dest, data, size);
	if (rc != EOK) {
		async_answer_0(iid, rc);
		free(data);
		return;
	}

	async_answer_0(iid, EOK);
	free(data);
}

static udp_crcv_queue_entry_t *udp_rmsg_get_next(udp_client_t *client)
{
	link_t *link;

	link = list_first(&client->crcv_queue);
	if (link == NULL)
		return NULL;

	return list_get_instance(link, udp_crcv_queue_entry_t, link);
}

static void udp_rmsg_info_srv(udp_client_t *client, ipc_callid_t iid,
    ipc_call_t *icall)
{
	ipc_callid_t callid;
	size_t size;
	inet_ep_t ep;
	udp_crcv_queue_entry_t *enext;
	sysarg_t assoc_id;
	int rc;

	log_msg(LOG_DEFAULT, LVL_DEBUG, "udp_rmsg_info_srv()");
	enext = udp_rmsg_get_next(client);

	if (!async_data_read_receive(&callid, &size)) {
		async_answer_0(callid, EREFUSED);
		async_answer_0(iid, EREFUSED);
		return;
	}

	if (enext == NULL) {
		async_answer_0(callid, ENOENT);
		async_answer_0(iid, ENOENT);
		return;
	}

	inet_ep_init(&ep);
	ep.addr = enext->sp.foreign.addr;
	ep.port = enext->sp.foreign.port;

	rc = async_data_read_finalize(callid, &ep, max(size, (ssize_t)sizeof(ep)));
	if (rc != EOK) {
		async_answer_0(iid, rc);
		return;
	}

	assoc_id = enext->cassoc->id;
	size = enext->msg->data_size;

	log_msg(LOG_DEFAULT, LVL_DEBUG, "udp_rmsg_info_srv(): assoc_id=%zu, "
	    "size=%zu", assoc_id, size);
	async_answer_2(iid, EOK, assoc_id, size);
}

static void udp_rmsg_read_srv(udp_client_t *client, ipc_callid_t iid,
    ipc_call_t *icall)
{
	ipc_callid_t callid;
	ssize_t msg_size;
	udp_crcv_queue_entry_t *enext;
	void *data;
	size_t size;
	ssize_t off;
	int rc;

	log_msg(LOG_DEFAULT, LVL_DEBUG, "udp_rmsg_read_srv()");
	off = IPC_GET_ARG1(*icall);

	enext = udp_rmsg_get_next(client);

	if (!async_data_read_receive(&callid, &size)) {
		async_answer_0(callid, EREFUSED);
		async_answer_0(iid, EREFUSED);
		return;
	}

	if (enext == NULL) {
		async_answer_0(callid, ENOENT);
		async_answer_0(iid, ENOENT);
		return;
	}

	data = enext->msg->data + off;
	msg_size = enext->msg->data_size;

	rc = async_data_read_finalize(callid, data, max(msg_size - off,
	    (ssize_t)size));
	if (rc != EOK) {
		async_answer_0(iid, rc);
		return;
	}

	async_answer_0(iid, EOK);
	log_msg(LOG_DEFAULT, LVL_DEBUG, "udp_rmsg_read_srv(): OK");
}

static void udp_rmsg_discard_srv(udp_client_t *client, ipc_callid_t iid,
    ipc_call_t *icall)
{
	udp_crcv_queue_entry_t *enext;

	log_msg(LOG_DEFAULT, LVL_DEBUG, "udp_rmsg_discard_srv()");

	enext = udp_rmsg_get_next(client);
	if (enext == NULL) {
		log_msg(LOG_DEFAULT, LVL_DEBUG, "usg_rmsg_discard_srv: enext==NULL");
		async_answer_0(iid, ENOENT);
		return;
	}

	list_remove(&enext->link);
	udp_msg_delete(enext->msg);
	free(enext);
	async_answer_0(iid, EOK);
}

static void udp_client_conn(ipc_callid_t iid, ipc_call_t *icall, void *arg)
{
	udp_client_t client;

	/* Accept the connection */
	async_answer_0(iid, EOK);

	log_msg(LOG_DEFAULT, LVL_DEBUG, "udp_client_conn()");

	list_initialize(&client.cassoc);
	list_initialize(&client.crcv_queue);

	while (true) {
		log_msg(LOG_DEFAULT, LVL_DEBUG, "udp_client_conn: wait req");
		ipc_call_t call;
		ipc_callid_t callid = async_get_call(&call);
		sysarg_t method = IPC_GET_IMETHOD(call);

		log_msg(LOG_DEFAULT, LVL_DEBUG, "udp_client_conn: method=%d",
		    (int)method);
		if (!method) {
			/* The other side has hung up */
			async_answer_0(callid, EOK);
			return;
		}

		switch (method) {
		case UDP_CALLBACK_CREATE:
			udp_callback_create_srv(&client, callid, &call);
			break;
		case UDP_ASSOC_CREATE:
			udp_assoc_create_srv(&client, callid, &call);
			break;
		case UDP_ASSOC_DESTROY:
			udp_assoc_destroy_srv(&client, callid, &call);
			break;
		case UDP_ASSOC_SEND_MSG:
			udp_assoc_send_msg_srv(&client, callid, &call);
			break;
		case UDP_RMSG_INFO:
			udp_rmsg_info_srv(&client, callid, &call);
			break;
		case UDP_RMSG_READ:
			udp_rmsg_read_srv(&client, callid, &call);
			break;
		case UDP_RMSG_DISCARD:
			udp_rmsg_discard_srv(&client, callid, &call);
			break;
		default:
			async_answer_0(callid, ENOTSUP);
			break;
		}
	}
}

int udp_service_init(void)
{
	int rc;
	service_id_t sid;

	async_set_client_connection(udp_client_conn);

	rc = loc_server_register(NAME);
	if (rc != EOK) {
		log_msg(LOG_DEFAULT, LVL_ERROR, "Failed registering server.");
		return EIO;
	}

	rc = loc_service_register(SERVICE_NAME_UDP, &sid);
	if (rc != EOK) {
		log_msg(LOG_DEFAULT, LVL_ERROR, "Failed registering service.");
		return EIO;
	}

	return EOK;
}

/**
 * @}
 */
