#ifndef PEAK_WEBAPI_H
#define PEAK_WEBAPI_H

#include "js.h"
#include "dom.h"

/*
 * Browser Web API layer (partial stubs — see kernel/gui/webapi_stubs.c).
 *
 * webapi_install() exposes quarantined stubs only:
 *   fetch            GET/POST http(s), same-origin/CORS; string body with POST
 *                    (bounded); AbortSignal via AbortController().signal;
 *                    Response.json() parses bodyText as JSON
 *   localStorage     VFS-backed under /var/peak/localStorage/<origin>; get/set/removeItem
 *   sessionStorage   in-memory per-tab map; get/set/removeItem; cleared on tab teardown
 *   AbortController  factory AbortController() → {signal, abort}
 *
 * Unsupported options fail closed with clear errors (no silent no-ops).
 * DOM ↔ JS bridge lives in browser_js.c (document, __dom_* helpers).
 * Classic <script src> loading is handled here, not in the stub layer.
 */

int webapi_install(struct js_runtime *rt, const char *page_url);

void webapi_set_tab(int tab_id, int private_tab);
/* Bind active store index + page URL used by fetch/storage stubs. */
void webapi_bind(int tab_id, int private_tab, const char *page_url);
void webapi_clear_tab(int tab_id);
/* After tab close: drop closed_idx stores and shift higher indices down. */
void webapi_compact_tab(int closed_idx);
/*
 * Optional hook: before stub entry, rebind g_web_* from the calling runtime
 * (browser registers a lookup of tab-by-js). NULL = leave globals unchanged.
 */
void webapi_set_runtime_binder(void (*fn)(struct js_runtime *rt));
void webapi_bind_runtime(struct js_runtime *rt);

int webapi_load_classic_scripts(struct js_runtime *rt, struct dom_document *doc,
                                const char *page_url);

#endif
