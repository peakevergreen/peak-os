/* Peak JS fixture: Promise.allSettled / Promise.race lite. */
var settled = Promise.allSettled([Promise.resolve(1), 2, Promise.resolve(3)]);
var race = Promise.race([Promise.resolve(99), Promise.resolve(1)]);
print(JSON.stringify(settled));
print(race);
