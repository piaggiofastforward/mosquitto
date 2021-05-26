/*
Copyright (c) 2020 Roger Light <roger@atchoo.org>

All rights reserved. This program and the accompanying materials
are made available under the terms of the Eclipse Public License 2.0
and Eclipse Distribution License v1.0 which accompany this distribution.

The Eclipse Public License is available at
   https://www.eclipse.org/legal/epl-2.0/
and the Eclipse Distribution License is available at
  http://www.eclipse.org/org/documents/edl-v10.php.

SPDX-License-Identifier: EPL-2.0 OR BSD-3-Clause

Contributors:
   Roger Light - initial implementation and documentation.
*/

#ifdef WITH_XTREPORT

/* This file allows reporting of internal parameters to a kcachegrind
 * compatible output file. It is for debugging purposes only and is most likely
 * of no interest to end users.
 */
#include "config.h"

#include <stdint.h>
#include <stdio.h>
#include <uthash.h>

#include "mosquitto_broker_internal.h"
#include "mosquitto_internal.h"
#include "net_mosq.h"


static void client_cost(FILE *fptr, struct mosquitto *context, int fn_index)
{
	size_t pkt_count, pkt_bytes;
	size_t cmsg_count, cmsg_bytes;
	struct mosquitto__packet *pkt_tmp;
	size_t tBytes;

	pkt_count = 1;
	pkt_bytes = context->in_packet.packet_length;
	if(context->current_out_packet){
		pkt_count++;
		pkt_bytes += context->current_out_packet->packet_length;
	}
	pkt_tmp = context->out_packet;
	while(pkt_tmp){
		pkt_count++;
		pkt_bytes += pkt_tmp->packet_length;
		pkt_tmp = pkt_tmp->next;
	}

	cmsg_count = (size_t)context->msgs_in.msg_count;
	cmsg_bytes = (size_t)context->msgs_in.msg_bytes;
	cmsg_count += (size_t)context->msgs_out.msg_count;
	cmsg_bytes += (size_t)context->msgs_out.msg_bytes;

	tBytes = pkt_bytes + cmsg_bytes;
	if(context->id){
		tBytes += strlen(context->id);
	}
	fprintf(fptr, "%d %ld %lu %lu %lu %lu %d 0 0\n", fn_index,
			tBytes,
			pkt_count, cmsg_count,
			pkt_bytes, cmsg_bytes,
			context->sock == INVALID_SOCKET?0:context->sock);
}

static void report_subscriptions(FILE *fptr, struct mosquitto *context, int *fn_index_max)
{
	int i, j;
	struct mosquitto__subhier *subhier;
	int topic_count;
	char *topics[100];

	for(i=0; i<context->sub_count; i++){
		if(context->subs && context->subs[i]){
			subhier = context->subs[i];
			topic_count = 0;
			do{
				topics[topic_count] = subhier->topic;
				subhier = subhier->parent;
				topic_count++;
				if(topic_count == 100){
					break;
				}
			}while(subhier);
			if(topic_count == 100){
				continue;
			}

			fprintf(fptr, "cfn=(%d) ", *fn_index_max);
			if(topics[topic_count-2] && topics[topic_count-2][0] == '\0'){
				topic_count--;
			}
			for(j=topic_count-2; j>0; j--){
				if(topics[j]){
					fprintf(fptr, "%s/", topics[j]);
				}
			}
			if(topics[0]){
				fprintf(fptr, "%s\n", topics[0]);
			}

			fprintf(fptr, "calls=1 %d\n", *fn_index_max);
			fprintf(fptr, "1 0 0 0 0 0 0 1 0\n");
			(*fn_index_max)++;
		}
	}
}


void xtreport(void)
{
	pid_t pid;
	char filename[40];
	FILE *fptr;
	struct mosquitto *context, *ctxt_tmp;
	int fn_index = 2, fn_index_max;
	static int iter = 1;

	pid = getpid();
	snprintf(filename, 40, "/tmp/xtmosquitto.kcg.%d.%d", pid, iter);
	iter++;
	fptr = fopen(filename, "wt");
	if(fptr == NULL) return;

	fprintf(fptr, "# callgrind format\n");
	fprintf(fptr, "version: 1\n");
	fprintf(fptr, "creator: mosquitto\n");
	fprintf(fptr, "pid: %d\n", pid);
	fprintf(fptr, "cmd: mosquitto\n\n");

	fprintf(fptr, "positions: line\n");
	fprintf(fptr, "event: tB : total bytes\n");
	fprintf(fptr, "event: pkt : currently queued packets\n");
	fprintf(fptr, "event: cmsg : currently pending client messages\n");
	fprintf(fptr, "event: pktB : currently queued packet bytes\n");
	fprintf(fptr, "event: cmsgB : currently pending client message bytes\n");
	fprintf(fptr, "event: sock : client socket number\n");
	fprintf(fptr, "event: sub : client subscriptions\n");
	fprintf(fptr, "event: refc : message store ref counts\n");
	fprintf(fptr, "events: tB pkt cmsg pktB cmsgB sock sub refc\n");

	fprintf(fptr, "\nfn=(1) clients\n");
	fprintf(fptr, "1 0 0 0 0 0 0 0 0\n");

	fn_index = 2;
	HASH_ITER(hh_id, db.contexts_by_id, context, ctxt_tmp){
		if(context->id){
			fprintf(fptr, "cfn=(%d) %s\n", fn_index, context->id);
		}else{
			fprintf(fptr, "cfn=(%d) unknown\n", fn_index);
		}
		fprintf(fptr, "calls=1 %d\n", fn_index);
		client_cost(fptr, context, fn_index);
		fn_index++;
	}
	fn_index_max = fn_index;

	fn_index = 2;
	HASH_ITER(hh_id, db.contexts_by_id, context, ctxt_tmp){
		fprintf(fptr, "\nfn=(%d)\n", fn_index);
		client_cost(fptr, context, fn_index);
		fn_index++;

		report_subscriptions(fptr, context, &fn_index_max);
	}

	fprintf(fptr, "\nfn=(%d) messages\n", fn_index_max);
	fprintf(fptr, "1 0 0 0 0 0 0 0 0\n");

	struct mosquitto_msg_store *tail;
	tail = db.msg_store;

	while(tail){
		fprintf(fptr, "cfn=(%d) %" PRIu64 "\n", fn_index_max, tail->db_id);
		fprintf(fptr, "calls=%d %d\n", tail->ref_count, fn_index_max);
		fprintf(fptr, "%d 0 0 0 0 0 0 0 %d\n", fn_index_max, tail->ref_count);

		fn_index_max++;
		tail = tail->next;
	}

	fclose(fptr);
}
#endif
