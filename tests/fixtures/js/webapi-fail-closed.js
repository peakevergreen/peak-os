/* Peak Browser Web API fail-closed fixture (documented expectations).
 * AbortController absent; fetch rejects non-GET / signal / body;
 * storage is in-memory getItem/setItem only.
 */
typeof AbortController; /* expect "undefined" */
