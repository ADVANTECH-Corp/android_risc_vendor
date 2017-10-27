/*
 * Copyright 2013, Telit Communications S.p.a.
 *
 * This file is licensed under a commercial license agreement.
 */

#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include <alloca.h>
#include <getopt.h>
#include <sys/socket.h>
#include <termios.h>
#include <cutils/sockets.h>
#include <cutils/properties.h>

#include "ril.h"
#include <telephony/ril_cdma_sms.h>
#include "reference-ril.h"
#include "misc.h"
#include "serial/ril-serial.h"
#include "serial/atchannel.h"
#include "serial/at_tok.h"
#include "serial/at_timeout.h"

#define LOG_TAG "RIL-LOCATION"
#include <utils/Log.h>

#ifdef RIL_SHLIB
extern const struct RIL_Env *s_rilenv;
#endif

static int cellFromMONILineGsm(char *line, RIL_NeighboringCell *p_cell)
{
    int err;
    char *value_str;
    char *line_tmp;
    int lac, cid;

    err = at_tok_start(&line);
    if (err < 0) goto error;

    value_str = nextStrOccurrence(&line, "LAC:");
    if (value_str == NULL) goto error;

    line_tmp = line;
    err = at_tok_nexthexint(&line_tmp, &lac);
    if (err < 0) goto error;

    value_str = nextStrOccurrence(&line, "Id:");
    if (value_str == NULL) goto error;

    line_tmp = line;
    err = at_tok_nexthexint(&line_tmp, &cid);
    if (err < 0) goto error;

    if ( (0 == lac) && (0xffff == cid) )
        goto error;
    sprintf(p_cell->cid, "%04x%04x", lac, cid);

    value_str = nextStrOccurrence(&line, "PWR:");
    if (value_str == NULL) goto error;

    err = at_tok_nextint(&line, &(p_cell->rssi));
    if (err < 0) goto error;

    return 0;

error:
    return -1;
}

static int cellFromMONILineUmts(char *line, RIL_NeighboringCell *p_cell)
{
    int err;
    char *value_str;
    char *line_tmp;
    int psc;

    err = at_tok_start(&line);
    if (err < 0) goto error;

    value_str = nextStrOccurrence(&line, "PSC:");
    if (value_str == NULL) goto error;

    line_tmp = line;
    err = at_tok_nextint(&line_tmp, &psc);
    if (err < 0) goto error;
    sprintf(p_cell->cid, "%x", psc);

    value_str = nextStrOccurrence(&line, "RSCP:");
    if (value_str == NULL) goto error;

    line_tmp = line;
    err = at_tok_nextint(&line_tmp, &(p_cell->rssi));
    if (err < 0) goto error;

    return 0;

error:
    return -1;
}

static void requestGetNeighboringCellGsm(RIL_Token t)
{
    int err;
    char *cmd;
    ATResponse *pp_response[7];
    RIL_NeighboringCell p_cell[6];
    RIL_NeighboringCell **pp_cells;
    int availableCells = 0;
    int i;

    for (i = 0; i < 7; i++)
        pp_response[i] = NULL;

    for (i = 1; i < 7; i++) {
        asprintf(&cmd, "AT#MONI=%d;#MONI", i);
        err = at_send_command_singleline(cmd,
                "#MONI:",
                &pp_response[i],
                AT_TIMEOUT_NORMAL);
        free(cmd);
        if (err < 0 || pp_response[i]->success == 0)
            goto error;

        p_cell[availableCells].cid = alloca(sizeof(char) * 11);
        memset(p_cell[availableCells].cid, '\0', 11);
        err = cellFromMONILineGsm(pp_response[i]->p_intermediates->line,
                p_cell + availableCells);
        if (err == 0)
            availableCells++;
    }

    if (0 == availableCells) goto error;

    pp_cells = (RIL_NeighboringCell **)
            alloca(availableCells * sizeof(RIL_NeighboringCell *));

    for (i = 0; i < availableCells; i++)
        pp_cells[i] = &(p_cell[i]);

    RIL_onRequestComplete(t,
            RIL_E_SUCCESS,
            pp_cells,
            availableCells * sizeof (RIL_NeighboringCell *));

    for (i = 1; i < 7; i++)
        at_response_free(pp_response[i]);
    return;

error:
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);

    for (i = 1; i < 7; i++)
        at_response_free(pp_response[i]);
}

static void requestGetNeighboringCellUmts(RIL_Token t)
{
    int err;
    ATResponse *p_response = NULL;
    ATLine *p_cur = NULL;
    RIL_NeighboringCell *p_cells;
    RIL_NeighboringCell **pp_cells;
    int countCells;
    int i;

    err = at_send_command_multiline("AT#MONI=4;#MONI",
            "#MONI:",
            &p_response,
            AT_TIMEOUT_NORMAL);
    if (err != 0 || p_response->success == 0)
        goto error;

    for (countCells = 0, p_cur = p_response->p_intermediates;
            p_cur != NULL;
            p_cur = p_cur->p_next) {
        countCells++;
    }

    if (0 == countCells) {
        goto error;
    } else {
        pp_cells = (RIL_NeighboringCell **)
                alloca(countCells * sizeof(RIL_NeighboringCell *));
        p_cells = (RIL_NeighboringCell *)
                alloca(countCells * sizeof(RIL_NeighboringCell));
        memset (p_cells, 0, countCells * sizeof(RIL_NeighboringCell));

        for (i = 0,
                p_cur = p_response->p_intermediates;
                (i < countCells) && (NULL != p_cur);
                i++, p_cur = p_cur->p_next) {
            p_cells[i].cid = alloca(sizeof(char) * 11);
            memset(p_cells[i].cid, '\0', 11);
            err = cellFromMONILineUmts(p_cur->line, p_cells + i);
            if (err != 0)
                goto error;

            pp_cells[i] = &(p_cells[i]);
        }
    }

    RIL_onRequestComplete(t,
            RIL_E_SUCCESS,
            pp_cells,
            countCells * sizeof (RIL_NeighboringCell *));

    at_response_free(p_response);
    return;

error:
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);

    at_response_free(p_response);
}

void requestGetNeighboringCell(void *data __unused,
        size_t datalen __unused,
        RIL_Token t)
{
    int err;
    ATResponse *p_response = NULL;
    char *line = NULL;
    int value_int;
    char *value_str;
    int networkType;

    err = at_send_command_singleline("AT+COPS?",
            "+COPS:",
            &p_response,
            AT_TIMEOUT_COPS);
    if (err < 0 || p_response->success == 0)
        goto error;

    line = p_response->p_intermediates->line;
    err = at_tok_start(&line);
    if (err < 0) goto error;

    err = at_tok_nextint(&line, &value_int);
    if (err < 0) goto error;

    err = at_tok_nextint(&line, &value_int);
    if (err < 0) goto error;

    err = at_tok_nextstr(&line, &value_str);
    if (err < 0) goto error;

    err = at_tok_nextint(&line, &networkType);
    if (err < 0) goto error;

    at_response_free(p_response);
    p_response = NULL;

    if (0 == networkType)
        requestGetNeighboringCellGsm(t);
    else
        requestGetNeighboringCellUmts(t);
    return;

error:
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);

    at_response_free(p_response);
}

void requestSetLocationUpdates(void *data,
        size_t datalen __unused,
        RIL_Token t)
{
    int err;
    char *cmd;
    ATResponse *p_response = NULL;
    int enabled;

    enabled = ((int *)data)[0];

    asprintf(&cmd, "AT+CREG=%d", (enabled == 1) ? 2 : 1);

    err = at_send_command(cmd, &p_response, AT_TIMEOUT_NORMAL);
    free(cmd);
    if (err < 0 || p_response->success == 0)
        RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
    else
        RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);

    at_response_free(p_response);
}
