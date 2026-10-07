import { Component, signal } from '@angular/core';

const A = ["pretty","large","big","small","tall","short","long","handsome","plain","quaint","clean","elegant","easy","angry","crazy","helpful","mushy","odd","unsightly","adorable","important","inexpensive","cheap","expensive","fancy"];
const C = ["red","yellow","blue","green","pink","brown","purple","brown","white","black","orange"];
const N = ["table","chair","house","bbq","desk","car","pony","cookie","sandwich","burger","pizza","mouse","keyboard"];
let seed = 1;
const random = (max: number) => { seed = (seed * 16807) % 2147483647; return seed % max; };
let nextId = 1;
interface Row { id: number; label: string; }
function buildData(count: number): Row[] {
  const d = new Array<Row>(count);
  for (let i = 0; i < count; i++)
    d[i] = { id: nextId++, label: `${A[random(A.length)]} ${C[random(C.length)]} ${N[random(N.length)]}` };
  return d;
}

@Component({ selector: 'app-root', templateUrl: './app.html' })
export class App {
  readonly data = signal<Row[]>([]);
  readonly selected = signal(0);
  run() { this.data.set(buildData(1000)); this.selected.set(0); }
  runLots() { this.data.set(buildData(10000)); this.selected.set(0); }
  add() { this.data.set(this.data().concat(buildData(1000))); }
  update() {
    const d = this.data().slice();
    for (let i = 0; i < d.length; i += 10) d[i] = { id: d[i].id, label: d[i].label + ' !!!' };
    this.data.set(d);
  }
  clear() { this.data.set([]); this.selected.set(0); }
  swapRows() {
    const d = this.data().slice();
    if (d.length > 998) { const t = d[1]; d[1] = d[998]; d[998] = t; }
    this.data.set(d);
  }
  remove(id: number) { this.data.set(this.data().filter((r) => r.id !== id)); }
  select(id: number) { this.selected.set(id); }
}
