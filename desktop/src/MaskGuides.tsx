import { forwardRef, useEffect, useImperativeHandle, useRef } from "react";
import type { Mark } from "./mask";

export type MaskGuidesHandle = {
  move: (point: [number, number], mark: Mark | null) => void;
  hover: (x: number, y: number) => void;
  hide: () => void;
};

export default forwardRef<MaskGuidesHandle, { tool: string; radius: number; ratio: number; zoom: number }>(
  function MaskGuides({ tool, radius, ratio, zoom }, ref) {
    const svg = useRef<SVGSVGElement>(null);
    const ring = useRef<HTMLDivElement>(null);
    const hover = useRef<(x: number, y: number) => void>(() => {});
    const pointer = useRef({ x: 0, y: 0, visible: false });
    const shape = useRef<SVGEllipseElement>(null);
    const center = useRef<SVGCircleElement>(null);
    const gradient = useRef<SVGGElement>(null);
    const line = useRef<SVGLineElement>(null);
    const start = useRef<SVGCircleElement>(null);
    const end = useRef<SVGCircleElement>(null);
    const live = useRef<Mark | null>(null);
    const frame = useRef(0);
    const height = 1000 / ratio;
    const brush = tool === "Brush" || tool === "Erase";

    function schedule() {
      if (frame.current) return;
      frame.current = requestAnimationFrame(() => {
        frame.current = 0;
        const mark = live.current;
        shape.current!.style.display = center.current!.style.display = mark?.tool === "Ellipse" ? "" : "none";
        gradient.current!.style.display = mark?.tool === "Gradient" ? "" : "none";
        if (!mark) return;
        const a = mark.points[0], b = mark.points[mark.points.length - 1];
        const x0 = a[0] * 1000, y0 = a[1] * height, x1 = b[0] * 1000, y1 = b[1] * height;
        const attributes = (element: SVGElement, values: Record<string, number>) => {
          for (const [key, value] of Object.entries(values)) element.setAttribute(key, String(value));
        };
        if (mark.tool === "Ellipse") {
          attributes(shape.current!, { cx: (x0 + x1) / 2, cy: (y0 + y1) / 2, rx: Math.abs(x1 - x0) / 2, ry: Math.abs(y1 - y0) / 2 });
          attributes(center.current!, { cx: (x0 + x1) / 2, cy: (y0 + y1) / 2 });
        } else if (mark.tool === "Gradient") {
          attributes(line.current!, { x1: x0, y1: y0, x2: x1, y2: y1 });
          attributes(start.current!, { cx: x0, cy: y0 });
          attributes(end.current!, { cx: x1, cy: y1 });
        }
      });
    }
    useImperativeHandle(ref, () => ({
      move: (_point, mark) => { if (mark || live.current) { live.current = mark; schedule(); } },
      hover: (x, y) => {
        pointer.current.x = x; pointer.current.y = y; pointer.current.visible = true;
        hover.current(x, y);
      },
      hide: () => { pointer.current.visible = false; if (ring.current) ring.current.style.display = "none"; },
    }));
    useEffect(() => { schedule(); return () => { cancelAnimationFrame(frame.current); frame.current = 0; }; }, [tool, radius, ratio]);

    useEffect(() => {
      const parent = svg.current!.parentElement!;
      let bounds: DOMRect;
      function update() {
        bounds = parent.getBoundingClientRect();
        const diameter = radius * 2 * Math.min(bounds.width, bounds.height);
        // Keep native cursors within a conservative WebKit-compatible size.
        const large = brush && diameter > 60;
        const size = brush && !large ? Math.max(16, Math.ceil(diameter) + 4) : 32;
        const middle = Math.floor(size / 2);
        const cross = `<path d="M ${middle - 7} ${middle} h 14 M ${middle} ${middle - 7} v 14"/>`;
        const drawing = brush && !large
          ? `<circle cx="${middle}" cy="${middle}" r="${diameter / 2}"/>${tool === "Erase" ? `<path d="M ${middle - 3} ${middle} h 6"/>` : ""}`
          : cross + (tool === "Ellipse" ? '<ellipse cx="25" cy="26" rx="5" ry="3"/>'
            : tool === "Gradient" ? '<path d="M 23 30 L 30 23 M 24 23 H 30 V 29"/>'
            : tool === "Erase" ? '<path d="M 24 27 h 6"/>' : "");
        const image = `<svg xmlns="http://www.w3.org/2000/svg" width="${size}" height="${size}" fill="none"><g stroke="black" stroke-width="3">${drawing}</g><g stroke="white" stroke-width="1">${drawing}</g></svg>`;
        parent.style.setProperty("--mask-cursor", `url("data:image/svg+xml,${encodeURIComponent(image)}") ${middle} ${middle}, crosshair`);
        const element = ring.current!;
        element.style.display = "none";
        element.style.width = element.style.height = `${diameter / zoom}px`;
        hover.current = large ? (x, y) => {
          element.style.display = "block";
          element.style.transform = `translate3d(${(x - bounds.left) / zoom}px, ${(y - bounds.top) / zoom}px, 0) translate(-50%, -50%)`;
        } : () => {};
        if (pointer.current.visible) hover.current(pointer.current.x, pointer.current.y);
      }
      update();
      const observer = new ResizeObserver(update);
      observer.observe(parent);
      window.addEventListener("scroll", update, true);
      window.addEventListener("resize", update);
      return () => {
        observer.disconnect();
        window.removeEventListener("scroll", update, true);
        window.removeEventListener("resize", update);
        parent.style.removeProperty("--mask-cursor");
        hover.current = () => {};
      };
    }, [tool, radius, ratio, zoom, brush]);

    return <><div ref={ring} className="brush-size-ring" aria-hidden="true" style={{ display: "none" }} />
    <svg ref={svg} className="mask-guides" viewBox={`0 0 1000 ${height}`} aria-hidden="true">
      <defs><marker id="mask-gradient-arrow" viewBox="0 0 10 10" refX="10" refY="5" markerWidth="8" markerHeight="8" orient="auto-start-reverse">
        <path d="M 0 0 L 10 5 L 0 10" fill="none" stroke="white" />
      </marker></defs>
      <ellipse ref={shape} className="ellipse-guide" style={{ display: "none" }} />
      <circle ref={center} r="3" style={{ display: "none" }} />
      <g ref={gradient} className="gradient-guide" style={{ display: "none" }}>
        <line ref={line} markerEnd="url(#mask-gradient-arrow)" />
        <circle ref={start} r="4" /><circle ref={end} r="4" />
      </g>
    </svg></>;
  },
);
