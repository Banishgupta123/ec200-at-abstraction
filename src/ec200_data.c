/**
 * @file ec200_data.c
 * @brief PDP context / packet data connection implementation.
 */

#include "ec200_data.h"
#include "ec200_at.h"

#include <string.h>
#include <stdio.h>

static const char * const pdp_type_str[] = {
    [EC200_PDP_TYPE_IP]     = "IP",
    [EC200_PDP_TYPE_IPV6]   = "IPV6",
    [EC200_PDP_TYPE_IPV4V6] = "IPV4V6",
};

ec200_status_t ec200_data_set_pdp(ec200_handle_t          *h,
                                  const ec200_pdp_context_t *ctx)
{
    if (!ctx || ctx->cid < 1 || ctx->cid > 16) return EC200_ERR_PARAM;
    if ((int)ctx->type > EC200_PDP_TYPE_IPV4V6)  return EC200_ERR_PARAM;

    char cmd[128];
    snprintf(cmd, sizeof(cmd), "AT+CGDCONT=%u,\"%s\",\"%s\"",
             (unsigned)ctx->cid,
             pdp_type_str[(int)ctx->type],
             ctx->apn);
    return ec200_at_send(h, cmd, NULL, 0, EC200_AT_TIMEOUT_DEFAULT);
}

ec200_status_t ec200_data_activate(ec200_handle_t *h, uint8_t cid)
{
    if (cid < 1 || cid > 16) return EC200_ERR_PARAM;
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "AT+CGACT=1,%u", (unsigned)cid);
    return ec200_at_send(h, cmd, NULL, 0, EC200_AT_TIMEOUT_LONG);
}

ec200_status_t ec200_data_deactivate(ec200_handle_t *h, uint8_t cid)
{
    if (cid < 1 || cid > 16) return EC200_ERR_PARAM;
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "AT+CGACT=0,%u", (unsigned)cid);
    return ec200_at_send(h, cmd, NULL, 0, EC200_AT_TIMEOUT_LONG);
}

ec200_status_t ec200_data_get_ip(ec200_handle_t *h,
                                 uint8_t         cid,
                                 char           *ip_buf,
                                 size_t          ip_buf_sz)
{
    if (!ip_buf || ip_buf_sz == 0) return EC200_ERR_PARAM;

    char cmd[32];
    snprintf(cmd, sizeof(cmd), "AT+CGPADDR=%u", (unsigned)cid);

    char resp[128];
    ec200_status_t st = ec200_at_send_wait(h, cmd, "+CGPADDR:",
                                           resp, sizeof(resp),
                                           EC200_AT_TIMEOUT_DEFAULT);
    if (st != EC200_OK) return st;

    /* +CGPADDR: <cid>,<PDP_addr> */
    const char *comma = strchr(resp, ',');
    if (!comma) return EC200_ERR_PARSE;
    comma++;
    /* Strip surrounding quotes if present */
    if (*comma == '"') comma++;

    size_t alen = strlen(comma);
    if (alen > 0 && comma[alen - 1] == '"') alen--;
    if (alen >= ip_buf_sz) alen = ip_buf_sz - 1U;

    memcpy(ip_buf, comma, alen);
    ip_buf[alen] = '\0';
    return EC200_OK;
}

ec200_status_t ec200_data_connect(ec200_handle_t    *h,
                                  ec200_pdp_context_t *ctx)
{
    if (!ctx) return EC200_ERR_PARAM;

    ec200_status_t st = ec200_data_set_pdp(h, ctx);
    if (st != EC200_OK) return st;

    st = ec200_data_activate(h, ctx->cid);
    if (st != EC200_OK) return st;

    return ec200_data_get_ip(h, ctx->cid,
                             ctx->ip_addr, sizeof(ctx->ip_addr));
}
