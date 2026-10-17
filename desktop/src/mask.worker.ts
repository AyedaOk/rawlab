import { depthRangeMask, paintMask, softenMask, type DepthPixels } from "./mask";

let depth: DepthPixels | undefined;

self.onmessage = ({ data }) => {
  if (data.depth) { depth = data.depth; return; }
  const { width, height, feather, blur, inverted, revision } = data;
  try {
    let values = data.values;
    if (data.depthRange && depth) {
      values = depthRangeMask(depth, width, height, data.depthRange);
      for (const mark of data.marks) paintMask(values, width, height, mark);
      if (data.live) paintMask(values, width, height, data.live);
    }
    const mask = softenMask(values, width, height, data.depthRange ? 0 : feather, blur);
    const pixels = new Uint8ClampedArray(width * height * 4);
    for (let i = 0; i < mask.length; i++) {
      pixels[i * 4] = 227;
      pixels[i * 4 + 1] = 79;
      pixels[i * 4 + 2] = 95;
      pixels[i * 4 + 3] = Math.round(102 * (inverted ? 1 - mask[i] : mask[i]));
    }
    self.postMessage({ pixels, width, height, revision }, { transfer: [pixels.buffer] });
  } catch (error) { self.postMessage({ revision, error: String(error) }); }
};
