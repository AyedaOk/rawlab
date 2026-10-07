import { softenMask } from "./mask";

self.onmessage = ({ data }) => {
  const { values, width, height, feather, blur, inverted, revision } = data;
  const mask = softenMask(values, width, height, feather, blur);
  const pixels = new Uint8ClampedArray(width * height * 4);
  for (let i = 0; i < mask.length; i++) {
    pixels[i * 4] = 227;
    pixels[i * 4 + 1] = 79;
    pixels[i * 4 + 2] = 95;
    pixels[i * 4 + 3] = Math.round(102 * (inverted ? 1 - mask[i] : mask[i]));
  }
  self.postMessage({ pixels, width, height, revision }, { transfer: [pixels.buffer] });
};
