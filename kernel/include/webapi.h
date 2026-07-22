#ifndef PEAK_WEBAPI_H
#define PEAK_WEBAPI_H

#include "js.h"
#include "dom.h"

/* Install fetch, storage, AbortController stubs on a tab runtime. */
int webapi_install(struct js_runtime *rt, const char *page_url);

/* Per-tab / private-tab storage isolation. */
void webapi_set_tab(int tab_id, int private_tab);
void webapi_clear_tab(int tab_id);

/* Fetch classic external <script src> in document order and eval. */
int webapi_load_classic_scripts(struct js_runtime *rt, struct dom_document *doc,
                                const char *page_url);

#endif
