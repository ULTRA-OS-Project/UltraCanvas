import { bootstrapApplication } from '@angular/platform-browser';
import { ApplicationRef, VERSION, provideZoneChangeDetection } from '@angular/core';
import { App } from './app/app';

// zone.js runs change detection itself at the end of every event task, so
// flush() does nothing: a passing DOM check proves zone.js drove the update.
let appRef: ApplicationRef;
(globalThis as any).__app = {
  name: 'Angular ' + VERSION.full + ' (zone.js)',
  async mount(container: Element) {
    container.appendChild(document.createElement('app-root'));
    appRef = await bootstrapApplication(App, { providers: [provideZoneChangeDetection()] });
  },
  flush() {},
};
