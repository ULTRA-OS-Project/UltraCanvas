// js-framework-benchmark data, with a seeded generator so every engine
// builds identical rows.
const A = ["pretty","large","big","small","tall","short","long","handsome","plain","quaint","clean","elegant","easy","angry","crazy","helpful","mushy","odd","unsightly","adorable","important","inexpensive","cheap","expensive","fancy"];
const C = ["red","yellow","blue","green","pink","brown","purple","brown","white","black","orange"];
const N = ["table","chair","house","bbq","desk","car","pony","cookie","sandwich","burger","pizza","mouse","keyboard"];
let seed = 1;
const random = (max) => { seed = (seed * 16807) % 2147483647; return seed % max; };
let nextId = 1;
export function buildData(count) {
  const d = new Array(count);
  for (let i = 0; i < count; i++)
    d[i] = { id: nextId++, label: `${A[random(A.length)]} ${C[random(C.length)]} ${N[random(N.length)]}` };
  return d;
}
