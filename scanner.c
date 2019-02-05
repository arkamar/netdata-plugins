/* SPDX-License-Identifier: GPL-3.0-or-later */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "netdata.h"
#include "callbacks.h"

#include "scanner.h"

struct scanner_statistics {
	int clear;
	int clamdscan;
	int spam_tagged;
	int spam_rejected;
	int spam_deleted;
	int other;

	int sc_0;
	int sc_1;

	int cc_0;
	int cc_1;
};

static
void *
scanner_data_init() {
	struct scanner_statistics * ret;
	ret = calloc(1, sizeof * ret);
	return ret;
}

static
void
scanner_clear(struct scanner_statistics * data) {
	memset(data, 0, sizeof * data);
}

static
void
scanner_process(const char * line, struct scanner_statistics * data) {
	if (strstr(line, "Clear")) {
		data->clear++;
	} else if (strstr(line, "CLAMDSCAN")) {
		data->clamdscan++;
	} else if (strstr(line, ":SPAM-TAGGED")) {
		data->spam_tagged++;
	} else if (strstr(line, ":SPAM-REJECTED")) {
		data->spam_rejected++;
	} else if (strstr(line, ":SPAM-DELETED")) {
		data->spam_deleted++;
	} else {
		data->other++;
	}

	if (strstr(line, ":SC:0")) {
		data->sc_0++;
	} else if (strstr(line, ":SC:1")) {
		data->sc_1++;
	}

	if (strstr(line, ":CC:1")) {
		data->cc_1++;
	} else {
		/* scanner does not report :CC:0 correctly, therefore we will dclare CC:0
		 * everything else from CC:1. */
		/* TODO: Fix this when scanner logger is fixed */
		data->cc_0++;
	}
}

static
void
scanner_print_hdr(const char * name) {
	nd_chart("scannerd", "scanner", "type", "", "", "volume", "scannerd", "type", ND_CHART_TYPE_STACKED);
	nd_dimension("clear", "Clear", ND_ALG_ABSOLUTE, 1, 1, ND_VISIBLE);
	nd_dimension("clamdscan", "Clamdscan", ND_ALG_ABSOLUTE, 1, 1, ND_VISIBLE);
	nd_dimension("spam_tagged", "SPAM Tagged", ND_ALG_ABSOLUTE, 1, 1, ND_VISIBLE);
	nd_dimension("spam_rejected", "SPAM Rejected", ND_ALG_ABSOLUTE, 1, 1, ND_VISIBLE);
	nd_dimension("spam_deleted", "SPAM Deleted", ND_ALG_ABSOLUTE, 1, 1, ND_VISIBLE);
	nd_dimension("other", "Other", ND_ALG_ABSOLUTE, 1, 1, ND_VISIBLE);

	nd_chart("scannerd", "scanner", "sc", "", "AntiSPAM Cache", "percentage", "scannerd", "sc", ND_CHART_TYPE_STACKED);
	nd_dimension("sc_0", "SC:0", ND_ALG_PERCENTAGE_OF_ABSOLUTE_ROW, 1, 1, ND_VISIBLE);
	nd_dimension("sc_1", "SC:1", ND_ALG_PERCENTAGE_OF_ABSOLUTE_ROW, 1, 1, ND_VISIBLE);

	nd_chart("scannerd", "scanner", "cc", "", "AntiVirus Cache", "percentage", "scannerd", "cc", ND_CHART_TYPE_STACKED);
	/* scanner does not report :CC:0 correctly, therefore we will dclare CC:0
	 * everything else from CC:1. Let's call it "The Rest" to force change in scanner
	 * developer group */
	/* TODO: Change "The Rest" to CC:0 when scanner logger is fixed */
	nd_dimension("cc_0", "The Rest", ND_ALG_PERCENTAGE_OF_ABSOLUTE_ROW, 1, 1, ND_VISIBLE);
	nd_dimension("cc_1", "CC:1", ND_ALG_PERCENTAGE_OF_ABSOLUTE_ROW, 1, 1, ND_VISIBLE);
	fflush(stdout);
}

static
void
scanner_print(const char * name, const struct scanner_statistics * data,
		const unsigned long time) {
	nd_begin_time("scannerd", "scanner", "type", time);
	nd_set("clear", data->clear);
	nd_set("clamdscan", data->clamdscan);
	nd_set("spam_tagged", data->spam_tagged);
	nd_set("spam_rejected", data->spam_rejected);
	nd_set("spam_deleted", data->spam_deleted);
	nd_set("other", data->other);
	nd_end();

	nd_begin_time("scannerd", "scanner", "sc", time);
	nd_set("sc_0", data->sc_0);
	nd_set("sc_1", data->sc_1);
	nd_end();

	nd_begin_time("scannerd", "scanner", "cc", time);
	nd_set("cc_0", data->cc_0);
	nd_set("cc_1", data->cc_1);
	nd_end();

	fflush(stdout);
}

static
struct stat_func scanner = {
	.init = &scanner_data_init,
	.fini = &free,

	.print_hdr = scanner_print_hdr,
	.print = scanner_print,

	.process = scanner_process,

	.postprocess = NULL,
	.clear = &scanner_clear,
};

struct stat_func * scanner_func = &scanner;