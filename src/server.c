//
// Created by jyjia on 2016/4/29.
//

#include "server.h"
#include "log.h"

void read_bucket(int bucket_id, socket_read_bucket_r *read_bucket_ctx, storage_ctx *sto_ctx) {
    //TODO Check Valid Bits To Decrease Bandwidth
    oram_bucket *bucket = get_bucket(bucket_id, sto_ctx);
    memcpy(&read_bucket_ctx->bucket, bucket, sizeof(oram_bucket));
    read_bucket_ctx->bucket_id = (unsigned int) bucket_id;
}

void write_bucket(int bucket_id, oram_bucket *bucket, storage_ctx *sto_ctx) {
    oram_bucket *bucket_sto = get_bucket(bucket_id, sto_ctx);
    memcpy(bucket_sto, bucket, sizeof(oram_bucket));
    bucket_sto->read_counter = 0;
    memset(bucket_sto->valid_bits, 1, sizeof(bucket_sto->valid_bits));
}

void get_metadata(int pos, socket_get_metadata_r *meta_ctx, storage_ctx *sto_ctx) {
    meta_ctx->pos = pos;
    oram_bucket *bucket;
    int i = 0;
    for (; ; pos >>= 1, ++i) {
        bucket = get_bucket(pos, sto_ctx);
        memcpy(meta_ctx->metadata[i].encrypt_metadata, bucket->encrypt_metadata, ORAM_CRYPT_META_SIZE);
        meta_ctx->metadata[i].read_counter = bucket->read_counter;
        memcpy(meta_ctx->metadata[i].valid_bits, bucket->valid_bits, sizeof(bucket->valid_bits));
        if (pos == 0)
            break;
    }
}

void read_block(int pos, int offsets[], socket_read_block_r *read_block_ctx, storage_ctx *sto_ctx) {
    int i = 0, j;
    oram_bucket *bucket;
    unsigned char return_block[ORAM_CRYPT_DATA_SIZE];
    bzero(return_block, ORAM_CRYPT_DATA_SIZE);
    for (; ; pos >>= 1, ++i) {
        // NO Memcpy here to save time
        bucket = get_bucket(pos, sto_ctx);
        bucket->valid_bits[offsets[i]] = 0;
        bucket->read_counter++;
        //TODO convert char to int to decrease xor
        for (j = 0;j < ORAM_CRYPT_DATA_SIZE;j++)
            return_block[j] ^= bucket->data[offsets[i]][j];
        if (pos == 0)
            break;
    }
    read_block_ctx->pos = pos;
    memcpy(read_block_ctx->data, return_block, ORAM_CRYPT_DATA_SIZE);
}

void init_server(int size, storage_ctx *sto_ctx) {
    int i = 0;
    for (; i <= size; i++) {
        sto_ctx->bucket_list[i] = malloc(sizeof(oram_bucket));
        sto_ctx->bucket_list[i]->read_counter = 0;
        memset(sto_ctx->bucket_list[i]->valid_bits, 1, sizeof(sto_ctx->bucket_list[i]->valid_bits));
    }
    sto_ctx->size = size;
    sto_ctx->mem_counter = size;
}

//TODO USE LESS SHARE BUFFER, MORE HEAP
void server_run(oram_args_t *args) {
    server_ctx *sv_ctx = malloc(sizeof(server_ctx));
    storage_ctx *sto_ctx = malloc(sizeof(storage_ctx));
    bzero(sv_ctx, sizeof(server_ctx));
    bzero(sto_ctx, sizeof(storage_ctx));
    sock_init(&sv_ctx->server_addr, &sv_ctx->addrlen, &sv_ctx->socket, args->host, args->port);
    sv_ctx->buff = malloc(ORAM_SOCKET_BUFFER);
    sv_ctx->buff_r = malloc(ORAM_SOCKET_BUFFER);
    sv_ctx->running = 1;
    while (sv_ctx->running == 1) {
        bzero(sv_ctx->buff, ORAM_SOCKET_BUFFER);
        bzero(sv_ctx->buff_r, ORAM_SOCKET_BUFFER);
        recvfrom(sv_ctx->socket, sv_ctx->buff, ORAM_SOCKET_BUFFER,
                 0, (struct sockaddr *)&sv_ctx->client_addr,
                 &sv_ctx->addrlen);
        socket_ctx *sock_ctx = (socket_ctx *)sv_ctx->buff;
        socket_ctx *sock_ctx_r = (socket_ctx *)sv_ctx->buff_r;
        socket_write_bucket *write_ctx = (socket_write_bucket *)sock_ctx->buf;
        socket_read_block *read_block_ctx = (socket_read_block *)sock_ctx->buf;
        socket_read_block_r *read_block_ctx_r = (socket_read_block_r *)sock_ctx_r->buf;
        socket_read_bucket *read_bucket_ctx = (socket_read_bucket *)sock_ctx->buf;
        socket_read_bucket_r *read_bucket_ctx_r = (socket_read_bucket_r *)sock_ctx_r->buf;
        socket_get_metadata *get_metadata_ctx = (socket_get_metadata *)sock_ctx_r->buf;
        socket_get_metadata_r *get_metadata_ctx_r = (socket_get_metadata_r *)sock_ctx_r->buf;
        switch (sock_ctx->type) {
            case SOCKET_READ_BUCKET: read_bucket(read_bucket_ctx->bucket_id,
                                                 read_bucket_ctx_r, sto_ctx);
                sock_ctx_r->type = SOCKET_READ_BUCKET;
                sendto(sv_ctx->socket, sv_ctx->buff_r, ORAM_SOCKET_READ_SIZE_R,
                       0, (struct sockaddr *)&sv_ctx->client_addr, sv_ctx->addrlen);
                logf("Message->Bucket Read");
                break;
            case SOCKET_WRITE_BUCKET:
                sock_ctx_r->type = SOCKET_WRITE_BUCKET;
                write_bucket(write_ctx->bucket_id, &write_ctx->bucket, sto_ctx);
                logf("Message->Bucket Write");
                break;
            case SOCKET_GET_META:
                sock_ctx_r->type = SOCKET_GET_META;
                get_metadata(get_metadata_ctx->pos, get_metadata_ctx_r, sto_ctx);
                sendto(sv_ctx->socket, sv_ctx->buff_r, ORAM_SOCKET_META_SIZE_R,
                       0, (struct sockaddr *)&sv_ctx->client_addr, sv_ctx->addrlen);
                logf("Message->Metadata Request");
                break;
            case SOCKET_READ_BLOCK:
                read_block(read_block_ctx->pos, read_block_ctx->offsets, read_block_ctx_r, sto_ctx);
                sendto(sv_ctx->socket, sv_ctx->buff_r, ORAM_SOCKET_BLOCK_SIZE_R,
                       0, (struct sockaddr *)&sv_ctx->client_addr, sv_ctx->addrlen);
                logf("Message->Block Read");
                break;
            case SOCKET_INIT:
                init_server(((socket_init *)sock_ctx->buf)->size, sto_ctx);
                logf("Message->Server Init");
                break;
            default:
                break;
        }
    }
}