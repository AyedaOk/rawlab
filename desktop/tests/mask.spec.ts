import { test, expect } from '@playwright/test';
import { paintMask, softenMask } from '../src/mask';

test('soft masks match distance falloff and clipped box averages', () => {
  const w = 9, h = 7;
  const source = new Float32Array(w * h);
  paintMask(source, w, h, { tool: 'Ellipse', points: [[.2, .2], [.8, .8]] });
  const feathered = softenMask(source, w, h, 2, 0);
  for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
    const inside = source[y * w + x] >= .5;
    let distance = Infinity;
    for (let yy = 0; yy < h; yy++) for (let xx = 0; xx < w; xx++) {
      if ((source[yy * w + xx] >= .5) !== inside)
        distance = Math.min(distance, Math.hypot(xx - x, yy - y));
    }
    const transition = .5 * Math.min(Math.max(0, distance - .5) / 2, 1);
    expect(feathered[y * w + x]).toBeCloseTo(.5 + (inside ? transition : -transition), 6);
  }
  const blurred = softenMask(source, w, h, 2, 1.2);
  for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
    let sum = 0, count = 0;
    for (let yy = Math.max(0, y - 2); yy <= Math.min(h - 1, y + 2); yy++)
      for (let xx = Math.max(0, x - 2); xx <= Math.min(w - 1, x + 2); xx++) {
        sum += feathered[yy * w + xx]; count++;
      }
    expect(blurred[y * w + x]).toBeCloseTo(sum / count, 6);
  }
  expect(softenMask(new Float32Array(12).fill(1), 4, 3, 8, 8)).toEqual(new Float32Array(12).fill(1));
  const full = new Float32Array(10000), incremental = new Float32Array(10000);
  const stroke = { tool: 'Brush', points: [[.1, .2], [.4, .5], [.8, .7]] as [number, number][] };
  paintMask(full, 100, 100, stroke);
  paintMask(incremental, 100, 100, { ...stroke, points: stroke.points.slice(0, 2) });
  paintMask(incremental, 100, 100, stroke, 2);
  expect(incremental).toEqual(full);
  paintMask(full, 100, 100, { tool: 'Erase', points: [[.4, .5]] });
  expect(full[50 * 100 + 40]).toBe(0);
  const small = new Float32Array(10000), large = new Float32Array(10000);
  paintMask(small, 100, 100, { tool: 'Brush', radius: .02, points: [[.5, .5]] });
  paintMask(large, 100, 100, { tool: 'Brush', radius: .1, points: [[.5, .5]] });
  expect(large.filter(v => v > 0).length).toBeGreaterThan(small.filter(v => v > 0).length * 10);
});

test('overlay previews feather, blur, inversion and cancelled strokes', async ({ page }) => {
  await page.goto('/');
  await page.locator('.photo-card').first().dblclick();
  await page.getByRole('button', { name: 'Add mask', exact: true }).click();
  await page.getByRole('button', { name: 'Edit Mask', exact: true }).click();
  await page.getByRole('button', { name: 'Ellipse', exact: true }).click();
  const frame = page.locator('.photo-frame');
  const box = (await frame.boundingBox())!;
  await page.mouse.move(box.x + box.width * .3, box.y + box.height * .3);
  await page.mouse.down();
  await page.mouse.move(box.x + box.width * .7, box.y + box.height * .7, { steps: 20 });
  await page.mouse.up();
  const overlay = page.locator('canvas.mask-overlay');
  const alpha = () => overlay.evaluate((canvas: HTMLCanvasElement) => {
    const pixels = canvas.getContext('2d')!.getImageData(0, 0, canvas.width, canvas.height).data;
    let soft = 0;
    for (let i = 3; i < pixels.length; i += 4) if (pixels[i] > 0 && pixels[i] < 102) soft++;
    return { soft, corner: pixels[3], center: pixels[(Math.floor(canvas.height / 2) * canvas.width + Math.floor(canvas.width / 2)) * 4 + 3] };
  });
  await expect.poll(async () => (await alpha()).center).toBe(102);
  expect((await alpha()).soft).toBe(0);
  await page.getByRole('slider', { name: 'Feather', exact: true }).fill('50');
  await expect.poll(async () => (await alpha()).soft).toBeGreaterThan(100);
  await page.getByRole('slider', { name: 'Feather', exact: true }).fill('0');
  await expect.poll(async () => (await alpha()).soft).toBe(0);
  await page.getByRole('slider', { name: 'Blur', exact: true }).fill('50');
  await expect.poll(async () => (await alpha()).soft).toBeGreaterThan(100);
  await page.getByRole('switch', { name: 'Invert mask', exact: true }).click();
  await expect.poll(async () => (await alpha()).corner).toBe(102);
  await expect.poll(async () => (await alpha()).center).toBe(0);
  await page.getByRole('button', { name: 'Brush', exact: true }).click();
  await page.mouse.move(box.x + box.width * .1, box.y + box.height * .1);
  await page.mouse.down();
  await frame.dispatchEvent('pointercancel');
  await page.mouse.up();
  await expect(overlay).toHaveAttribute('data-mark-count', '1');
});

test('long brush strokes update before release and commit once', async ({ page }) => {
  await page.goto('/');
  await page.locator('.photo-card').first().dblclick();
  await page.getByRole('button', { name: 'Add mask', exact: true }).click();
  await page.getByRole('button', { name: 'Edit Mask', exact: true }).click();
  await page.getByRole('button', { name: 'Brush', exact: true }).click();
  await page.getByRole('slider', { name: 'Feather', exact: true }).fill('20');
  await page.getByRole('slider', { name: 'Blur', exact: true }).fill('10');
  const box = (await page.locator('.photo-frame').boundingBox())!;
  await page.mouse.move(box.x + box.width * .1, box.y + box.height * .5);
  await page.mouse.down();
  const timings = await page.evaluate(async () => {
    const frame = document.querySelector('.photo-frame')!;
    const box = frame.getBoundingClientRect();
    const intervals: number[] = [];
    let previous = performance.now();
    // 2,400 points with continuous motion, delivered in 60 display frames.
    for (let tick = 0; tick < 60; tick++) {
      await new Promise<void>(resolve => requestAnimationFrame(now => {
        intervals.push(now - previous); previous = now;
        for (let point = 0; point < 40; point++) {
          const t = (tick * 40 + point) / 2399;
          frame.dispatchEvent(new PointerEvent('pointermove', {
            bubbles: true, pointerId: 1, buttons: 1,
            clientX: box.x + box.width * (.1 + .8 * t),
            clientY: box.y + box.height * (.5 + .2 * Math.sin(t * Math.PI * 8)),
          }));
        }
        resolve();
      }));
    }
    intervals.sort((a, b) => a - b);
    return { median: intervals[30], p95: intervals[57] };
  });
  console.log('Long stroke frame intervals (ms):', timings);
  const overlay = page.locator('canvas.mask-overlay');
  await expect(overlay).toHaveAttribute('data-mark-count', '0');
  await expect.poll(() => overlay.evaluate((canvas: HTMLCanvasElement) => {
    const pixels = canvas.getContext('2d')!.getImageData(0, 0, canvas.width, canvas.height).data;
    return pixels.some((v, i) => i % 4 === 3 && v > 0);
  })).toBe(true);
  await page.mouse.move(box.x + box.width * .9, box.y + box.height * .5);
  await page.mouse.up();
  await expect(overlay).toHaveAttribute('data-mark-count', '1');
});

test('mask history keeps the editor open and clear can be undone', async ({ page }) => {
  await page.goto('/');
  await page.locator('.photo-card').first().dblclick();
  await page.getByRole('button', { name: 'Add mask', exact: true }).click();
  const editor = page.getByRole('region', { name: 'Mask editing' });
  await expect(editor).toBeVisible();
  const box = (await page.locator('.photo-frame').boundingBox())!;
  for (const tool of ['Gradient', 'Brush']) {
    await page.getByRole('button', { name: tool, exact: true }).click();
    await page.mouse.move(box.x + box.width * .3, box.y + box.height * .3);
    await page.mouse.down();
    await page.mouse.move(box.x + box.width * .7, box.y + box.height * .7, { steps: 4 });
    await page.mouse.up();
  }
  const overlay = page.locator('.mask-overlay');
  await expect(overlay).toHaveAttribute('data-mark-count', '2');
  await page.keyboard.press('Control+z');
  await expect(editor).toBeVisible();
  await expect(editor.getByText('Editing Mask 1')).toBeVisible();
  await expect(overlay).toHaveAttribute('data-mark-count', '1');
  await editor.getByRole('button', { name: 'Redo', exact: true }).click();
  await expect(overlay).toHaveAttribute('data-mark-count', '2');
  await editor.getByRole('button', { name: 'Clear mask', exact: true }).click();
  await expect(overlay).toHaveAttribute('data-mark-count', '0');
  await editor.getByRole('button', { name: 'Undo', exact: true }).click();
  await expect(overlay).toHaveAttribute('data-mark-count', '2');
  const show = page.getByRole('switch', { name: 'Show mask', exact: true });
  await show.click();
  await expect(show).toHaveAttribute('aria-checked', 'false');
  await expect(overlay).toBeHidden();
  await show.press('Space');
  await expect(overlay).toBeVisible();
  await page.getByRole('button', { name: 'Dismiss notification' }).click();
  await expect(page.locator('.status-message')).toHaveText('');
});

test('tool cursors, brush sizing and shape guides follow mask editing', async ({page}) => {
  await page.goto('/');
  await page.locator('.photo-card').first().dblclick();
  await page.getByRole('button', {name:'Add mask',exact:true}).click();
  const surface = page.locator('.photo-frame');
  const box = (await surface.boundingBox())!;
  await page.getByRole('button', {name:'Brush',exact:true}).click();
  const size = page.getByRole('slider', {name:'Brush size'});
  await size.fill('12');
  await page.mouse.move(box.x + box.width * .4, box.y + box.height * .4);
  await expect(surface).toHaveCSS('cursor', /url\(.+crosshair/);
  const ring = page.locator('.brush-size-ring');
  await expect(ring).toBeVisible();
  const diameter = (await ring.boundingBox())!.width;
  await surface.focus();
  await page.keyboard.press(']');
  await expect(size).toHaveValue('13');
  await page.mouse.move(box.x + box.width * .41, box.y + box.height * .4);
  await expect.poll(async () => (await ring.boundingBox())?.width ?? 0).toBeGreaterThan(diameter);
  await page.getByRole('button', {name:'Erase',exact:true}).click();
  await expect(size).toHaveValue('6');
  await page.mouse.move(box.x + box.width * .45, box.y + box.height * .4);
  await expect(ring).toBeHidden();
  await expect(surface).toHaveCSS('cursor', /url\(.+crosshair/);
  await surface.focus();
  await page.keyboard.down('Space');
  await expect(surface).toHaveCSS('cursor', 'grab');
  await expect(page.locator('.mask-guides')).toHaveCount(0);
  await page.keyboard.up('Space');
  await page.getByRole('switch', {name:'Show mask',exact:true}).click();
  for (const tool of ['Ellipse', 'Gradient']) {
    await page.getByRole('button', {name:tool,exact:true}).click();
    await expect(size).toHaveCount(0);
    await page.mouse.move(box.x + box.width * .3, box.y + box.height * .3);
    await page.mouse.down();
    await page.mouse.move(box.x + box.width * .6, box.y + box.height * .6, {steps:3});
    const guide = page.locator(tool === 'Ellipse' ? '.ellipse-guide' : '.gradient-guide');
    await expect(guide).toBeVisible();
    await expect(page.locator('.mask-overlay')).toBeHidden();
    if (tool === 'Gradient') await expect(guide.locator('line')).toHaveAttribute('marker-end', 'url(#mask-gradient-arrow)');
    await page.mouse.up();
    await expect(guide).toBeHidden();
  }
  const tools = (await page.getByRole('group', {name:'Mask tools'}).boundingBox())!;
  const history = (await page.getByRole('group', {name:'Mask history'}).boundingBox())!;
  expect(history.y).toBeGreaterThan(tools.y + tools.height);
});

test('idle mask hover does not read layout or repaint SVG guides', async ({page}) => {
  await page.goto('/');
  await page.locator('.photo-card').first().dblclick();
  await page.getByRole('button', {name:'Add mask',exact:true}).click();
  for (const tool of ['Brush', 'Erase', 'Ellipse', 'Gradient']) {
    await page.getByRole('button', {name:tool,exact:true}).click();
    await expect(page.getByText(/tool selected · Hold Space/)).toHaveCount(0);
    await expect(page.locator('.photo-frame')).toHaveCSS('cursor', /url\(.+crosshair/);
    const result = await page.evaluate(async () => {
      const surface = document.querySelector('.photo-frame')!;
      const guides = document.querySelector('.mask-guides')!;
      const rect = surface.getBoundingClientRect();
      await new Promise(requestAnimationFrame);
      await new Promise(requestAnimationFrame);
      let reads = 0, mutations = 0;
      const original = surface.getBoundingClientRect;
      surface.getBoundingClientRect = () => { reads++; return original.call(surface); };
      const observer = new MutationObserver(records => { mutations += records.length; });
      observer.observe(guides, {attributes:true, childList:true, subtree:true});
      for (let frame = 0; frame < 5; frame++) {
        for (let i = 0; i < 50; i++) surface.dispatchEvent(new PointerEvent('pointermove', {
          bubbles:true, clientX:rect.x + rect.width * (.2 + i / 100), clientY:rect.y + rect.height * .5,
        }));
        await new Promise(requestAnimationFrame);
      }
      observer.disconnect();
      surface.getBoundingClientRect = original;
      return {reads, mutations};
    });
    expect(result).toEqual({reads:0, mutations:0});
  }
});

test('Shift wheel resizes each brush without scrolling or zooming', async ({page}) => {
  await page.goto('/');
  await page.locator('.photo-card').first().dblclick();
  await page.getByRole('button', {name:'Add mask',exact:true}).click();
  await page.getByRole('button', {name:'Brush',exact:true}).click();
  await expect(page.getByText(/Diameter relative to the short image edge/)).toHaveCount(0);
  const surface = page.locator('.photo-frame');
  const size = page.getByRole('slider', {name:'Brush size'});
  const transform = await surface.evaluate(element => element.style.transform);
  const wheel = (deltaY: number, deltaX = 0) => surface.evaluate((element, delta) => {
    const event = new WheelEvent('wheel', {bubbles:true, cancelable:true, shiftKey:true, ...delta});
    element.dispatchEvent(event);
    return event.defaultPrevented;
  }, {deltaY, deltaX});
  expect(await wheel(-120)).toBe(true);
  await expect(size).toHaveValue('7');
  expect(await wheel(120)).toBe(true);
  await expect(size).toHaveValue('6');
  expect(await wheel(0, -120)).toBe(true);
  await expect(size).toHaveValue('7');
  await page.getByRole('button', {name:'Erase',exact:true}).click();
  await expect(size).toHaveValue('6');
  await size.fill('50');
  await wheel(-120);
  await expect(size).toHaveValue('50');
  await size.fill('0.2');
  await wheel(120);
  await expect(size).toHaveValue('0.2');
  await page.getByRole('button', {name:'Brush',exact:true}).click();
  await expect(size).toHaveValue('7');
  expect(await surface.evaluate(element => element.style.transform)).toBe(transform);
  await page.getByRole('button', {name:'Ellipse',exact:true}).click();
  expect(await wheel(-120)).toBe(false);
});
