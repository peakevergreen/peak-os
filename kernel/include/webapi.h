#ifndef PEAK_WEBAPI_H
#define PEAK_WEBAPI_H

#include "js.h"
#include "dom.h"

/*
 * Browser Web API layer (partial stubs — see kernel/gui/webapi_stubs.c).
 *
 * webapi_install() exposes quarantined stubs only:
 *   fetch            GET/POST http(s), same-origin/CORS; string body with POST
 *                    (bounded); AbortSignal via AbortController().signal
 *   localStorage     in-memory per-tab map; get/set/removeItem; not disk
 *   sessionStorage   same as localStorage but cleared on tab teardown
 *   AbortController  factory AbortController() → {signal, abort}
 *
 * Unsupported options fail closed with clear errors (no silent no-ops).
 * DOM ↔ JS bridge lives in browser_js.c (document, __dom_* helpers).
 * Classic <script src> loading is handled here, not in the stub layer.
 */

int webapi_install(struct js_runtime *rt, const char *page_url);

void webapi_set_tab(int tab_id, int private_tab);
void webapi_clear_tab(int tab_id);

int webapi_load_classic_scripts(struct js_runtime *rt, struct dom_document *doc,
                                const char *page_url);

#endif
