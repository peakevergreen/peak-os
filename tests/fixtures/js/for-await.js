/* for-await lite over settled promises */
var sum = 0;
for await (x of [Promise.resolve(1), Promise.resolve(2), 3]) {
  sum = sum + x;
}
sum;
