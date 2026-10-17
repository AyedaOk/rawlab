import { forwardRef, useEffect, useImperativeHandle, useRef } from "react";
import { paintMask, type Mark, type DepthRange } from "./mask";

export type MaskOverlayHandle = { draw: (mark: Mark | null) => void };
type Props = { marks: Mark[]; aiMask?: string; feather: number; blur: number; inverted: boolean; showMask: boolean;
  depthPng?: string; depthRange?: DepthRange; onError?: (message: string) => void };

export default forwardRef<MaskOverlayHandle, Props>(function MaskOverlay(props, ref) {
  const canvas = useRef<HTMLCanvasElement>(null);
  const draw = useRef<(mark: Mark | null) => void>(() => {});
  const refresh = useRef(() => {});
  const settings = useRef(props);
  settings.current = props;
  useImperativeHandle(ref, () => ({ draw: mark => draw.current(mark) }), []);

  useEffect(() => {
    const surface = canvas.current!;
    const context = surface.getContext("2d")!;
    const worker = new Worker(new URL("./mask.worker.ts", import.meta.url), { type: "module" });
    let base = new Float32Array(0), working = base;
    let live: Mark | null = null, painted = 0;
    let frame = 0, busy = false, pending = false, revision = 0, disposed = false;
    let ai: HTMLImageElement | null = null;

    function send() {
      if (busy || !pending || !base.length) return;
      pending = false;
      busy = true;
      const values = working.slice();
      const edge = Math.min(surface.width, surface.height) / 1000;
      worker.postMessage({ values, width: surface.width, height: surface.height,
         feather: settings.current.feather * edge, blur: settings.current.blur * edge,
        inverted: settings.current.inverted, revision, depthRange: settings.current.depthRange,
        marks: props.marks, live }, [values.buffer]);
    }
    worker.onmessage = ({ data }) => {
      busy = false;
      if (data.revision === revision) {
        if (data.error) settings.current.onError?.(data.error);
        else context.putImageData(new ImageData(data.pixels, data.width, data.height), 0, 0);
      }
      send();
    };
    function schedule() {
      if (frame) return;
      frame = requestAnimationFrame(() => {
        frame = 0;
        if (live && !props.depthPng) {
          if (live.tool !== "Brush" && live.tool !== "Erase") {
            working = base.slice();
            painted = 0;
          }
          paintMask(working, surface.width, surface.height, live, painted);
          painted = live.points.length;
        }
        pending = true;
        send();
      });
    }
    function rebuild() {
      if (disposed) return;
      const rect = surface.getBoundingClientRect();
      if (!rect.width || !rect.height) return;
      // ponytail: cap interactive masks at 1024px; add viewport tiles if zoomed edges need more detail.
      const scale = Math.min(window.devicePixelRatio || 1, 1024 / Math.max(rect.width, rect.height));
      surface.width = Math.max(1, Math.round(rect.width * scale));
      surface.height = Math.max(1, Math.round(rect.height * scale));
      base = new Float32Array(surface.width * surface.height);
      if (ai) {
        context.drawImage(ai, 0, 0, surface.width, surface.height);
        const pixels = context.getImageData(0, 0, surface.width, surface.height).data;
        for (let i = 0; i < base.length; i++) base[i] = pixels[i * 4] / 255;
        context.clearRect(0, 0, surface.width, surface.height);
      }
      if (!props.depthPng) for (const mark of props.marks) paintMask(base, surface.width, surface.height, mark);
      working = base.slice();
      painted = 0;
      revision++;
      schedule();
    }
    draw.current = mark => {
      if (mark !== live) {
        working = base.slice();
        painted = 0;
        revision++;
      }
      live = mark;
      schedule();
    };
    refresh.current = () => { revision++; schedule(); };
    const observer = new ResizeObserver(rebuild);
    observer.observe(surface);
    rebuild();
    if (props.depthPng) {
      const image = new Image();
      image.onload = () => {
        if (disposed) return;
        const decoded = document.createElement("canvas");
        decoded.width = image.width; decoded.height = image.height;
        const ctx = decoded.getContext("2d")!;
        ctx.drawImage(image, 0, 0);
        const pixels = ctx.getImageData(0, 0, image.width, image.height).data;
        const values = new Float32Array(image.width * image.height);
        for (let i = 0; i < values.length; i++) values[i] = (pixels[i * 4] * 256 + pixels[i * 4 + 1]) / 65535;
        worker.postMessage({ depth: { values, width: image.width, height: image.height } }, [values.buffer]);
        revision++; schedule();
      };
      image.onerror = () => { if (!disposed) settings.current.onError?.("Could not decode the depth map."); };
      image.src = `data:image/png;base64,${props.depthPng}`;
    } else if (props.aiMask) {
      const image = new Image();
      image.onload = () => { ai = image; rebuild(); };
      image.src = `data:image/png;base64,${props.aiMask}`;
    }
    return () => {
      disposed = true;
      draw.current = () => {};
      refresh.current = () => {};
      observer.disconnect();
      cancelAnimationFrame(frame);
      worker.terminate();
    };
  }, [props.marks, props.aiMask, props.depthPng]);
  useEffect(() => { refresh.current(); }, [props.feather, props.blur, props.inverted, props.depthRange]);

  return <canvas ref={canvas} className="mask-overlay" aria-hidden="true"
    data-mark-count={props.marks.length} style={{ visibility: props.showMask ? "visible" : "hidden" }} />;
});
