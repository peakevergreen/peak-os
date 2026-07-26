/* Peak Browser Web API fail-closed fixture (documented expectations).
 * fetch rejects non-GET/POST / signal / body / extra init, empty or non-http(s) URLs;
 * Response.text() absent (use bodyText); localStorage has no clear/key/length.
 */
typeof AbortController; /* expect "function" */
typeof Response; /* expect "undefined" — use fetch() return value */
