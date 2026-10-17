import { useRef, type CSSProperties } from "react";
import { type DepthRange } from "./mask";

export default function DepthRangeControls({ value, preview, commit, disabled = false }: {
  value: DepthRange; preview: (value: DepthRange | undefined) => void;
  commit: (value: DepthRange) => void; disabled?: boolean;
}) {
  const dragging = useRef<HTMLInputElement | null>(null);
  const pending = useRef<DepthRange | undefined>(undefined);
  const finish = () => {
    if (!dragging.current) return;
    dragging.current = null;
    const next = pending.current;
    pending.current = undefined;
    preview(undefined);
    if (next) commit(next);
  };
  return <fieldset className="depth-controls" disabled={disabled}>
    <legend>Depth selection</legend>
    <div className="depth-range-track" style={{
      "--fade-start": `${value[0]}%`, "--range-start": `${value[1]}%`,
      "--range-end": `${value[2]}%`, "--fade-end": `${value[3]}%`,
      // Divide hit areas between the two handles on each side of the track, including at equal values.
      "--selection-split": `calc(7px + (100% - 14px) * ${(value[1] + value[2]) / 200})`,
      "--fade-split": `calc(7px + (100% - 14px) * ${(value[0] + value[3]) / 200})`,
    } as CSSProperties}>
      {value.map((position, index) => {
        const start = index < 2 ? 0 : 2;
        const label = ["Depth fade-in start", "Depth fully selected start", "Depth fully selected end", "Depth fade-out end"][index];
        return <input key={index} type="range" min="0" max="100" step="1"
          aria-label={label} title={`${label}: ${position}%`}
          aria-valuetext={`${position}% from near to far`} value={position} data-handle={index}
          onPointerDown={event => {
            if (event.button !== 0 || !event.isPrimary) return;
            finish();
            // Let the native range own capture; capturing its host breaks WebKit's thumb drag.
            dragging.current = event.currentTarget;
          }}
          onPointerUp={finish} onLostPointerCapture={finish}
          onBlur={event => { if (dragging.current === event.currentTarget) finish(); }}
          onPointerCancel={() => { dragging.current = null; pending.current = undefined; preview(undefined); }}
          onChange={event => {
            const next = [...(pending.current ?? value)] as DepthRange;
            next[index] = Math.max(next[start - 1] ?? 0, Math.min(next[start + 2] ?? 100, +event.target.value));
            if (index === start) next[index + 1] = Math.max(next[index], next[index + 1]);
            else next[index - 1] = Math.min(next[index], next[index - 1]);
            if (dragging.current) { pending.current = next; preview(next); }
            else commit(next);
          }} />;
      })}
    </div>
    <div className="depth-range-scale"><span>Near</span>
      <output>{value[0]}–{value[1]}% · {value[2]}–{value[3]}%</output><span>Far</span></div>
  </fieldset>;
}
