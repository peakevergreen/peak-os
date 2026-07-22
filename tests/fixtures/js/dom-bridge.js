/* Peak Browser DOM bridge fixture (runs under browser bindings) */
var el = document.createElement("p");
textContent(el, "hello");
textContent(el); /* expect "hello" via print in harness */
