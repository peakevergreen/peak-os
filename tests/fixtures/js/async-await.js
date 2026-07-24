/* Peak JS fixture: async/await + settled Promise unwrap. */
async function add(a, b) {
  return await Promise.resolve(a + b);
}
await add(2, 3);
