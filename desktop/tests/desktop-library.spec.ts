import { test, expect } from '@playwright/test';

const png = 'iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+jRZkAAAAASUVORK5CYII=';

function nativeBridge(paths: string[]) {
  return `
    const paths = ${JSON.stringify(paths)};
    const png = ${JSON.stringify(png)};
    const calls = [];
    Object.assign(window, { isTauri: true, __libraryCalls: calls, __TAURI_INTERNALS__: {
      invoke: async (command, args) => {
        calls.push({ command, args: args ? JSON.parse(JSON.stringify(args)) : args });
        if (command === 'external_session' || command === 'supersede_previews') return null;
        if (command === 'plugin:app|version') return '0.1.0';
        if (command === 'raw_source_version') return 'v1';
        if (command === 'processing_models') return { models: { denoise: [{ id: 'hf:rawdenoise-okeinla', name: 'Okeinla', available: true }], sharpen: [], provider: 'cpu' } };
        if (command === 'render_raw_preview') return { version: 1, width: 2, height: 2, png, quality: args.highQuality ? 'high' : 'fast', metadata: { rawWidth: 2, rawHeight: 2 } };
        if (command === 'process_raw') return { version: 1, width: 2, height: 2, png, before: png, resultId: 'result-1', output: args.output, quality: 'fast', sourceVersion: 'v1', metadata: {} };
        if (command === 'render_processed') return { version: 1, width: 2, height: 2, png, before: png, quality: 'fast', sourceVersion: 'v1', metadata: {} };
        if (command === 'desktop_control') {
          const request = args.request;
          if (request.operation === 'library_list') return { paths };
          if (request.operation === 'preset_list') return { names: [] };
          if (request.operation === 'batch_status') return { snapshot: null, jobs: [], completed: 0, remaining: 0 };
          if (request.operation === 'recipe_load') return { outcome: 'missing', recipe: null };
          if (request.operation === 'recipe_save') return { saved: true };
          if (request.operation === 'library_metadata') return { rating: 0, labels: [] };
          if (request.operation === 'library_metadata_save') return { rating: 0, labels: [] };
          if (request.operation === 'library_remove') return { paths: [] };
        }
        throw new Error('Unexpected native command: ' + command);
      }
    }});
  `;
}

function removeCalls(page: import('@playwright/test').Page, path: string) {
  return page.evaluate((target) => (window as any).__libraryCalls.some((call: any) =>
    call.command === 'desktop_control'
    && call.args.request.operation === 'library_remove'
    && call.args.request.paths.includes(target)), path);
}

test('removes library photographs with Delete and the context menu', async ({ page }) => {
  await page.addInitScript(nativeBridge(['/photos/a.ARW', '/photos/b.ARW']));
  await page.goto('/');
  const cards = page.locator('.photo-card');
  await expect(cards).toHaveCount(2);

  await page.getByRole('button', { name: 'Select a.ARW' }).click();
  await page.keyboard.press('Delete');
  await expect(page.getByRole('button', { name: 'Select a.ARW' })).toHaveCount(0);
  await expect(cards).toHaveCount(1);
  expect(await removeCalls(page, '/photos/a.ARW')).toBe(true);

  await page.getByRole('button', { name: 'Select b.ARW' }).click({ button: 'right' });
  await page.getByRole('menuitem', { name: 'Remove from library' }).click();
  await expect(page.getByRole('button', { name: 'Select b.ARW' })).toHaveCount(0);
  expect(await removeCalls(page, '/photos/b.ARW')).toBe(true);
  await expect(page.getByRole('button', { name: /Alpine lake/ })).toBeVisible();
});

test('header shows the application version instead of the design label', async ({ page }) => {
  await page.addInitScript(nativeBridge([]));
  await page.goto('/');
  await expect(page.locator('.brand small')).toHaveText('v0.1.0');
  await expect(page.getByText('DESIGN PREVIEW')).toHaveCount(0);
});

test('local mask layers inherit the default denoise model', async ({ page }) => {
  await page.addInitScript(nativeBridge(['/photos/a.ARW']));
  await page.goto('/');
  const card = page.getByRole('button', { name: 'Select a.ARW' });
  await expect(card).toBeVisible();
  await card.dblclick();
  await expect(page.locator('.main-photo')).toHaveAttribute('src', /^data:image\/png;base64,/);
  await page.getByRole('button', { name: 'Add mask', exact: true }).click();
  await page.getByRole('button', { name: 'Update', exact: true }).click();
  await expect.poll(() => page.evaluate(() => {
    const call = (window as any).__libraryCalls.filter((entry: any) => entry.command === 'process_raw').at(-1);
    return call?.args.recipe.layers[1]?.modelId;
  })).toBe('hf:rawdenoise-okeinla');
});

test('a failed recipe load no longer blocks closing', async ({ page }) => {
  await page.addInitScript(() => {
    const png = 'iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+jRZkAAAAASUVORK5CYII=';
    const callbacks = new Map<number, (event: any) => unknown>();
    let callbackId = 0;
    let closeHandler: (event: any) => unknown = () => {};
    let closeReady = false;
    let destroyed = 0;
    Object.assign(window, {
      isTauri: true,
      __destroyed: () => destroyed,
      __closeReady: () => closeReady,
      __requestClose: () => closeHandler({ event: 'tauri://close-requested', id: 1, payload: null }),
      __TAURI_INTERNALS__: {
        metadata: { currentWindow: { label: 'main' } },
        transformCallback: (callback: (event: any) => unknown) => {
          const id = ++callbackId;
          callbacks.set(id, callback);
          return id;
        },
        invoke: async (command: string, args: any) => {
          if (command === 'external_session' || command === 'supersede_previews') return null;
          if (command === 'plugin:event|listen') {
            if (args.event === 'tauri://close-requested') {
              closeHandler = callbacks.get(args.handler)!;
              closeReady = true;
            }
            return callbackId;
          }
          if (command === 'plugin:event|unlisten') return null;
          if (command === 'plugin:window|destroy') { destroyed++; return null; }
          if (command === 'processing_models') return { models: { denoise: [{ id: 'denoise', name: 'Denoise', available: true }], sharpen: [], provider: 'cpu' } };
          if (command === 'render_raw_preview') return { version: 1, width: 100, height: 80, png, quality: 'fast', metadata: { rawWidth: 100, rawHeight: 80 } };
          if (command === 'desktop_control') {
            const request = args.request;
            if (request.operation === 'library_list') return { paths: ['/photos/a.ARW'] };
            if (request.operation === 'preset_list') return { names: [] };
            if (request.operation === 'recipe_load') return { outcome: 'invalid', error: 'Malformed recipe JSON', recipe: null };
            if (request.operation === 'recipe_save') return { saved: true };
            if (request.operation === 'library_metadata' || request.operation === 'library_metadata_save') return { rating: 0, labels: [] };
          }
          throw new Error('Unexpected native command: ' + command);
        },
      },
    });
  });
  await page.goto('/');
  await expect(page.getByText(/Could not restore recipe for .*:.*Malformed recipe JSON/)).toBeVisible();
  await expect.poll(() => page.evaluate(() => (window as any).__closeReady())).toBe(true);
  await page.evaluate(() => (window as any).__requestClose());
  await expect.poll(() => page.evaluate(() => (window as any).__destroyed())).toBe(1);
});
