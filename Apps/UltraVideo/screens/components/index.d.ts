// UltraVideo — the neutral timeline model drawn by UltraVideo.timeline().
// It mirrors the module-free model UltraCanvasVideoTimeline edits; the app converts it to and from VideoFX segments.

export type TrackKind = 'video' | 'audio' | 'overlay';
export type ClipKind = 'video' | 'image' | 'card' | 'audio';
export type MarkerColor = 'amber' | 'coral' | 'green' | 'cyan' | 'violet';
export type EffectIcon = 'fx' | 'title' | 'logo';

export interface ClipModel {
  name: string;
  kind?: ClipKind;            // default 'video'
  start: number;              // seconds on the timeline
  dur: number;                // seconds on the timeline (after speed)
  speed?: number;             // 0.25 … 4, badge shown when not 1
  muted?: boolean;
  locked?: boolean;
  selected?: boolean;
  hover?: boolean;            // shows trim grips
  trim?: 'l' | 'r';           // edge being dragged, drawn in accent
  ghost?: boolean;            // drag ghost
  lift?: number;              // ghost lift in px
  fx?: EffectIcon[];          // overlay and effect markers in the band
  shots?: number[];           // placeholder frame styles for the filmstrip
  color?: string;             // colour-card colour
  fadeIn?: number; fadeOut?: number;               // audio, seconds
  crossfades?: { at: number; dur: number }[];      // playlist song boundaries
  seed?: number; showDur?: boolean;
}
export interface OverlaySpan { name: string; start: number; dur: number; icon?: 'title' | 'logo'; selected?: boolean; }
export interface TransitionModel { at: number; dur: number; type?: string; selected?: boolean; }
export interface TrackModel {
  id: string;                 // 'V1', 'A1', 'T1' …
  kind: TrackKind;
  name: string;
  height?: number;            // default from track-h-* tokens
  magnetic?: boolean;         // video only; V1 true by default
  hidden?: boolean; muted?: boolean; locked?: boolean; selected?: boolean; hover?: boolean;
  items?: (ClipModel | OverlaySpan)[];
  transitions?: TransitionModel[];
  overflow?: { from: number; to: number; side: 'l' | 'r'; label?: string }[];  // unused source while trimming
  inserts?: { t: number; drop?: boolean }[];                                  // insertion markers
  drop?: boolean;             // lane is a drop target
  fadeKnobs?: number[];
}
export interface Marker { t: number; color?: MarkerColor; label?: string; }
export interface TimelineModel {
  fps: number;                // project frame rate
  pps: number;                // pixels per second (zoom)
  duration: number;           // seconds
  playhead?: number;
  range?: [number, number];   // work area in/out
  markers?: Marker[];
  beats?: number[];
  showBeats?: boolean;
  snap?: number;              // time of the active snap line
  band?: [number, number, number, number]; // rubber band: t0, t1, top px, height px
  tracks: TrackModel[];
  scrollbar?: boolean;
  extra?: string[];           // overlay HTML (tooltips) in body coordinates
}

export interface UltraVideoNamespace {
  timeline(el: HTMLElement, model: TimelineModel): HTMLElement;
  ruler(model: TimelineModel, widthPx: number): string;
  trackHeader(track: TrackModel, heightPx: number): string;
  clip(clip: ClipModel, x: number, w: number, laneH: number, fps?: number): string;
  icon(name: string, cls?: string): string;   // 16px stroke icon as SVG markup
  icons: string[];
  timecode(seconds: number, fps?: number, short?: boolean): string;
  peaksPath(w: number, h: number, seed?: number, step?: number): string;
  demoModel(overrides?: Partial<TimelineModel>): TimelineModel;
}
declare global { interface Window { UltraVideo: UltraVideoNamespace; } }
