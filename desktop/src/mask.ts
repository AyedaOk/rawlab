export type Mark = { tool: string; points: [number, number][]; radius?: number; hardness?: number };
const clamp = (n: number) => Math.max(0, Math.min(1, n));

// Match Mask::paintSegment, ellipseMask and linearGradientMask in Mask.cpp.
export function paintMask(values: Float32Array, w: number, h: number, mark: Mark, start = 0) {
  if (!mark.points.length) return;
  const first = mark.points[0], last = mark.points[mark.points.length - 1];
  const radius = Math.max(.001, Math.min(1, mark.radius ?? .03)) * Math.min(w, h);
  const hardRadius = radius * clamp(mark.hardness ?? .8);
  if (mark.tool === "Brush" || mark.tool === "Erase") {
    for (let i = start; i < mark.points.length; i++) {
      const a = mark.points[Math.max(0, i - 1)], b = mark.points[i];
      const x0 = a[0] * w, y0 = a[1] * h, x1 = b[0] * w, y1 = b[1] * h;
      const dx = x1 - x0, dy = y1 - y0, length = dx * dx + dy * dy;
      for (let y = Math.max(0, Math.floor(Math.min(y0, y1) - radius)); y <= Math.min(h - 1, Math.ceil(Math.max(y0, y1) + radius)); y++) {
        for (let x = Math.max(0, Math.floor(Math.min(x0, x1) - radius)); x <= Math.min(w - 1, Math.ceil(Math.max(x0, x1) + radius)); x++) {
          const t = length ? clamp(((x + .5 - x0) * dx + (y + .5 - y0) * dy) / length) : 0;
          const distance = Math.hypot(x + .5 - x0 - t * dx, y + .5 - y0 - t * dy);
          const alpha = distance <= hardRadius ? 1 : distance >= radius ? 0
            : clamp((radius - distance) / (radius - hardRadius));
          const index = y * w + x;
          values[index] += ((mark.tool === "Erase" ? 0 : 1) - values[index]) * alpha;
        }
      }
    }
    return;
  }
  const dx = (last[0] - first[0]) * w, dy = (last[1] - first[1]) * h;
  const length = dx * dx + dy * dy;
  const cx = (first[0] + last[0]) * w / 2, cy = (first[1] + last[1]) * h / 2;
  const rx = Math.max(.5, Math.abs(dx) / 2), ry = Math.max(.5, Math.abs(dy) / 2);
  for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
    const value = mark.tool === "Ellipse"
      ? Number(((x + .5 - cx) / rx) ** 2 + ((y + .5 - cy) / ry) ** 2 <= 1)
      : length ? clamp(((x - first[0] * w) * dx + (y - first[1] * h) * dy) / length) : 0;
    values[y * w + x] = Math.max(values[y * w + x], value);
  }
}

function distanceTransform(source: Float32Array, w: number, h: number, inside: boolean) {
  const infinity = 1e20;
  const temporary = new Float32Array(source.length).fill(infinity);
  const result = new Float32Array(source.length).fill(infinity);
  const sites = new Int32Array(Math.max(w, h));
  const boundaries = new Float64Array(sites.length + 1);
  function line(length: number, read: (q: number) => number, write: (q: number, value: number) => void) {
    let count = 0;
    for (let q = 0; q < length; q++) {
      const value = read(q);
      if (value >= infinity / 2) continue;
      let boundary = 0;
      while (count > 0) {
        const previous = sites[count - 1];
        boundary = (value + q * q - read(previous) - previous * previous) / (2 * (q - previous));
        if (boundary > boundaries[count - 1]) break;
        count--;
      }
      sites[count] = q;
      boundaries[count] = count ? boundary : -infinity;
      boundaries[++count] = infinity;
    }
    if (!count) return;
    let site = 0;
    for (let q = 0; q < length; q++) {
      while (site + 1 < count && boundaries[site + 1] < q) site++;
      write(q, (q - sites[site]) ** 2 + read(sites[site]));
    }
  }
  for (let y = 0; y < h; y++) line(w,
    x => (source[y * w + x] >= .5) === inside ? 0 : infinity,
    (x, value) => { temporary[y * w + x] = value; });
  for (let x = 0; x < w; x++) line(h,
    y => temporary[y * w + x], (y, value) => { result[y * w + x] = value; });
  return result;
}

// Radii are pixels, as in native featherMask/blurMask. Inversion happens last.
export function softenMask(source: Float32Array, w: number, h: number, feather: number, blur: number) {
  let values = source;
  if (feather > 0 && source.some(v => v >= .5) && source.some(v => v < .5)) {
    const inside = distanceTransform(source, w, h, true);
    const outside = distanceTransform(source, w, h, false);
    values = source.map((v, i) => {
      const distance = Math.max(0, Math.sqrt(v >= .5 ? outside[i] : inside[i]) - .5);
      const transition = .5 * Math.min(distance / feather, 1);
      return v >= .5 ? .5 + transition : .5 - transition;
    });
  }
  const extent = Math.ceil(blur);
  if (extent > 0) {
    const horizontal = new Float32Array(values.length), result = new Float32Array(values.length);
    const prefix = new Float64Array(Math.max(w, h) + 1);
    for (let y = 0; y < h; y++) {
      for (let x = 0; x < w; x++) prefix[x + 1] = prefix[x] + values[y * w + x];
      for (let x = 0; x < w; x++) {
        const first = Math.max(0, x - extent), last = Math.min(w - 1, x + extent);
        horizontal[y * w + x] = (prefix[last + 1] - prefix[first]) / (last - first + 1);
      }
    }
    for (let x = 0; x < w; x++) {
      for (let y = 0; y < h; y++) prefix[y + 1] = prefix[y] + horizontal[y * w + x];
      for (let y = 0; y < h; y++) {
        const first = Math.max(0, y - extent), last = Math.min(h - 1, y + extent);
        result[y * w + x] = (prefix[last + 1] - prefix[first]) / (last - first + 1);
      }
    }
    values = result;
  }
  return values;
}
