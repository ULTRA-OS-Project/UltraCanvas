import { bootstrapApplication } from '@angular/platform-browser';
import { ApplicationRef, VERSION, provideZonelessChangeDetection } from '@angular/core';
import { App } from './app/app';

let appRef: ApplicationRef;
(globalThis as any).__app = {
  name: 'Angular ' + VERSION.full + ' (zoneless)',
  async mount(container: Element) {
    container.appendChild(document.createElement('app-root'));
    appRef = await bootstrapApplication(App, { providers: [provideZonelessChangeDetection()] });
    appRef.tick();
  },
  flush() { appRef.tick(); },
};
