// Renders SVG files to PNG with headless Chromium, each at its own size:
//   node shot.mjs <playwright index.mjs> <in.svg> <out.png> [<in.svg> <out.png> ...]
import { readFileSync } from 'node:fs';
const [, , pw, ...files] = process.argv;
const { chromium } = await import(pw);
const browser = await chromium.launch();
for (let i = 0; i < files.length; i += 2) {
    const [, width, height] = readFileSync(files[i], 'utf8').match(/width="(\d+)" height="(\d+)"/);
    const page = await browser.newPage({ viewport: { width: +width, height: +height }, deviceScaleFactor: 2 });
    await page.goto('file://' + files[i]);
    await page.screenshot({ path: files[i + 1] });
    await page.close();
}
await browser.close();
