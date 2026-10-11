The full editor window: app bar, media bin, viewer with transport, inspector and the timeline dock.

**Consumer provides:** the panel contents. The layout itself only sizes and separates them.

- Panels sit on `bg-100`, separated by 1 px `bg-000` gutters. The viewer frame sits on `bg-000`.
- Default widths are `bin-w` and `inspector-w`. The timeline dock starts at 35 % of the height and is resized by the splitter.
- Below 1100 px window width, the inspector collapses into a tab beside the media bin.
