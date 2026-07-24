/* Peak Browser Web API fail-closed fixture (documented expectations).
 * AbortController absent; fetch rejects non-GET / signal / body / extra init,
 * empty or non-http(s) URLs; storage is in-memory getItem/setItem only
 * (no removeItem/clear; empty/oversized keys fail closed).
 */
typeof AbortController; /* expect "undefined" */
